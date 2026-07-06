# Acuity Encrypt — TODO

Priority order within each section. Items in **bold** are blockers for the
next release.

---

## Testing (must complete before 1.x-0.2.0 release)

- [ ] **Round-trip: single-line text field**
      - Create node → value visible masked on display → edit shows ••••••••
        → reveal → confirm plaintext → save → view confirms display masked
      - Confirm ciphertext in DB (`enc1:...` prefix present)

- [ ] **Round-trip: long text / WYSIWYG**
      - Same as above but with a `text_long` field + CKEditor widget
      - Confirm CKEditor loads under overlay → reveal shows working editor
      - Confirm formatted HTML is preserved through encrypt/decrypt cycle

- [ ] **Round-trip: text with summary**
      - Confirm both `value` and `summary` are encrypted/decrypted correctly
      - Confirm `safe_summary` refreshed after decrypt

- [ ] **Unauthorised user workflow**
      - Log in as user without `view encrypted fields`
      - Edit node: confirm locked placeholder shown, no plaintext in DOM
      - Save: confirm value remains encrypted (not blanked)

- [ ] **Empty field: first encryption**
      - New node, empty field: confirm plain widget shown (no masking)
      - Save: confirm value encrypted in DB on first save

- [ ] **Display formatter: acuity_encrypt_reveal**
      - Authorised user: ••••••• + Reveal button → click → plaintext visible
        → click Hide → masked again
      - Unauthorised user: ••••••• only, no button, no value in DOM

- [ ] **Display formatter: acuity_encrypt_plain**
      - Confirm plaintext shown directly (no masking)

- [ ] **VBO bulk encryption**
      - Populate field with plain text (manually insert, bypass presave)
      - Enable acuity_encrypt_widget on that field
      - Run VBO action on all records
      - Confirm all values now have `enc1:` prefix in DB

- [ ] **Key storage — settings.php**
      - Add key to settings.php → status banner shows Active → encrypt/decrypt works

- [ ] **Key storage — private file**
      - Write key via admin UI → confirm file created → encrypt/decrypt works

- [ ] **Key storage — external file**
      - Place key file → enter path → encrypt/decrypt works

- [ ] **Generate Key button** (all three modes)
      - settings.php mode: output is `$settings['acuity_encrypt_key1'] = '...'`
      - private/external modes: output is raw 64-char hex key
      - Copy to clipboard works

- [ ] **Key reveal panel** on Set Keys tab
      - Confirm raw key NOT present in page source before clicking Reveal
      - Click Reveal → key fetched via AJAX and shown; Copy button works
      - Confirm the reveal-key URL 403s / returns an error for a user without
        `administer acuity_encrypt`, and for a stale/missing token

- [ ] **Cache clear safety**
      - Clear caches → load encrypted node → confirm decrypted correctly
      - (Regression check for the `backdrop_static()` cache in load hook)

- [ ] **Key overwrite confirmation** (private files)
      - With an existing key file, paste a different key and Save without
        ticking the checkbox → form error, key file unchanged
      - Tick the checkbox → save succeeds, new key file written
      - Save with the field blank → existing key preserved, no checkbox needed
      - Save with the same key pasted back → no checkbox needed

- [ ] **External file path rejected inside webroot**
      - Point the external file path at a file under the Backdrop docroot →
        confirm the save is blocked with a form error (not just a warning)

- [ ] **Double-encryption guard on realistic input**
      - Save a field value like `enc1:not really ciphertext` → confirm it is
        encrypted (not stored verbatim) and decrypts back to the original text

---

## Phase 1 follow-on (post-0.2.0, pre-Phase 2)

Plan revised 2026-07-06: secure_message is now a consumer of a shared
`acuity_secure_link` core (a generic tokenised-link engine; other consumers
register their own link types). Design spec: `../acuity_secure_link/CLAUDE.md`;
build checklist: `../acuity_secure_link/TODO.md`.

- [x] **Add key-override variants to the public API** (done 2026-07-06;
      standalone round-trip/tamper tests pass):
      - `acuity_encrypt_encrypt_with_key(string $plaintext, string $key): string|false`
      - `acuity_encrypt_decrypt_with_key(string $ciphertext, string $key): string|false`
      - Same AES-256-GCM construction, caller supplies the 32-byte key;
        no slot prefix (secure_link uses its own `tok:` wire prefix)
