# Acuity Encrypt — Module Notes

## Purpose

AES-256-GCM field encryption for Backdrop CMS with no external library
dependencies. Two roles:

1. **Utility API** — `acuity_encrypt_encrypt()` / `acuity_encrypt_decrypt()` for
   use by other modules (`acuity_secure_message`, future modules).
2. **Encrypt-as-behaviour** — a custom widget (`acuity_encrypt_widget`) that can
   be selected on any standard `text`, `text_long`, or `text_with_summary` field
   instance. No new field type; no data migration required to enable encryption
   on an existing field.

---

## Architecture decision: widget approach, not a custom field type

The original design used a custom `acuity_encrypted_text` field type. This was
replaced with an encrypt-as-behaviour approach for two reasons:

1. Site builders can enable encryption on an **existing** text field by switching
   the widget in Manage Fields — no separate field to add, no migration needed.
2. A single widget covers `text`, `text_long`, and `text_with_summary` (including
   WYSIWYG textarea) without needing separate field types for each.

The trade-off: encryption is implicit in the widget selection rather than explicit
in the field type. Switching a field away from the acuity_encrypt_widget leaves
ciphertext in the DB — a warning is shown in the Field UI settings.

---

## File map

```
acuity_encrypt.module      Public API + hooks (menu, permission, config_info)
acuity_encrypt.field.inc   Widget, formatters, load/presave hooks (loaded at boot)
acuity_encrypt.admin.inc   Admin settings page — key tabs, accordion, generate key
acuity_encrypt.actions.inc VBO action: bulk encrypt existing field values
acuity_encrypt.install     hook_install / hook_uninstall (CMI lifecycle)
config/acuity_encrypt.settings.json  CMI config: storage method + external path
js/acuity_encrypt.js       Reveal toggles: display formatter + edit widget
js/acuity_encrypt.admin.js Generate key button, copy-to-clipboard
css/acuity_encrypt.css     Masked/revealed styles, WYSIWYG overlay, admin styles
```

All three .inc files are loaded unconditionally via `module_load_include()` at
the top of `.module` so all hook implementations are always discoverable.

---

## Encryption details

- Algorithm: AES-256-GCM via `openssl_encrypt()`.
- Key: 32 bytes derived from the configured passphrase via `hash('sha256', ..., TRUE)`.
- IV: 12 bytes from `random_bytes()` per encryption — stored inline.
- Tag: 16 bytes GCM authentication tag — stored inline, verified on decrypt.
- Wire format: `enc{slot}:base64( iv[12] . tag[16] . ciphertext )`, e.g. `enc1:AAECAwQ…`
- The `enc\d+:` prefix lets `acuity_encrypt_is_encrypted()` distinguish ciphertext
  from plaintext and prevents double-encryption on re-save.

---

## Key management

### Storage priority (highest first)

1. `$settings['acuity_encrypt_key1']` in `settings.php` — always checked first;
   never in DB or config exports.
2. External file — absolute path configured in admin settings.
3. Private file — `private://keys/acuity_encrypt_key1.key`, written via admin UI.

Keys are **never stored in CMI config or the database**.

### Key naming convention

```
settings.php:  $settings['acuity_encrypt_key1']
Private file:  private://keys/acuity_encrypt_key1.key
External file: /your/path/acuity_encrypt_key1.key
```

Slot number is embedded in every key name for consistency across all methods.

### Admin UI for keys

Two local task tabs at `admin/config/acuity-utils/acuity_encrypt`:

- **View keys** (default tab) — table of key slots with storage method, key
  identifier, and status (Active / Key missing). Only Slot 1 shown in v1.
- **Set keys** — accordion settings form with three collapsed sections
  (settings.php / private file / external file). Each section has its own
  submit handler. Generate Key button uses `window.crypto.getRandomValues()`
  client-side; settings.php mode outputs a full `$settings[...]` line to copy.

---

## Field widget: acuity_encrypt_widget

Registered via `hook_field_widget_info()`. Targets `text`, `text_long`,
`text_with_summary`. Site builders select it in Manage Fields → Widget column.

