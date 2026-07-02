<?php

/**
 * @file
 * Adds encrypt-as-behaviour to existing Backdrop text field types.
 *
 * Works by registering a custom widget (acuity_encrypt_widget) that targets
 * the standard text, text_long, and text_with_summary field types. Selecting
 * this widget in Field UI is the encryption signal — no separate checkbox or
 * instance setting is needed.
 *
 * Encryption is transparent: hook_field_attach_load() decrypts on every
 * entity load, so Views, tokens, search indexes, and all other consumers
 * receive plaintext automatically. The protection is at-rest (database
 * dumps) — it is not an access control mechanism. Use Backdrop's normal role
 * permissions and Views access rules to control who sees the content.
 *
 * Edit form: hook_field_widget_form() masks the value behind a
 * "Reveal to edit" button. Users without 'view encrypted fields' see a
 * locked placeholder and cannot modify the field.
 */

// ── Widget registration ───────────────────────────────────────────────────────

/**
 * Implements hook_field_widget_info().
 *
 * Registers the Acuity Encrypt masked widget for all standard Backdrop text
 * field types. Choosing this widget in Field UI enables AES-256-GCM at-rest
 * encryption for the field automatically.
 */
function acuity_encrypt_field_widget_info(): array {
  return [
    'acuity_encrypt_widget' => [
      'label'       => t('Acuity Encrypt (masked text)'),
      'field types' => ['text', 'text_long', 'text_with_summary'],
      'settings'    => [],
      'behaviors'   => [
        'multiple values' => FIELD_BEHAVIOR_DEFAULT,
        'default value'   => FIELD_BEHAVIOR_DEFAULT,
      ],
    ],
  ];
}

// ── Widget form ───────────────────────────────────────────────────────────────

/**
 * Implements hook_field_widget_form().
 *
 * Renders the encrypted field widget on entity edit forms. By the time this
 * runs, hook_field_attach_load() has already decrypted the stored value, so
 * $items contains plaintext.
 *
 * Single-line text fields (text):
 *   Masked with a display:none textfield + reveal button. The real input is
 *   always submitted and re-encrypted by presave.
 *
 * Long text fields (text_long, text_with_summary):
 *   Uses #type => 'text_format' so CKEditor/WYSIWYG is wired up automatically.
 *   Masked with a position:absolute overlay so CKEditor can initialise
 *   underneath; clicking "Reveal to edit" removes the overlay.
 *
 * Unauthorised users (no 'view encrypted fields') with existing data:
 *   '#type' => 'value' elements preserve plaintext through the form submission
 *   without rendering it in the DOM. Presave re-encrypts on save.
 *
 * Empty fields (any user):
 *   Widget renders unmasked — nothing to mask yet.
 */
