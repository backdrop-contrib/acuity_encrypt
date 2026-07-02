# Acuity Encrypt — Changelog

All notable changes to this module are documented here.
Format loosely follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

---

## 1.x-0.2.0 (unreleased)

### Security fixes (2026-07-02 audit)

- `acuity_encrypt_is_encrypted()` now validates that the value after the
  `enc{digits}:` prefix is valid base64 decoding to at least 28 bytes (IV+tag),
  not just a prefix match. Previously, plaintext a user typed that happened to
  start with `enc1:` (etc.) was mistaken for existing ciphertext by the presave
  double-encryption guard and stored unencrypted.
- Saving a new key on the "Private files" admin tab now requires an explicit
  confirmation checkbox if it would overwrite a key that's already in use —
  previously a new key silently replaced the old one, permanently orphaning
  all data encrypted under it (no rotation/re-encryption exists yet).
- The active encryption key is no longer embedded in the "Set keys" page's
  HTML (masked only by CSS). It's now fetched via a CSRF-token-protected AJAX
  endpoint (`admin/config/acuity-utils/acuity_encrypt/reveal-key`, gated on
  `administer acuity_encrypt`) only when "Reveal key" is clicked.
- External file key paths are now hard-rejected at validation time if they
  resolve inside the webroot, instead of only showing a warning after saving.

Complete architectural pivot: encrypt-as-behaviour via a custom widget on
standard Backdrop text field types, replacing the original custom field type.

### Architecture

- Removed `acuity_encrypted_text` custom field type, widget, and formatters.
- Added `acuity_encrypt_widget` — a custom widget targeting the standard
  `text`, `text_long`, and `text_with_summary` field types. Site builders
  select it in Manage Fields → Widget column; no new field or data migration
  needed to encrypt an existing field.
- Encryption signal is the widget selection. `hook_field_attach_load()` and
  `hook_field_attach_presave()` check `$instance['widget']['type']` rather than
  a custom instance setting.

### Widget: single-line text

- Renders a `<input type="text">` hidden with `display:none` (always submits).
- Shows a fake readonly mask input (`••••••••`) and a "Reveal to edit" button.
- JS `data-mode="inline"`: reveal hides the mask row and shows/focuses the
  real input.

### Widget: long text / WYSIWYG

- Uses `#type => 'text_format'` (the Backdrop compound element that wires up
  CKEditor/WYSIWYG) as the top-level element, matching what the core text widget
  does. Site builders get the same rich text editor they are used to.
- CKEditor initialises in normal flow so it has proper dimensions.
- An absolutely-positioned overlay div (`acuity-wysiwyg-mask`) sits on top,
  showing `••••••••` + "Reveal to edit". Clicking hides the overlay only; the
  already-initialised editor is immediately usable.
- JS `data-mode="overlay"`: reveal hides `.acuity-wysiwyg-mask`.

### Widget: unauthorised users

- For users without `view encrypted fields`, field value and format are
  carried through the form as `#type => 'value'` elements (no HTML rendered,
  no DOM exposure). A visible locked `••••••••` placeholder is shown instead.
- `hook_field_attach_presave()` re-encrypts the preserved value on save.

### Display formatters

- `acuity_encrypt_reveal` — `••••••••` + Reveal/Hide toggle button for
  authorised users. Unauthorised users see `••••••••` with no button and no
  value in the DOM.
- `acuity_encrypt_plain` — decrypted value rendered directly via `safe_value`.
- Both formatters target `text`, `text_long`, and `text_with_summary`.

### safe_value fix

- Backdrop's `text_field_load()` runs before `hook_field_attach_load()` and
  sets `$item['safe_value'] = check_markup(ciphertext, format)`. The standard
  text formatter uses `safe_value`, so without a fix it renders ciphertext.
- Fix: after decrypting `value`, `hook_field_attach_load()` recomputes
  `safe_value` (and `safe_summary` for `text_with_summary`) via `check_markup()`.
  This corrects the output for all consumers of `safe_value` including the
  standard text formatter, Views, and tokens.

### VBO bulk encryption action

- Simplified to a non-configurable action: just resaves each entity.
- `hook_field_attach_presave()` handles all encryption automatically on save.
- No source/destination field configuration needed — the widget selection on
  the field instance is the only required setup.

### Admin UI

- Two local task tabs at `admin/config/acuity-utils/acuity_encrypt`:
  - **View keys** (default): slot table with storage method, identifier,
    and Active/Key missing status. Only Slot 1 in v1; structured for Phase 2.
  - **Set keys**: three collapsible fieldset sections (all collapsed by
    default) — settings.php, private file, external file. Each section has its
    own submit handler with `#limit_validation_errors` so the others don't block.
- **Generate Key** button in each section: uses `window.crypto.getRandomValues()`
  client-side; settings.php mode outputs a complete `$settings[...]` line;
  other modes output the raw hex key. Copy-to-clipboard via Clipboard API with
  `execCommand` fallback.
- Key reveal panel on "View keys" tab: shows current active key behind a
  Reveal button with instruction to save to a password manager.

### Bug fixes

- `hook_form_field_ui_field_edit_form_alter()`: Backdrop stores field and
  instance on `$form['#field']` / `$form['#instance']`, not in `$form_state`.
  Previous code used wrong keys and silently failed.
- `hook_field_widget_form_alter()` removed (replaced by proper widget).
- `hook_field_info_alter()` removed (no longer needed).

---

## 1.x-0.1.0 (2026-06-04)

Initial release.

### Encryption core

- AES-256-GCM encryption via PHP `openssl_encrypt()` — zero external dependencies.
- 12-byte random IV and 16-byte GCM authentication tag stored inline per value.
- Ciphertext wire format: `enc{slot}:base64(iv[12] . tag[16] . ciphertext)`.
  Slot prefix enables future key rotation without re-encrypting existing data.
- `acuity_encrypt_encrypt(string $plaintext, int $slot = 1): string|false`
- `acuity_encrypt_decrypt(string $ciphertext): string|false`
- `acuity_encrypt_is_encrypted(string $value): bool`
- `acuity_encrypt_get_slot(string $ciphertext): int|null`
- `acuity_encrypt_key_available(): bool`

### Key management

- Three storage methods with inline step-by-step instructions on the admin page.
  1. `settings.php` — `$settings['acuity_encrypt_key1']` (recommended).
  2. Private filesystem — `private://keys/acuity_encrypt_key1.key`.
  3. External absolute path — path stored in CMI, key in file.
- Consistent key naming: slot number embedded in name across all methods.
- Keys never stored in CMI config or the database.
- Admin page shows live status banner and key backup panel (reveal + copy).
- `settings.php` never written programmatically.

### Field type: acuity_encrypted_text (replaced in 0.2.0)

- Custom field type, widget, and formatters (see 0.2.0 for replacement).

### Permissions

- `administer acuity_encrypt` (`restrict access: TRUE`).
- `view encrypted fields` (`restrict access: TRUE`).
