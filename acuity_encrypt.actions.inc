<?php

/**
 * @file
 * VBO actions: bulk encrypt in place, and copy values between text fields.
 *
 * ENCRYPT ACTION
 * When the encrypt widget is enabled on an existing field that already has
 * data, the values in the database are still plaintext. Resaving each entity
 * triggers hook_field_attach_presave() which encrypts any unencrypted values.
 * The encrypt action does exactly that — resaves the entity — giving site
 * builders a convenient bulk operation to encrypt all existing data after
 * enabling field encryption.
 *
 * Usage:
 *   1. Enable the acuity_encrypt_widget on one or more field instances.
 *   2. Create a View of the relevant content type with the VBO field.
 *   3. Select all records, run "Acuity Encrypt: encrypt existing field values".
 *   4. Each record is resaved — plaintext values are encrypted automatically.
 *
 * COPY ACTION
 * Configurable source field → destination field copy, intended as a safety
 * net around encryption migrations. Typical flow:
 *   1. Copy field_password → field_password_saved (plaintext backup).
 *   2. Switch field_password to the encrypt widget, run the encrypt action.
 *   3. Verify decryption works, then delete the backup field.
 * Because source and destination are both selectable, the same action also
 * restores in the other direction (field_password_saved → field_password).
 * The copy itself is plaintext-in-memory; whether either side ends up
 * encrypted at rest is decided per-field by hook_field_attach_presave() on
 * save, exactly as with any other write.
 *
 * Note: resaving updates the entity's changed timestamp (e.g. node.changed).
 */

// ── Hook implementation ───────────────────────────────────────────────────────

/**
 * Implements hook_action_info().
 */
function acuity_encrypt_action_info(): array {
  return [
    'acuity_encrypt_encrypt_fields_action' => [
      'label'        => t('Acuity Encrypt: encrypt existing field values'),
      'type'         => 'entity',
      'configurable' => FALSE,
      'triggers'     => ['any'],
    ],
    'acuity_encrypt_copy_field_action' => [
      'label'        => t('Acuity Encrypt: copy field value to another field'),
      'type'         => 'entity',
      'configurable' => TRUE,
      'triggers'     => ['any'],
    ],
  ];
}

// ── Action callback ───────────────────────────────────────────────────────────

/**
 * Resaves the entity so hook_field_attach_presave() encrypts any plaintext
 * values in fields that have "Encrypt this field" enabled.
 *
 * Already-encrypted values are skipped automatically (enc\d+: prefix check).
 *
 * @param object $entity
 *   The entity being processed by VBO.
 * @param array  $context
 *   VBO context. 'entity_type' is read from here.
 */
function acuity_encrypt_encrypt_fields_action(object $entity, array $context): void {
  $entity_type = $context['entity_type'] ?? 'node';

  if (!acuity_encrypt_key_available()) {
    list($entity_id) = entity_extract_ids($entity_type, $entity);
    watchdog('acuity_encrypt',
      'VBO encrypt action skipped for @type !id: no encryption key configured.',
      ['@type' => $entity_type, '!id' => $entity_id],
      WATCHDOG_WARNING
    );
    return;
  }

  $entity->save();
}

// ── Copy action: configuration form ──────────────────────────────────────────

/**
 * Configuration form for the copy field action (VBO configurable action).
 *
 * Presents source and destination field selects plus an overwrite checkbox.
 * Field type is shown in each option label because source and destination
 * must share the same type.
 *
 * The selects are narrowed to fields that exist on the bundles of the rows
 * actually selected in the view (one DISTINCT query on the base table), so a
 * view filtered to e.g. "Content: Type = Asset - Emails" only offers that
 * content type's fields. Falls back to all fields on the entity type, then
 * all text fields site-wide, when the selection or entity info is
 * unavailable. With "select all pages" the sample is the current page's
 * rows — for a bundle-filtered view that is the full bundle set, and the
 * per-entity instance check in the action skips any mismatch regardless.
 *
 * @param array $context
 *   VBO action context: entity_type, view, settings.
 * @param array $form_state
 *   The VBO multistep form state; 'selection' holds the chosen entity IDs.
 *
 * @return array
 *   The configuration form.
 */
function acuity_encrypt_copy_field_action_form(array $context, array $form_state = []): array {
  $entity_type = $context['entity_type'] ?? NULL;
  $bundles     = [];
  if ($entity_type !== NULL && !empty($form_state['selection'])) {
    $bundles = _acuity_encrypt_selection_bundles($entity_type, $form_state['selection']);
  }
  $options = _acuity_encrypt_copy_field_options($entity_type, $bundles);

  $form['source_field'] = [
    '#type'          => 'select',
    '#title'         => t('Source field'),
    '#options'       => $options,
    '#required'      => TRUE,
    '#description'   => t('Field to copy values from. Only fields that exist on the selected items are listed. Encrypted values are decrypted on load (you need the <em>view encrypted fields</em> permission), so the destination receives plaintext.'),
  ];
  $form['destination_field'] = [
    '#type'          => 'select',
    '#title'         => t('Destination field'),
    '#options'       => $options,
    '#required'      => TRUE,
    '#description'   => t('Field to copy values into. Must be the same field type as the source. If this field uses the Acuity Encrypt widget, the copied value is encrypted when the entity is saved.'),
  ];
  $form['overwrite'] = [
    '#type'          => 'checkbox',
    '#title'         => t('Overwrite existing destination values'),
    '#default_value' => FALSE,
    '#description'   => t('If unticked, entities whose destination field already holds a value are skipped (safe default for taking backups).'),
  ];

  return $form;
}