function acuity_encrypt_field_widget_form(array &$_form, array &$_form_state, array $field, array $instance, string $_langcode, array $items, int $delta, array $element): array {
  $stored   = isset($items[$delta]['value']) ? $items[$delta]['value'] : '';
  $has_data = $stored !== '';
  $can_view = user_access('view encrypted fields');
  $is_long  = in_array($field['type'], ['text_long', 'text_with_summary'], TRUE);
  $rows     = !empty($instance['widget']['settings']['rows']) ? (int) $instance['widget']['settings']['rows'] : 5;
  $format   = isset($items[$delta]['format']) ? $items[$delta]['format'] : NULL;

  $module_path = backdrop_get_path('module', 'acuity_encrypt');

  // ── Long text / WYSIWYG ────────────────────────────────────────────────────

  if ($is_long) {
    // Unauthorised user with existing data: preserve value+format as hidden
    // value elements so they pass through the form submission unchanged.
    if ($has_data && !$can_view) {
      $element['value'] = ['#type' => 'value', '#value' => $stored];
      $element['format'] = ['#type' => 'value', '#value' => $format];
      $element['acuity_locked'] = [
        '#markup' => '<div class="form-item acuity-encrypt-locked">'
          . '<label>' . check_plain($element['#title']) . '</label>'
          . '<span class="acuity-widget-mask-display form-text">••••••••</span>'
          . '&nbsp;<span class="description">'
          . t('Encrypted — you do not have permission to view or edit this field.')
          . '</span>'
          . '</div>',
        '#weight' => -1,
        '#attached' => ['css' => [$module_path . '/css/acuity_encrypt.css']],
      ];
      return $element;
    }

    // Authorised user or empty field: use text_format so CKEditor wires up.
    // Mirror what the core text widget does (text_field_widget_form).
    $element['#type']          = 'text_format';
    $element['#default_value'] = $stored;
    $element['#format']        = $format;
    $element['#base_type']     = 'textarea';
    $element['#rows']          = $rows;

    // Empty field — show the editor unmasked.
    if (!$has_data) {
      return $element;
    }

    // Authorised user with existing data: overlay mask.
    // CKEditor sits in normal flow and initialises under the overlay; clicking
    // "Reveal to edit" removes the overlay with JS.
    $mask_html = '<div class="acuity-wysiwyg-mask">'
      . '<input type="text" class="form-text acuity-widget-mask-display" value="••••••••" readonly tabindex="-1" aria-hidden="true">'
      . '&nbsp;<button type="button" class="acuity-widget-reveal-btn" data-mode="overlay">'
      . '&#128065;&nbsp;' . t('Reveal to edit')
      . '</button>'
      . '</div>';

    $element['#prefix'] = '<div class="acuity-encrypt-wysiwyg-wrap">';
    $element['#suffix'] = $mask_html . '</div>';

    $element['#attached']['js'][]  = $module_path . '/js/acuity_encrypt.js';
    $element['#attached']['css'][] = $module_path . '/css/acuity_encrypt.css';

    return $element;
  }

  // ── Single-line text ───────────────────────────────────────────────────────

  $element['value'] = [
    '#type'          => 'textfield',
    '#title'         => $element['#title'],
    '#description'   => $element['#description'],
    '#required'      => $element['#required'],
    '#default_value' => $stored,
    '#size'          => 60,
    '#maxlength'     => !empty($field['settings']['max_length']) ? (int) $field['settings']['max_length'] : 255,
    '#attributes'    => ['class' => ['text-full']],
  ];

  // Empty field — show the real input unmasked.
  if (!$has_data) {
    return $element;
  }

  $element['value']['#attached']['js'][]  = $module_path . '/js/acuity_encrypt.js';
  $element['value']['#attached']['css'][] = $module_path . '/css/acuity_encrypt.css';

  // Unauthorised user: replace with value element so plaintext never hits DOM.
  if (!$can_view) {
    $element['value'] = ['#type' => 'value', '#value' => $stored];
    $element['acuity_locked'] = [
      '#markup' => '<div class="form-item acuity-encrypt-locked">'
        . '<label>' . check_plain($element['#title']) . '</label>'
        . '<span class="acuity-widget-mask-display form-text">••••••••</span>'
        . '&nbsp;<span class="description">'
        . t('Encrypted — you do not have permission to view or edit this field.')
        . '</span>'
        . '</div>',
      '#weight' => -1,
    ];
    return $element;
  }

  // Authorised user: hide the real input behind a mask + reveal button.
  // display:none keeps the input submittable; presave re-encrypts on save.
  $element['value']['#attributes']['class'][]      = 'acuity-widget-edit-input';
  $element['value']['#attributes']['style']        = 'display:none';
  $element['value']['#attributes']['autocomplete'] = 'off';

  $element['value']['#prefix'] = '<div class="acuity-encrypt-widget-wrap">'
    . '<div class="acuity-widget-mask-row">'
    . '<input type="text" class="form-text acuity-widget-mask-display" value="••••••••" readonly tabindex="-1" aria-hidden="true">'
    . '&nbsp;<button type="button" class="acuity-widget-reveal-btn" data-mode="inline">'
    . '&#128065;&nbsp;' . t('Reveal to edit')
    . '</button>'
    . '</div>';

  $element['value']['#suffix'] = '</div>';

  return $element;
}

