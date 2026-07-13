<?php

/**
 * @file
 * Hard key rotation for Acuity Encrypt (Phase 2).
 *
 * Re-encrypts every enc{from}: value as enc{to}: using the Batch API.
 * Walks BOTH field_data_* and field_revision_* tables for every field
 * instance using the encrypt widget — a source key can only be retired
 * once its revisions are migrated too.
 *
 * Rows are updated with direct db_update() (not $entity->save()) so entity
 * timestamps, revisions, and hooks are untouched; caches are cleared in the
 * finished callback.
 */

/**
 * Form callback: choose a source slot and start the rotation batch.
 */
function acuity_encrypt_rotate_form(array $form, array &$form_state): array {
  backdrop_set_title(t('Rotate encryption keys'));

  $registry = acuity_encrypt_slots();
  $counts = acuity_encrypt_slot_value_counts();
  $active = acuity_encrypt_active_slot();

  if (_acuity_encrypt_get_key($active) === NULL) {
    $form['error'] = [
      '#markup' => '<p class="messages error">' . t(
        'The active slot (@id) has no key configured — rotation cannot run. Fix the active slot first.',
        ['@id' => $active]
      ) . '</p>',
    ];
    return $form;
  }

  // Source candidates: any non-active slot holding data whose key is present.
  $options = [];
  $blocked = [];
  foreach ($registry['slots'] as $slot_id => $info) {
    $count = $counts['slots'][$slot_id] ?? 0;
    if ($slot_id === $active || $count === 0) {
      continue;
    }
    if (_acuity_encrypt_get_key($slot_id) === NULL) {
      $blocked[] = format_plural($count,
        'Slot @id ("@label") holds 1 encrypted entry but its key is missing — restore the key before it can be rotated.',
        'Slot @id ("@label") holds @count encrypted entries but its key is missing — restore the key before it can be rotated.',
        ['@id' => $slot_id, '@label' => $info['label']]
      );
      continue;
    }
    $options[$slot_id] = format_plural($count,
      'Slot @id ("@label") — 1 encrypted entry',
      'Slot @id ("@label") — @count encrypted entries',
      ['@id' => $slot_id, '@label' => $info['label']]
    );
  }

  foreach ($blocked as $message) {
    $form['blocked_' . md5($message)] = ['#markup' => '<p class="messages error">' . $message . '</p>'];
  }

  if (empty($options)) {
    $form['none'] = [
      '#markup' => '<p>' . t('Nothing to rotate: no non-active slot currently holds encrypted data with a configured key.') . '</p>'
        . '<p>' . l(t('Back to key slots'), 'admin/config/acuity-utils/acuity_encrypt') . '</p>',
    ];
    return $form;
  }

  $form['intro'] = [
    '#markup' => '<p>' . t(
      'Rotation decrypts every value stored under the source slot and re-encrypts it under the ACTIVE slot (@id), including revisions. Nothing is deleted; a value that fails to decrypt is left untouched and logged.',
      ['@id' => $active]
    ) . '</p>',
  ];

  $form['from_slot'] = [
    '#type'     => 'radios',
    '#title'    => t('Rotate data FROM'),
    '#options'  => $options,
    '#required' => TRUE,
  ];

  $form['to_display'] = [
    '#markup' => '<p><strong>' . t('Rotate data TO: slot @id (active)', ['@id' => $active]) . '</strong></p>',
  ];

  $form['confirm_backup'] = [
    '#type'          => 'checkbox',
    '#title'         => t('Both keys are backed up in a password manager. I understand rotation rewrites database values.'),
    '#required'      => TRUE,
  ];

  $form['actions'] = ['#type' => 'actions'];
  $form['actions']['submit'] = [
    '#type'  => 'submit',
    '#value' => t('Start rotation'),
  ];
  $form['actions']['cancel'] = [
    '#markup' => l(t('Cancel'), 'admin/config/acuity-utils/acuity_encrypt'),
  ];

  return $form;
}

/**
 * Submit handler: kick off the rotation batch.
 */
function acuity_encrypt_rotate_form_submit(array $form, array &$form_state): void {
  $from = (int) $form_state['values']['from_slot'];
  $to = acuity_encrypt_active_slot();

  $operations = [];
  foreach (acuity_encrypt_encrypted_field_map() as $field_name => $info) {
    foreach ($info['tables'] as $table) {
      if (db_table_exists($table)) {
        $operations[] = ['acuity_encrypt_rotate_table_op', [$table, $info['columns'], $from, $to]];
      }
    }
  }

  if (empty($operations)) {
    backdrop_set_message(t('No encrypted fields found — nothing to rotate.'), 'warning');
    return;
  }

  watchdog('acuity_encrypt', 'Key rotation started: slot @from → slot @to.', ['@from' => $from, '@to' => $to], WATCHDOG_NOTICE);

  batch_set([
    'title' => t('Rotating encryption keys (slot @from → slot @to)', ['@from' => $from, '@to' => $to]),
    'operations' => $operations,
    'finished' => 'acuity_encrypt_rotate_finished',
    'file' => backdrop_get_path('module', 'acuity_encrypt') . '/acuity_encrypt.rotate.inc',
  ]);

  $form_state['redirect'] = 'admin/config/acuity-utils/acuity_encrypt';
}

/**
 * Batch operation: rotate one field table, 50 rows per pass.
 *
 * Rows that fail to decrypt are skipped permanently (tracked in the sandbox
 * so they cannot cause an infinite loop) and left byte-for-byte untouched.
 */