/**
 * Validation handler for the copy field action configuration form.
 */
function acuity_encrypt_copy_field_action_validate(array $form, array $form_state): void {
  $source = $form_state['values']['source_field'];
  $dest   = $form_state['values']['destination_field'];

  if ($source === $dest) {
    form_set_error('destination_field', t('Source and destination must be different fields.'));
    return;
  }

  /** @var array $map */
  $map = field_info_field_map();
  if (isset($map[$source], $map[$dest]) && $map[$source]['type'] !== $map[$dest]['type']) {
    form_set_error('destination_field', t('Source (@source_type) and destination (@dest_type) must be the same field type — create the backup field with the same type as the original.', [
      '@source_type' => $map[$source]['type'],
      '@dest_type'   => $map[$dest]['type'],
    ]));
  }
}

/**
 * Submit handler for the copy field action configuration form.
 *
 * @return array
 *   Settings merged into $context for each action callback invocation.
 */
function acuity_encrypt_copy_field_action_submit(array $form, array $form_state): array {
  return [
    'source_field'      => $form_state['values']['source_field'],
    'destination_field' => $form_state['values']['destination_field'],
    'overwrite'         => (bool) $form_state['values']['overwrite'],
  ];
}

/**
 * Builds the select options list of text-family fields.
 *
 * Narrowest available scope wins: fields with an instance on one of the
 * given bundles, else fields with an instance anywhere on the entity type,
 * else every text-family field on the site.
 *
 * @param string|null $entity_type
 *   Entity type to restrict to, or NULL for site-wide.
 * @param array $bundles
 *   Bundle names to restrict to within $entity_type; empty for no bundle
 *   restriction.
 *
 * @return array
 *   field_name => "field_name (type)" options array, sorted by field name.
 */
function _acuity_encrypt_copy_field_options(?string $entity_type = NULL, array $bundles = []): array {
  $options = [];
  /** @var array $map */
  $map = field_info_field_map();
  foreach ($map as $field_name => $info) {
    if (!in_array($info['type'], ['text', 'text_long', 'text_with_summary'], TRUE)) {
      continue;
    }
    if ($entity_type !== NULL) {
      if (empty($info['bundles'][$entity_type])) {
        continue;
      }
      if ($bundles && !array_intersect($bundles, $info['bundles'][$entity_type])) {
        continue;
      }
    }
    $options[$field_name] = $field_name . ' (' . $info['type'] . ')';
  }
  ksort($options);
  return $options;
}

/**
 * Determines the distinct bundles of the entities selected in the view.
 *
 * One query on the entity's base table — the entities themselves are not
 * loaded (the selection can be large).
 *
 * @param string $entity_type
 *   The entity type of the VBO selection.
 * @param array $selection
 *   VBO selection: row index => entity ID.
 *
 * @return array
 *   Distinct bundle names, or empty if they could not be determined (caller
 *   falls back to the entity-type-wide field list).
 */
function _acuity_encrypt_selection_bundles(string $entity_type, array $selection): array {
  /** @var array $info */
  $info = entity_get_info($entity_type);
  if (empty($info['entity keys']['bundle'])) {
    // Bundle-less entity types (e.g. user): the bundle IS the entity type.
    return [$entity_type];
  }
  $ids = array_filter(array_values($selection), 'is_numeric');
  if (!$ids || empty($info['base table']) || empty($info['entity keys']['id'])) {
    return [];
  }
  return db_select($info['base table'], 'b')
    ->fields('b', [$info['entity keys']['bundle']])
    ->condition($info['entity keys']['id'], $ids, 'IN')
    ->distinct()
    ->execute()
    ->fetchCol();
}

// ── Copy action: per-entity callback ──────────────────────────────────────────

/**
 * Copies one text field's values into another field and resaves the entity.
 *
 * Safety-net companion to the encrypt action: back up a plaintext field
 * before switching it to the encrypt widget, or restore from the backup by
 * swapping source and destination. Skips (with a watchdog entry) rather than
 * doing anything destructive when:
 *   - either field has no instance on the entity's bundle,
 *   - the destination already has a value and overwrite was not ticked,
 *   - a source value is still ciphertext (the acting user lacks
 *     'view encrypted fields' or the decrypt failed) — copying ciphertext
 *     would silently produce a useless backup,
 *   - the value will not fit the destination column (single-line text,
 *     allowing for encryption overhead when the destination is encrypted).
 *
 * Only the storage columns (value, summary, format) are copied; deltas beyond
 * the destination's cardinality are dropped with a warning.
 *
 * @param object $entity
 *   The entity being processed by VBO.
 * @param array  $context
 *   VBO context: entity_type plus source_field / destination_field /
 *   overwrite from the configuration form.
 */