// ── Display formatters ────────────────────────────────────────────────────────

/**
 * Implements hook_field_formatter_info().
 *
 * Registers two formatters for text fields using the acuity_encrypt_widget.
 * Site builders select these in Manage Display for their encrypted fields.
 */
function acuity_encrypt_field_formatter_info(): array {
  $types = ['text', 'text_long', 'text_with_summary'];
  return [
    'acuity_encrypt_reveal' => [
      'label'       => t('Acuity Encrypt: masked with reveal button'),
      'field types' => $types,
    ],
    'acuity_encrypt_plain' => [
      'label'       => t('Acuity Encrypt: plain decrypted text'),
      'field types' => $types,
    ],
  ];
}

/**
 * Implements hook_field_formatter_view().
 *
 * acuity_encrypt_reveal: shows ••••••• + a "Reveal" toggle button.
 *   - Authorised users (view encrypted fields): decrypted value hidden in DOM,
 *     revealed by JS on click.
 *   - Unauthorised users: ••••••• only (no button, no value in DOM).
 *
 * acuity_encrypt_plain: renders the decrypted value directly using the field's
 *   stored text format. Use this where the masked reveal UX is not needed.
 *
 * By the time this runs, hook_field_attach_load() has already decrypted the
 * values and refreshed safe_value, so $items contains plaintext.
 */
function acuity_encrypt_field_formatter_view(string $_entity_type, object $_entity, array $_field, array $_instance, string $_langcode, array &$items, array $display): array {
  $elements    = [];
  $module_path = backdrop_get_path('module', 'acuity_encrypt');

  foreach ($items as $delta => $item) {
    $value = isset($item['value']) ? $item['value'] : '';

    if ($display['type'] === 'acuity_encrypt_plain') {
      $elements[$delta] = [
        '#markup' => !empty($item['safe_value']) ? $item['safe_value'] : check_plain($value),
      ];
      continue;
    }

    // acuity_encrypt_reveal
    if (!user_access('view encrypted fields')) {
      $elements[$delta] = [
        '#markup' => '<span class="acuity-encrypt-masked">••••••••</span>',
        '#attached' => ['css' => [$module_path . '/css/acuity_encrypt.css']],
      ];
      continue;
    }

    $rendered = !empty($item['safe_value']) ? $item['safe_value'] : check_plain($value);

    $elements[$delta] = [
      '#markup' => '<div class="acuity-encrypt-wrap">'
        . '<span class="acuity-encrypt-masked">••••••••</span>'
        . '&nbsp;<button type="button" class="acuity-encrypt-reveal-btn">'
        . t('Reveal') . '</button>'
        . '<span class="acuity-encrypt-value" style="display:none">'
        . $rendered
        . '</span>'
        . '</div>',
      '#attached' => [
        'js'  => [$module_path . '/js/acuity_encrypt.js'],
        'css' => [$module_path . '/css/acuity_encrypt.css'],
      ],
    ];
  }

  return $elements;
}

// ── Transparent decrypt on load ───────────────────────────────────────────────

/**
 * Implements hook_field_attach_load().
 *
 * Decrypts values for any field instance using the acuity_encrypt_widget.
 * Called after field data has been loaded from the database for one or more
 * entities.
 *
 * The text module's hook_field_load() runs before this hook and sets
 * safe_value/safe_summary from the encrypted ciphertext. We recompute those
 * after decryption so that the standard text formatters (and any other
 * consumer of safe_value) receive the correct plaintext output.
 *
 * Uses backdrop_static() to cache the list of encrypted instances per
 * entity type + bundle, avoiding repeated field_info_instances() lookups
 * on sites that load many entities of the same type.
 */
