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
 * Decryption is permission-gated: hook_field_attach_load() decrypts on
 * entity load ONLY when the current user has 'view encrypted fields'. For
 * those users, Views, tokens, and other consumers receive plaintext
 * automatically. For everyone else (including cron/search indexing, which
 * runs unprivileged) the ciphertext stays in place — core formatters render
 * ciphertext, this module's widget and formatters render a mask. Fail
 * closed.
 *
 * This gate is only safe because acuity_encrypt_entity_info_alter()
 * disables persistent entity/field caching for encrypted bundles: nothing
 * user-dependent (and no plaintext) is ever written to a cache table.
 *
 * Edit form: hook_field_widget_form() masks the value behind a
 * "Reveal to edit" button. Users without 'view encrypted fields' see a
 * locked placeholder and cannot modify the field; the stored ciphertext is
 * passed through the form submission untouched.
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
 *   attach_load did not decrypt for them, so $items still holds ciphertext.
 *   '#type' => 'value' elements pass it through the form submission without
 *   rendering anything in the DOM; presave skips already-encrypted values,
 *   so the stored value survives the save byte-for-byte.
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
    // Unauthorised user with existing data: preserve value+format (and
    // summary) as hidden value elements so they pass through the form
    // submission unchanged.
    if ($has_data && !$can_view) {
      $element['value'] = ['#type' => 'value', '#value' => $stored];
      $element['format'] = ['#type' => 'value', '#value' => $format];
      if ($field['type'] === 'text_with_summary') {
        $element['summary'] = [
          '#type' => 'value',
          '#value' => isset($items[$delta]['summary']) ? $items[$delta]['summary'] : NULL,
        ];
      }
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
    // Core's text.js locates the main field by this class to place the
    // "Edit summary" link next to its label; core's own text widgets always
    // set it. Without it the summary link is appended to a detached label
    // and never appears.
    $element['#attributes']['class'][] = 'text-full';

    // Summary sub-element — same construction as core's
    // text_textarea_with_summary widget, including text.js, which provides
    // the "Edit summary" toggle link. Without this child element the form
    // would silently drop the stored summary on save. When the field has
    // data, the summary sits inside the overlay wrapper below, so it is
    // masked and revealed together with the main value.
    if ($field['type'] === 'text_with_summary') {
      $summary = isset($items[$delta]['summary']) ? $items[$delta]['summary'] : NULL;
      $display_summary = !empty($summary) || !empty($instance['settings']['display_summary']);
      $element['summary'] = [
        '#type'          => $display_summary ? 'textarea' : 'value',
        '#default_value' => $summary,
        '#title'         => t('Summary'),
        '#rows'          => !empty($instance['widget']['settings']['summary_rows']) ? (int) $instance['widget']['settings']['summary_rows'] : 5,
        '#description'   => t('Leave blank to use trimmed value of full text as the summary.'),
        '#attached'      => ['js' => [backdrop_get_path('module', 'text') . '/js/text.js']],
        '#attributes'    => ['class' => ['text-summary']],
        '#prefix'        => '<div class="text-summary-wrapper">',
        '#suffix'        => '</div>',
        '#weight'        => -10,
      ];
    }

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

  // The ENCRYPTED value must fit the field's column, so the usable plaintext
  // limit is smaller than the field's max_length (~1.34x inflation + 45
  // fixed chars). Enforce it here: #maxlength stops most over-typing, the
  // element validator is the byte-accurate authority (multibyte input can
  // pass #maxlength but still exceed the byte limit).
  $max_length = !empty($field['settings']['max_length']) ? (int) $field['settings']['max_length'] : 255;
  $plain_limit = acuity_encrypt_max_plaintext_bytes($max_length);

  $element['value'] = [
    '#type'          => 'textfield',
    '#title'         => $element['#title'],
    '#description'   => trim(($element['#description'] ? $element['#description'] . ' ' : '')
      . t('Encrypted field — maximum @count characters (fewer if using accented or non-Latin characters).', ['@count' => $plain_limit])),
    '#required'      => $element['#required'],
    '#default_value' => $stored,
    '#size'          => 60,
    '#maxlength'     => min($max_length, max(1, $plain_limit)),
    '#attributes'    => ['class' => ['text-full']],
    '#acuity_encrypt_max_bytes' => $plain_limit,
    '#element_validate' => ['acuity_encrypt_widget_length_validate'],
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

/**
 * Element validate: reject plaintext whose ENCRYPTED form exceeds the column.
 *
 * Byte-accurate (strlen), unlike #maxlength which counts characters. Without
 * this, an over-long value reaches the database where — depending on SQL
 * mode — the save fails outright or the ciphertext is silently truncated
 * and becomes permanently undecryptable.
 */
function acuity_encrypt_widget_length_validate(array $element, array &$form_state): void {
  $value = (string) $element['#value'];
  if ($value === '') {
    return;
  }
  $limit = (int) $element['#acuity_encrypt_max_bytes'];
  if (strlen($value) > $limit) {
    form_error($element, t(
      '%title: too long to encrypt — this field can hold at most @limit bytes of text once encryption overhead is added (you entered @actual bytes). Shorten the value, or ask an administrator to raise the field\'s maximum length.',
      ['%title' => $element['#title'], '@limit' => $limit, '@actual' => strlen($value)]
    ));
  }
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
  $settings = ['hide_denied' => 0];
  return [
    'acuity_encrypt_reveal' => [
      'label'       => t('Acuity Encrypt: masked with reveal button'),
      'field types' => $types,
      'settings'    => $settings,
    ],
    'acuity_encrypt_plain' => [
      'label'       => t('Acuity Encrypt: plain decrypted text'),
      'field types' => $types,
      'settings'    => $settings,
    ],
    // Fail-closed replacement for core's "Summary or trimmed" — the core
    // formatter renders whatever is in the item, which for unauthorised
    // viewers is raw ciphertext.
    'acuity_encrypt_summary' => [
      'label'       => t('Acuity Encrypt: summary or trimmed'),
      'field types' => ['text_with_summary'],
      'settings'    => $settings + ['trim_length' => 600],
    ],
  ];
}

/**
 * Returns the static masked element rendered for undecrypted values.
 */
function _acuity_encrypt_masked_element(string $module_path): array {
  return [
    '#markup' => '<span class="acuity-encrypt-masked acuity-encrypt-masked-text">'
      . t('Encrypted')
      . '</span>',
    '#attached' => ['css' => [$module_path . '/css/acuity_encrypt.css']],
  ];
}

/**
 * Implements hook_field_formatter_settings_form().
 */
function acuity_encrypt_field_formatter_settings_form(array $field, array $instance, string $view_mode, array $form, array &$form_state): array {
  $display  = $instance['display'][$view_mode] ?? [];
  $settings = $display['settings'] ?? [];
  $element = [];
  if (($display['type'] ?? '') === 'acuity_encrypt_summary') {
    // Mirrors core's trim_length setting on "Summary or trimmed".
    $element['trim_length'] = [
      '#type'             => 'textfield',
      '#title'            => t('Trim length'),
      '#description'      => t('Used when the summary is empty: the full text is trimmed to this many characters.'),
      '#default_value'    => $settings['trim_length'] ?? 600,
      '#size'             => 10,
      '#element_validate' => ['element_validate_integer_positive'],
      '#required'         => TRUE,
    ];
  }
  $element['hide_denied'] = [
    '#type'          => 'checkbox',
    '#title'         => t('Hide completely for viewers without the <em>view encrypted fields</em> permission'),
    '#description'   => t('Output nothing at all instead of the "Encrypted" mask — the field, including its label, disappears for unauthorised viewers. In Views, combine with "No results behavior: Hide if empty" so the row leaves no gap.'),
    '#default_value' => !empty($settings['hide_denied']),
  ];
  return $element;
}

/**
 * Implements hook_field_formatter_settings_summary().
 */
function acuity_encrypt_field_formatter_settings_summary(array $field, array $instance, string $view_mode): string {
  $display  = $instance['display'][$view_mode] ?? [];
  $settings = $display['settings'] ?? [];
  $parts = [];
  if (($display['type'] ?? '') === 'acuity_encrypt_summary') {
    $parts[] = t('Trim length: @length', ['@length' => $settings['trim_length'] ?? 600]);
  }
  $parts[] = !empty($settings['hide_denied'])
    ? t('Without permission: hidden entirely')
    : t('Without permission: masked');
  return implode('; ', $parts);
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
 *   Unauthorised users see the static mask.
 *
 * acuity_encrypt_summary: fail-closed replacement for core's "Summary or
 *   trimmed" (text_with_summary only) — decrypted summary if present, else
 *   the decrypted value trimmed to the trim_length setting. Core's own
 *   formatter would render raw ciphertext for unauthorised viewers.
 *
 * For authorised users, hook_field_attach_load() has already decrypted the
 * values and refreshed safe_value, so $items contains plaintext. For
 * unauthorised users (or when the key is missing) $items still holds
 * ciphertext — both formatters detect that and render the static mask
 * rather than leaking raw ciphertext into the page.
 */
function acuity_encrypt_field_formatter_view(string $_entity_type, object $_entity, array $_field, array $_instance, string $_langcode, array &$items, array $display): array {
  $elements    = [];
  $module_path = backdrop_get_path('module', 'acuity_encrypt');

  $can_view = user_access('view encrypted fields');

  // Optional per-display setting: render nothing at all for unauthorised
  // viewers. Returning an empty array makes field_default_view() skip the
  // whole field — wrapper and label included — on node displays and in
  // Views (pair with Views' "No results behavior: Hide if empty").
  if (!$can_view && !empty($display['settings']['hide_denied'])) {
    return [];
  }

  foreach ($items as $delta => $item) {
    $value = isset($item['value']) ? $item['value'] : '';

    // Fail closed: no permission, or the value is still ciphertext (key
    // missing / decrypt failed) — render the static mask, keep the value
    // out of the DOM entirely.
    if (!$can_view || acuity_encrypt_is_encrypted($value)) {
      $elements[$delta] = _acuity_encrypt_masked_element($module_path);
      continue;
    }

    if ($display['type'] === 'acuity_encrypt_plain') {
      $elements[$delta] = [
        '#markup' => !empty($item['safe_value']) ? $item['safe_value'] : check_plain($value),
      ];
      continue;
    }

    // acuity_encrypt_summary — like core's "Summary or trimmed", but the
    // undecrypted cases were already masked above.
    if ($display['type'] === 'acuity_encrypt_summary') {
      $summary = isset($item['summary']) ? $item['summary'] : '';
      if ($summary !== '' && acuity_encrypt_is_encrypted($summary)) {
        // Value decrypted but the summary did not — treat as undecrypted.
        $elements[$delta] = _acuity_encrypt_masked_element($module_path);
      }
      elseif ($summary !== '') {
        $elements[$delta] = [
          '#markup' => !empty($item['safe_summary']) ? $item['safe_summary'] : check_plain($summary),
        ];
      }
      else {
        $output = !empty($item['safe_value']) ? $item['safe_value'] : check_plain($value);
        $trim_length = !empty($display['settings']['trim_length']) ? (int) $display['settings']['trim_length'] : 600;
        $elements[$delta] = [
          '#markup' => text_summary($output, !empty($item['format']) ? $item['format'] : NULL, $trim_length),
        ];
      }
      continue;
    }

    // acuity_encrypt_reveal
    // Honest at-rest indicator: only values the load hook actually
    // decrypted (acuity_encrypt_decrypted item flag) get the mask —
    // masking plaintext as "Encrypted" would give a false assurance.
    // A value still plaintext in the DB (mid-migration) renders as normal
    // visible text, exactly like a plain text field, so unmigrated rows
    // are unmistakable. Only permitted viewers reach this branch; everyone
    // else was masked above.
    $rendered = !empty($item['safe_value']) ? $item['safe_value'] : check_plain($value);

    if (empty($item['acuity_encrypt_decrypted'])) {
      $elements[$delta] = ['#markup' => $rendered];
      continue;
    }

    $elements[$delta] = [
      // The word "Encrypted" IS the toggle: click it to reveal, and it
      // becomes a small "Hide" after the value (the JS restores the
      // original label on hide). Value span precedes the button so the
      // control sits to the right of the revealed text.
      '#markup' => '<div class="acuity-encrypt-wrap">'
        . '<span class="acuity-encrypt-value" style="display:none">'
        . $rendered
        . '</span>'
        . '<button type="button" class="acuity-encrypt-reveal-btn acuity-encrypt-chip" title="'
        . t('Click to reveal') . '">'
        . t('Encrypted') . '</button>'
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

  // Permission gate: decrypt only for users allowed to see plaintext.
  // Everyone else keeps ciphertext in the entity — core formatters render
  // ciphertext, this module's widget/formatters render a mask (fail closed).
  // Safe only because acuity_encrypt_entity_info_alter() disables persistent
  // entity/field caching for encrypted bundles; otherwise this user-dependent
  // result would be cached and served to other users.
  if (!user_access('view encrypted fields')) {
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
          /** @var array $field */
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
              // Mark the item as decrypted-from-ciphertext so the reveal
              // formatter can distinguish it from a value that is still
              // plaintext at rest (mid-migration) and must NOT be masked —
              // masking plaintext would falsely suggest at-rest encryption.
              $item['acuity_encrypt_decrypted'] = TRUE;
              // Recompute safe_value so the text formatter and other consumers
              // get plaintext, not the ciphertext that hook_field_load() saw.
              // Plain fields (no text processing, empty format) get
              // check_plain — mirroring text module's _text_sanitize().
              if (!empty($item['format'])) {
                $item['safe_value'] = check_markup($item['value'], $item['format'], $langcode, FALSE);
              }
              elseif (isset($item['safe_value'])) {
                $item['safe_value'] = check_plain($item['value']);
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
              elseif (isset($item['safe_summary'])) {
                $item['safe_summary'] = check_plain($item['summary']);
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
    /** @var array $field */
    $field = field_info_field($field_name);
    if (!isset($entity->{$field_name})) {
      continue;
    }

    foreach ($entity->{$field_name} as $_langcode => &$lang_items) {
      foreach ($lang_items as &$item) {
        if (!empty($item['value']) && !acuity_encrypt_is_encrypted($item['value'])) {
          $enc = acuity_encrypt_encrypt($item['value']);
          if ($enc !== FALSE) {
            // Fail-closed guard for single-line text: the ciphertext must fit
            // the column. Form submissions are caught earlier by the widget
            // validator; this protects programmatic saves (Feeds, custom
            // code), where the alternatives are a strict-mode SQL error or —
            // worse — silent truncation into undecryptable ciphertext.
            if ($field['type'] === 'text') {
              $max_length = !empty($field['settings']['max_length']) ? (int) $field['settings']['max_length'] : 255;
              if (strlen($enc) > $max_length) {
                watchdog('acuity_encrypt',
                  'Presave blocked: encrypted value for field @field (@len chars) exceeds its column length (@max). Plaintext limit is @limit bytes.',
                  ['@field' => $field_name, '@len' => strlen($enc), '@max' => $max_length, '@limit' => acuity_encrypt_max_plaintext_bytes($max_length)],
                  WATCHDOG_ERROR
                );
                throw new LengthException(format_string(
                  'acuity_encrypt: value for field @field is too long to encrypt within its @max-character column (plaintext limit @limit bytes). Shorten the value or raise the field maximum length.',
                  ['@field' => $field_name, '@max' => $max_length, '@limit' => acuity_encrypt_max_plaintext_bytes($max_length)]
                ));
              }
            }
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
