<?php

/**
 * @file
 * VBO action: encrypt existing plaintext values in fields marked for encryption.
 *
 * When "Encrypt this field" is enabled on an existing field that already has
 * data, the values in the database are still plaintext. Resaving each entity
 * triggers hook_field_attach_presave() which encrypts any unencrypted values.
 *
 * This action does exactly that — resaves the entity — giving site builders a
 * convenient bulk operation to encrypt all existing data after enabling field
 * encryption.
 *
 * Usage:
 *   1. Enable "Encrypt this field" on one or more field instances.
 *   2. Create a View of the relevant content type with the VBO field.
 *   3. Select all records, run "Acuity Encrypt: encrypt existing field values".
 *   4. Each record is resaved — plaintext values are encrypted automatically.
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

  entity_save($entity_type, $entity);
}