function acuity_encrypt_copy_field_action(object $entity, array $context): void {
  $entity_type = $context['entity_type'] ?? 'node';
  $source      = $context['source_field'];
  $dest        = $context['destination_field'];
  $overwrite   = !empty($context['overwrite']);

  list($entity_id, , $bundle) = entity_extract_ids($entity_type, $entity);
  $log_args = ['@type' => $entity_type, '!id' => $entity_id, '@source' => $source, '@dest' => $dest];

  // Both fields must have an instance on this entity's bundle.
  if (!field_info_instance($entity_type, $source, $bundle) || !field_info_instance($entity_type, $dest, $bundle)) {
    watchdog('acuity_encrypt',
      'Copy skipped for @type !id: @source and/or @dest is not attached to bundle %bundle.',
      $log_args + ['%bundle' => $bundle], WATCHDOG_NOTICE);
    return;
  }

  // Nothing to copy.
  if (empty($entity->{$source}) || !_acuity_encrypt_field_has_value($entity, $source)) {
    return;
  }

  // Safe default: never clobber an existing destination value.
  if (!$overwrite && _acuity_encrypt_field_has_value($entity, $dest)) {
    watchdog('acuity_encrypt',
      'Copy skipped for @type !id: @dest already has a value and overwrite is off.',
      $log_args, WATCHDOG_NOTICE);
    return;
  }

  /** @var array $dest_field */
  $dest_field = field_info_field($dest);
  /** @var array $dest_instance */
  $dest_instance = field_info_instance($entity_type, $dest, $bundle);
  $dest_encrypts = $dest_instance['widget']['type'] === 'acuity_encrypt_widget';

  // Column-length guard for single-line destinations. When the destination
  // encrypts on save, the LIMIT is the plaintext budget that leaves room for
  // the enc{slot}: envelope — pre-checking here keeps presave's
  // LengthException from aborting the whole VBO batch mid-run.
  $max_bytes = NULL;
  if ($dest_field['type'] === 'text') {
    $max_length = !empty($dest_field['settings']['max_length']) ? (int) $dest_field['settings']['max_length'] : 255;
    $max_bytes  = $dest_encrypts ? acuity_encrypt_max_plaintext_bytes($max_length) : $max_length;
  }

  $copied = [];
  foreach ($entity->{$source} as $langcode => $lang_items) {
    foreach ($lang_items as $delta => $item) {
      // Refuse to copy ciphertext: it means this user could not decrypt the
      // source (no permission, missing key, or corrupt value). A ciphertext
      // "backup" is worthless once keys rotate — skip the whole entity.
      foreach (['value', 'summary'] as $column) {
        if (!empty($item[$column]) && acuity_encrypt_is_encrypted($item[$column])) {
          watchdog('acuity_encrypt',
            'Copy skipped for @type !id: @source still holds ciphertext (missing %perm permission, or decrypt failed). Nothing was copied.',
            $log_args + ['%perm' => 'view encrypted fields'], WATCHDOG_WARNING);
          return;
        }
      }

      if ($max_bytes !== NULL && isset($item['value']) && strlen($item['value']) > $max_bytes) {
        watchdog('acuity_encrypt',
          'Copy skipped for @type !id: @source value (@len bytes) exceeds the @dest limit of @max bytes.',
          $log_args + ['@len' => strlen($item['value']), '@max' => $max_bytes], WATCHDOG_WARNING);
        return;
      }

      // Copy storage columns only — safe_value/safe_summary are recomputed
      // from these on the next load.
      $copy = ['value' => $item['value'] ?? ''];
      if (array_key_exists('format', $item)) {
        $copy['format'] = $item['format'];
      }
      if ($dest_field['type'] === 'text_with_summary') {
        $copy['summary'] = $item['summary'] ?? '';
      }
      $copied[$langcode][$delta] = $copy;
    }

    // Respect the destination's cardinality.
    if (isset($copied[$langcode])
        && $dest_field['cardinality'] != FIELD_CARDINALITY_UNLIMITED
        && count($copied[$langcode]) > $dest_field['cardinality']) {
      $copied[$langcode] = array_slice($copied[$langcode], 0, $dest_field['cardinality']);
      watchdog('acuity_encrypt',
        'Copy for @type !id: @source has more values than @dest allows — extra values were dropped.',
        $log_args, WATCHDOG_WARNING);
    }
  }

  $entity->{$dest} = $copied;
  $entity->save();
}

/**
 * Determines whether a field on an entity holds at least one non-empty value.
 *
 * @param object $entity
 *   The entity to inspect.
 * @param string $field_name
 *   The field to check.
 *
 * @return bool
 *   TRUE if any delta in any language has a non-empty 'value'.
 */
function _acuity_encrypt_field_has_value(object $entity, string $field_name): bool {
  if (empty($entity->{$field_name})) {
    return FALSE;
  }
  foreach ($entity->{$field_name} as $lang_items) {
    foreach ($lang_items as $item) {
      if (isset($item['value']) && $item['value'] !== '') {
        return TRUE;
      }
    }
  }
  return FALSE;
}