- [x] Build `acuity_secure_link` core module (done 2026-07-06 — Phase A in
      its TODO; in-Backdrop testing outstanding)
- [ ] Build `acuity_secure_message` as secure_link consumer (Phase B)
      - Depends on acuity_encrypt being stable and tested

- [x] Update `.info` description to reflect widget-based approach and
      Phase 2 slots/rotation (done 2026-07-06)

- [ ] Add warning in Field UI when admin switches a field AWAY from the
      acuity_encrypt_widget while the field has data — existing ciphertext
      will display raw if the widget is changed.

- [x] **Presave length guard for single-line `text` fields** (done
      2026-07-06, 12-assertion CLI test): widget shows the effective limit
      in its description, caps #maxlength, and byte-validates on submit;
      presave throws LengthException for programmatic saves. Helper:
      `acuity_encrypt_max_plaintext_bytes($max_length, $slot)`.

- [ ] `text_with_summary` widget: handle summary masking in edit form
      (currently only `value` is masked; `summary` renders plain in widget)

- [ ] Consider `hook_field_widget_settings_form()` for per-instance row count
      configuration (currently defaults to 5 rows for all long text fields)

---

## Phase 2 — Key slot management (BUILT + CLI-TESTED 2026-07-06)

Full design + implementation notes in CLAUDE.md under "PHASE 2".

- [x] `config/acuity_encrypt.slots.json` — slot registry (+ update_1000
      migration for existing sites; legacy Set Keys accordion kept in sync)
- [x] View Keys tab: all slots with status + live encrypted-value counts
      (data AND revision tables; orphan-slot detection)
- [x] Add/Edit Slot form (label, storage radios via #states, per-slot
      generate button, external-path webroot rejection, private-file
      overwrite confirmation)
- [x] Set Active slot (confirm form; blocked if slot has no key)
- [x] Hard Rotate batch (Batch API, 50 rows/pass, revisions included,
      failure-safe, cache flush + recount on finish)
- [x] Missing key prominent warning on all admin pages (state flag,
      refreshed by cron/overview/rotation — no per-page queries)
- [x] Value counts: computed live, cached in state (deliberately NOT
      maintained per-presave — see CLAUDE.md)

### Phase 2 browser testing (CLI harness passed; click-through outstanding)

- [ ] Keys overview table renders correctly with 2+ slots
- [ ] Add slot → appears in table as "No key configured" until key added
- [ ] Set active confirm flow; blocked when key missing
- [ ] Rotate form → batch progress bar → completion messages
- [ ] Missing-key banner appears on other admin pages within a cron run

---

## Phase 3 — File encryption (future, design agreed 2026-07-06)

Envelope encryption for uploaded files. Design sketch in CLAUDE.md under
"PHASE 3 — FILE ENCRYPTION". Key points: random per-file key encrypts the
file (sodium secretstream, chunked, streaming); file key is wrapped with
the active SLOT key via acuity_encrypt_encrypt_with_key() and stored in a
DB table (enc{slot}: prefix) — so slot rotation re-wraps tiny keys instead
of re-encrypting gigabytes, and file counts fold into the existing slot
status/entry counts. Opt-in per file field. v1 scope: plain file fields
(PDF/DXF/photos without derivatives), streamed decrypt via
hook_file_download. Known hard parts: image-style derivatives, modules
reading files directly (getid3, pdf_to_image, imagemagick), byte-range
serving for pdf.js.

- [ ] Build `acuity_encrypt_file` (or submodule) per the sketch

Note: sending a document via a one-off secure link (encrypted attachment on
acuity_secure_message) is a SEPARATE, much simpler feature — token-derived
key, no slots/rotation involved — tracked in
../acuity_secure_link/TODO.md "Phase B follow-on". Build that one first.

## Future / Nice to have

- [ ] Per-field "last encrypted" timestamp for audit trail
- [ ] Drush command: `drush acuity-encrypt-rotate --from=1 --to=2`
- [ ] `acuity_encrypt_long_text` formatter variant that renders HTML safely
      (currently uses `safe_value` which applies the stored text format —
      consider whether a separate "strip tags" plain variant is useful)
- [ ] Token support: `[node:field-name]` currently returns decrypted plaintext
      (this is intentional — document it clearly in README)
- [ ] Search index: encrypted values decrypt before indexing (intentional —
      document it clearly; site builder may want to exclude from search)