### Selecting the widget = enabling encryption for that field instance

No separate checkbox needed. The widget selection IS the signal to
`hook_field_attach_load()` and `hook_field_attach_presave()`.

### Edit form behaviour

**Single-line text (`text`):**
- Real `<input>` hidden with `display:none` (still submits).
- Mask row: fake readonly `<input>` showing `••••••••` + "Reveal to edit" button.
- JS (`data-mode="inline"`): clicking reveal hides the mask row and shows the
  real input, focusing it.

**Long text / WYSIWYG (`text_long`, `text_with_summary`):**
- Element type is `text_format` (Backdrop compound element — wires up CKEditor).
- CKEditor initialises normally in the DOM under a `position:absolute` overlay.
- Overlay shows `••••••••` + "Reveal to edit" button on top of the editor.
- JS (`data-mode="overlay"`): clicking reveal hides only the overlay div;
  CKEditor is already initialised and ready.

**Unauthorised users (no `view encrypted fields`) with existing data:**
- `#type => 'value'` elements carry `value` (and `format` for long text)
  through the form submission without appearing in the HTML DOM.
- Visible locked `••••••••` placeholder shown instead.
- Presave re-encrypts automatically.

**Empty fields (any user):**
- Widget rendered normally (nothing to mask yet).

### Why display:none works for inline but not WYSIWYG

CKEditor 4 (used in Backdrop) uses the textarea's rendered dimensions for layout.
A hidden textarea has zero dimensions. Rather than trying to force CKEditor to
re-flow after reveal, the overlay approach keeps CKEditor visible in normal flow
so it initialises correctly, then the opaque overlay sits on top.

---

## Display formatters

Registered via `hook_field_formatter_info()`. Site builders select in Manage
Display independently of the widget.

### acuity_encrypt_reveal (recommended for encrypted fields)

- Authorised users: `••••••••` + Reveal button. Clicking toggles visibility of
  the decrypted value hidden in a `<span class="acuity-encrypt-value">`. No page
  reload. JS handles both show and re-hide (button label toggles Reveal/Hide).
- Unauthorised users: `••••••••` only (no button, no value in DOM).

### acuity_encrypt_plain

- Renders the decrypted value directly using `$item['safe_value']` (which
  has been refreshed by the load hook to contain plaintext).

---

## Hook flow

```
Entity load
  text_field_load()                      ← sets safe_value from CIPHERTEXT (wrong)
  acuity_encrypt_field_attach_load()     ← decrypts value, refreshes safe_value ✓

Entity save
  text_field_presave()                   ← text processing (filters etc.)
  acuity_encrypt_field_attach_presave()  ← encrypts plaintext value
```

### The safe_value problem (solved)

Backdrop's text module computes `safe_value = check_markup(value, format)` during
`hook_field_load()`, BEFORE our attach hook runs. If we only decrypted `value`,
the standard text formatter would still render the ciphertext via `safe_value`.

Fix: after decrypting `value`, we recompute `safe_value` in `hook_field_attach_load()`:
```php
$item['safe_value'] = check_markup($item['value'], $item['format'], $langcode, FALSE);
```
This means the standard text formatter, Views, tokens, and anything else that
reads `safe_value` all get the correct plaintext.

---

## VBO bulk encryption action

`acuity_encrypt.actions.inc` registers `acuity_encrypt_encrypt_fields_action`.

- **Non-configurable** (no config form needed). Just resaves each entity.
- `hook_field_attach_presave()` encrypts any unencrypted values on save.
- Double-encryption guard: `acuity_encrypt_is_encrypted()` check prevents
  re-encrypting already-encrypted values.
- Use case: you've enabled the widget on a field that already has plain text
  data. VBO bulk-resaves every record; the presave hook encrypts each one.
- Note: resaving updates the entity's `changed` timestamp.

---

## Permissions

- `administer acuity_encrypt` — key settings page (`restrict access: TRUE`).
- `view encrypted fields` — decrypt display + widget edit (`restrict access: TRUE`).