function acuity_encrypt_rotate_table_op(string $table, array $columns, int $from, int $to, array &$context): void {
  if (!isset($context['sandbox']['skip'])) {
    $context['sandbox']['skip'] = 0;
    $context['results'] += ['updated' => 0, 'failed' => 0, 'from' => $from, 'to' => $to];
  }

  $prefix = 'enc' . $from . ':';
  $is_revision = strpos($table, 'field_revision_') === 0;

  // Row identity columns (the table's primary key).
  $keys = ['entity_type', 'entity_id', 'deleted', 'delta', 'language'];
  if ($is_revision) {
    $keys[] = 'revision_id';
  }

  $or = db_or();
  foreach ($columns as $column) {
    $or->condition($column, db_like($prefix) . '%', 'LIKE');
  }

  $select = db_select($table, 't')->fields('t')->condition($or);
  foreach ($keys as $key) {
    $select->orderBy($key);
  }
  $rows = $select->range($context['sandbox']['skip'], 50)->execute()->fetchAll();

  if (empty($rows)) {
    $context['finished'] = 1;
    return;
  }

  foreach ($rows as $row) {
    $updates = [];
    $row_failed = FALSE;

    foreach ($columns as $column) {
      $value = $row->{$column};
      if ($value === NULL || strpos($value, $prefix) !== 0 || !acuity_encrypt_is_encrypted($value)) {
        continue;
      }
      $plain = acuity_encrypt_decrypt($value);
      if ($plain === FALSE) {
        $row_failed = TRUE;
        continue;
      }
      $new = acuity_encrypt_encrypt($plain, $to);
      if ($new === FALSE) {
        $row_failed = TRUE;
        continue;
      }
      $updates[$column] = $new;
    }

    if (!empty($updates)) {
      $update = db_update($table)->fields($updates);
      foreach ($keys as $key) {
        $update->condition($key, $row->{$key});
      }
      // Guard against concurrent edits: only rewrite the exact value we read.
      foreach ($updates as $column => $_new) {
        $update->condition($column, $row->{$column});
      }
      $updated = $update->execute();
      $context['results']['updated'] += $updated ? count($updates) : 0;
      if (!$updated) {
        // Row changed underneath us; it will be re-selected next pass.
        continue;
      }
      if ($row_failed) {
        // One column rotated, another failed — the failed column keeps the
        // old prefix, but the row no longer fully matches; count + skip it.
        $context['results']['failed']++;
        $context['sandbox']['skip']++;
      }
    }
    else {
      // Nothing rotated (decrypt failure or prefix false-positive): skip the
      // row permanently so the LIKE query cannot loop on it forever.
      if ($row_failed) {
        $context['results']['failed']++;
        watchdog('acuity_encrypt', 'Rotation: a value in @table could not be decrypted with slot @from — left untouched.', [
          '@table' => $table, '@from' => $from,
        ], WATCHDOG_ERROR);
      }
      $context['sandbox']['skip']++;
    }
  }

  $context['finished'] = 0.5;
  $context['message'] = t('@table: @count value(s) rotated so far…', ['@table' => $table, '@count' => $context['results']['updated']]);
}

/**
 * Batch finished callback.
 */
function acuity_encrypt_rotate_finished(bool $success, array $results, array $_operations): void {
  // Field values changed under the entity system's feet — flush field and
  // entity caches so stale plaintext/ciphertext is never served.
  field_cache_clear();
  foreach (array_keys(entity_get_info()) as $entity_type) {
    if (db_table_exists('cache_entity_' . $entity_type)) {
      cache_clear_all('*', 'cache_entity_' . $entity_type, TRUE);
    }
  }

  $from = $results['from'] ?? 0;
  $to = $results['to'] ?? 0;
  $updated = $results['updated'] ?? 0;
  $failed = $results['failed'] ?? 0;

  // Recount — also refreshes the missing-key/orphan state flags.
  $counts = acuity_encrypt_slot_value_counts();
  $remaining = $counts['slots'][$from] ?? 0;

  watchdog('acuity_encrypt', 'Key rotation finished: @updated value(s) moved from slot @from to slot @to, @failed failure(s), @remaining remaining on source.', [
    '@updated' => $updated, '@from' => $from, '@to' => $to, '@failed' => $failed, '@remaining' => $remaining,
  ], $failed ? WATCHDOG_WARNING : WATCHDOG_NOTICE);

  if (!$success) {
    backdrop_set_message(t('Rotation ended abnormally — re-run it to continue. Values already rotated stay rotated; nothing is ever double-encrypted.'), 'error');
    return;
  }

  backdrop_set_message(t('Rotation complete: @updated value(s) re-encrypted from slot @from to slot @to.', [
    '@updated' => $updated, '@from' => $from, '@to' => $to,
  ]));

  if ($failed > 0) {
    backdrop_set_message(t('@failed value(s) could not be decrypted and were left untouched — see the log. They remain on slot @from.', [
      '@failed' => $failed, '@from' => $from,
    ]), 'warning');
  }

  if ($remaining === 0) {
    backdrop_set_message(t('Slot @from now holds no encrypted data — it is Available. You can safely retire its key (e.g. remove acuity_encrypt_key@from from settings.php) after confirming your backups.', [
      '@from' => $from,
    ]));
  }
}
