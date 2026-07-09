# Acuity Encrypt — Changelog

All notable changes to this module are documented here.
Format loosely follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

---

## 1.x-0.3.0 (unreleased) — 2026-07-06

Phase 2: key slot management and hard rotation. Built and verified with a
32-assertion CLI test harness against a live site (rotation forward and
back on real nodes including revisions).

### Security (2026-07-09)

- **Decryption is now permission-gated.** `hook_field_attach_load()` only
  decrypts for users with `view encrypted fields`. Previously it decrypted
  unconditionally, so core formatters, Views raw output, and tokens served
  plaintext to unpermitted users; those paths now see ciphertext (fail
  closed) and this module's formatters render a static mask for any value
  that is still ciphertext. Consequence: encrypted fields are no longer
  searchable (cron indexing runs unprivileged) — rebuild the search index
  to flush any previously indexed plaintext.
- **Plaintext no longer written to persistent caches.** Core writes entities
  to `cache_entity_*` / `cache_field` AFTER the decrypt hook runs, so
  decrypted plaintext was being persisted into the same database — and,
  once decrypt became user-dependent, whatever the first loader saw would
  have been cached for everyone. `hook_entity_info_alter()` now disables
  persistent entity/field caching for any bundle containing an encrypted
  field (instance CRUD hooks keep the flags current), and
  `acuity_encrypt_update_1002()` purges cache entries written by older
  versions. This also fixes stale decrypted values being served from the
  entity cache after a field's widget was switched away from Acuity
  Encrypt.
- Unauthorised users' edit forms now round-trip the ciphertext instead of
  hiding decrypted plaintext in `#type => 'value'` elements (plaintext no
  longer enters the form cache).

### Changed (2026-07-09)

- Display formatters now show the explanatory mask "Encrypted text - If you
  have permission to view, click Reveal" (translatable) instead of the
  `••••••••` bullets. Edit-widget masks and the admin key backup panel keep
  the bullet style.
- New formatter "Acuity Encrypt: summary or trimmed" (text_with_summary
  only) — fail-closed replacement for core's "Summary or trimmed", which
  renders raw ciphertext for viewers without permission. Decrypted summary
  when present, else the value trimmed to the trim_length setting
  (default 600, same as core); undecrypted values get the mask.
- New per-display formatter setting "Hide completely for viewers without
  the view encrypted fields permission" (all formatters, default off):
  outputs nothing instead of the mask, so the field — label included —
  disappears for unauthorised viewers on node displays and in Views
  (combine with Views' "No results behavior: Hide if empty").

### Fixed (2026-07-09)

- Admin key backup panel: clicking Hide after Reveal only set the fetched
  key to `display:none`, leaving the raw key in the page DOM. Hide now
  scrubs the key text from the DOM entirely; clicking Reveal again
  re-fetches it via the token-protected AJAX endpoint. (Field formatter
  reveal is unaffected — its hidden value is embedded server-side for
  authorised users by design.)

- `text_with_summary` widget had no summary sub-element: the "Edit summary"
  link never appeared, the summary could not be edited, and — because the
  form never submitted a `summary` key — the stored summary was silently
  dropped on save. The widget now mirrors core's
  `text_textarea_with_summary` construction (including core's text.js
  toggle link); the summary renders inside the reveal overlay so it is
  masked/revealed with the main value, and unauthorised users' forms
  round-trip it untouched via a hidden value element. The main element also
  gains core's `text-full` class — text.js anchors the "Edit summary" link
  to it, so without the class the link (and a hidden empty summary) never
  appeared.
- Plain short-text fields (text processing off, empty format) displayed
  ciphertext through both of this module's formatters: `safe_value` was
  only recomputed after decrypt when the item had a text format, but core's
  text module had already set `safe_value` to sanitized CIPHERTEXT — and
  the formatters prefer `safe_value`. Formatless items now get
  `check_plain(plaintext)`, mirroring text module's `_text_sanitize()`
  (same fix for `safe_summary`).

### Key slots

- New `config/acuity_encrypt.slots.json` registry: active slot id plus
  per-slot label and storage (settings.php / private file / external file —
  each slot independent). Keys themselves still never touch config or DB.
- `acuity_encrypt_encrypt()` slot parameter is now NULL-default and resolves
  to the **active slot**; `_acuity_encrypt_get_key()` resolves any slot
  (settings.php `acuity_encrypt_key{N}` always wins, then registered storage).
- The slot registry is the sole source of truth: the legacy single-slot
  `acuity_encrypt.settings` config is removed entirely (the "Set keys"
  accordion now reads/writes registry slot 1 directly), and
  `acuity_encrypt_update_1001()` deletes the stale config file.
- View keys tab rewritten: every slot with storage, key identifier, live
  encrypted-entry count (data + revision tables), derived status (Active /
  In use / Available / KEY MISSING / No key configured), and operations.
  Orphan detection flags `enc{N}:` data whose slot is unregistered.
- Add/Edit slot form with conditional storage fields (#states), per-slot
  Generate key button (admin JS is now `data-slot` aware), webroot-path
  rejection, and per-slot private-file overwrite confirmation.
- Set active slot confirm form — blocked if the slot has no key.

### Hard rotation

- New `acuity_encrypt.rotate.inc`: Batch API re-encryption of every
  `enc{from}:` value under the active slot, **including field_revision_***
  tables (a key is only retirable once revisions are migrated too).
- Direct `db_update()` on field tables — entity hooks, timestamps, and
  revision history untouched; field + entity caches flushed on completion.
- Optimistic old-value guard against concurrent edits; rows that fail to
  decrypt are skipped permanently (no infinite loop), logged, and left
  byte-for-byte untouched. Completion recount tells the admin when the
  source slot is safe to retire.

### Missing-key warning

- Error banner on every admin page (for `administer acuity_encrypt` users)
  when encrypted data exists whose slot has no key. Driven by a state flag
  maintained by the counts function (refreshed on cron, the keys overview,
  and rotation finish) — no counting queries on ordinary page loads.

### Public API additions

- `acuity_encrypt_encrypt_with_key()` / `acuity_encrypt_decrypt_with_key()` —
  same AES-256-GCM construction with a caller-supplied 32-byte key and no
  slot prefix. Used by acuity_secure_link for HKDF(token)-derived per-link
  payload encryption.
- `acuity_encrypt_slots()`, `acuity_encrypt_active_slot()`,
  `acuity_encrypt_slot_value_counts()`, `acuity_encrypt_slot_status()`,
  `acuity_encrypt_encrypted_field_map()`.

### UI wording

- Slot data counts are labelled "encrypted entries" (stored field values,
  including revision copies) — previously "values", which read ambiguously
  as a count of keys.

### Encrypted-length guard (bug fix)

- Encryption inflates storage (~1.34 × plaintext + ~45 chars), so on
  single-line `text` fields an over-long plaintext produced ciphertext
  exceeding the database column — failing the save in strict SQL mode or,
  worse, silently truncating into permanently undecryptable data.
- New `acuity_encrypt_max_plaintext_bytes($max_length, $slot)` computes the
  effective plaintext byte limit (158 for the default 255-char field).
- The widget now shows the limit in the field description, caps
  `#maxlength`, and byte-validates on submit; `hook_field_attach_presave()`
  throws a `LengthException` for over-long programmatic saves (Feeds,
  custom code) instead of letting the database decide.
- README documents the storage overhead formula for site builders.

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