Both default to no roles. Admin must grant explicitly.

---

## Admin paths

```
admin/config/acuity-utils/acuity_encrypt           → View keys tab (slot table)
admin/config/acuity-utils/acuity_encrypt/keys      → MENU_DEFAULT_LOCAL_TASK
admin/config/acuity-utils/acuity_encrypt/settings  → Set keys tab (accordion form)
```

---

## Dependencies

- PHP `openssl` extension (standard in most PHP installs).
- No Backdrop contrib dependencies.
- Views Bulk Operations (optional, for bulk encryption action).

---

## Used by

- `acuity_secure_message` (planned) — calls `acuity_encrypt_encrypt()` /
  `acuity_encrypt_decrypt()` to store message body encrypted at rest.

---

## CURRENT STATE

Status: **architecture complete, partially tested on dev**.

- Widget appears in Field UI widget dropdown ✓
- Display formatters registered ✓
- `safe_value` fix in load hook ✓
- WYSIWYG overlay mask ✓
- Key admin UI (tabs, accordion, generate key) ✓

Still needs:
- Full round-trip test: create node → view (masked display) → edit (masked widget) → reveal → save → re-view.
- Test WYSIWYG: long text field with CKEditor — reveal overlay, edit, save, re-view.
- Test unauthorised user: login as user without `view encrypted fields`, confirm locked placeholder and value preserved on save.
- Test empty field: confirm plain widget shown, value encrypted on first save.
- Test VBO bulk action: populate plain text, enable widget, run VBO, confirm ciphertext in DB.
- Test all three key storage methods.
- Test key reveal panel on Set Keys tab.
- Test Generate Key button (all three modes).

---

## PLANNED / NEXT

See TODO.md in this directory for the full prioritised list.

Short-term:
- Complete dev testing (see checklist above).
- Build `acuity_secure_message` module.

Phase 2 (do not implement until core is stable and tested):
- Key slot management UI: multiple slots, status (Active/In-use/Available/Missing).
- Hard rotation batch: re-encrypt all enc1: values to enc2: slot.
- Per-field "last encrypted" timestamp for audit trail.


==================================================
PHASE 2 — KEY SLOT MANAGEMENT
==================================================

Design agreed in session 2026-07-02. Do not implement until acuity_encrypt
core and acuity_secure_message are stable and tested.

SLOT STATUSES
─────────────
  Active       Default slot — all new encryptions use this. Only one Active
               slot at a time.
  In-use       Has encrypted data in the DB but is not the current default.
               Key MUST remain configured or those records become unreadable.
  Available    Key is configured but zero DB values use this slot. Safe to
               retire the key or reuse the slot.
  Missing key  ⚠ Danger — data exists encrypted with this slot but no key
               is configured. Admin must restore the key immediately.

ROTATION FLOW
─────────────
  1. Admin adds Slot 2 with a new key (status: Available).
  2. Admin sets Slot 2 as Active — all new encryptions write enc2:.
     Slot 1 becomes In-use (enc1: data still exists).
  3. Admin triggers "Hard Rotate Slot 1 → Slot 2":
       - Batch walks every acuity_encrypt_widget field on every entity type.
       - Decrypts enc1: values with Slot 1 key.
       - Re-encrypts as enc2: with Slot 2 key.
       - Progress bar + count shown in admin UI.
  4. Rotation completes:
       - Slot 1 → Available (zero enc1: records remain).
       - Slot 2 → Active (all records now enc2:).
  5. Admin told: "Slot 1 is now Available. You can safely remove
     acuity_encrypt_key1 from settings.php."

WHAT TO BUILD
─────────────
  - config/acuity_encrypt.slots.json  — slot registry (id, label, storage
    method, storage path/key_name, status, value_count).
  - Admin page: key slots list with status column and value counts.
  - "Add slot" form (same storage options as current settings page).
  - "Hard Rotate" batch: Drush command + admin Batch API UI.
  - "Missing key" warning shown prominently on any page if detected.
  - Slot value counts maintained on field presave/delete, OR recount batch.
    Recount always runs on rotation completion.