function acuity_encrypt_field_attach_load(string $entity_type, array $entities, $_age, array $_options): void {
  if (!acuity_encrypt_key_available()) {
    return;
  }

  $cache = &backdrop_static(__FUNCTION__, []);

  foreach ($entities as $entity) {
    list(, , $bundle) = entity_extract_ids($entity_type, $entity);
    $cache_key = $entity_type . ':' . $bundle;

    if (!isset($cache[$cache_key])) {
      $cache[$cache_key] = [];
      foreach (field_info_instances($entity_type, $bundle) as $field_name => $instance) {
        if ($instance['widget']['type'] === 'acuity_encrypt_widget') {
          $field = field_info_field($field_name);
          $cache[$cache_key][$field_name] = $field['type'];
        }
      }
    }

    foreach ($cache[$cache_key] as $field_name => $field_type) {
      if (!isset($entity->{$field_name})) {
        continue;
      }
      foreach ($entity->{$field_name} as $langcode => &$lang_items) {
        foreach ($lang_items as &$item) {
          if (!empty($item['value']) && acuity_encrypt_is_encrypted($item['value'])) {
            $plain = acuity_encrypt_decrypt($item['value']);
            if ($plain !== FALSE) {
              $item['value'] = $plain;
              // Recompute safe_value so the text formatter and other consumers
              // get plaintext, not the ciphertext that hook_field_load() saw.
              if (!empty($item['format'])) {
                $item['safe_value'] = check_markup($item['value'], $item['format'], $langcode, FALSE);
              }
            }
          }
          if ($field_type === 'text_with_summary'
              && !empty($item['summary'])
              && acuity_encrypt_is_encrypted($item['summary'])) {
            $plain = acuity_encrypt_decrypt($item['summary']);
            if ($plain !== FALSE) {
              $item['summary'] = $plain;
              if (!empty($item['format'])) {
                $item['safe_summary'] = check_markup($item['summary'], $item['format'], $langcode, FALSE);
              }
            }
          }
        }
        unset($item);
      }
      unset($lang_items);
    }
  }
}

// ── Encrypt before save ───────────────────────────────────────────────────────

/**
 * Implements hook_field_attach_presave().
 *
 * Encrypts values for any field instance using the acuity_encrypt_widget
 * before the entity is written to the database. Already-encrypted values
 * (enc\d+: prefix) are skipped to prevent double-encryption.
 */
function acuity_encrypt_field_attach_presave(string $entity_type, object $entity): void {
  if (!acuity_encrypt_key_available()) {
    watchdog('acuity_encrypt',
      'Presave: no encryption key configured — encrypted fields on @type saved as plaintext.',
      ['@type' => $entity_type],
      WATCHDOG_WARNING
    );
    return;
  }

  list(, , $bundle) = entity_extract_ids($entity_type, $entity);

  foreach (field_info_instances($entity_type, $bundle) as $field_name => $instance) {
    if ($instance['widget']['type'] !== 'acuity_encrypt_widget') {
      continue;
    }
    $field = field_info_field($field_name);
    if (!isset($entity->{$field_name})) {
      continue;
    }

    foreach ($entity->{$field_name} as $_langcode => &$lang_items) {
      foreach ($lang_items as &$item) {
        if (!empty($item['value']) && !acuity_encrypt_is_encrypted($item['value'])) {
          $enc = acuity_encrypt_encrypt($item['value']);
          if ($enc !== FALSE) {
            $item['value'] = $enc;
          }
        }
        if ($field['type'] === 'text_with_summary'
            && !empty($item['summary'])
            && !acuity_encrypt_is_encrypted($item['summary'])) {
          $enc = acuity_encrypt_encrypt($item['summary']);
          if ($enc !== FALSE) {
            $item['summary'] = $enc;
          }
        }
      }
      unset($item);
    }
    unset($lang_items);
  }
}
