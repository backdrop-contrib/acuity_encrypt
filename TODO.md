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

- [ ] **Key reveal panel** on View Keys tab
      - Confirm raw key shown behind Reveal button

- [ ] **Cache clear safety**
      - Clear caches → load encrypted node → confirm decrypted correctly
      - (Regression check for the `backdrop_static()` cache in load hook)

---

## Phase 1 follow-on (post-0.2.0, pre-Phase 2)

- [ ] Build `acuity_secure_message` module
      - Uses `acuity_encrypt_encrypt()` / `acuity_encrypt_decrypt()` API
      - Stores message body encrypted at rest in a custom DB table
      - Depends on acuity_encrypt being stable and tested

- [ ] Update `.info` description to reflect widget-based approach
      (current description still says "encrypted text field type")

- [ ] Add warning in Field UI when admin switches a field AWAY from the
      acuity_encrypt_widget while the field has data — existing ciphertext
      will display raw if the widget is changed.

- [ ] `text_with_summary` widget: handle summary masking in edit form
      (currently only `value` is masked; `summary` renders plain in widget)

- [ ] Consider `hook_field_widget_settings_form()` for per-instance row count
      configuration (currently defaults to 5 rows for all long text fields)

---

## Phase 2 — Key slot management (do not start until acuity_secure_message done)

Full design is in CLAUDE.md under "PHASE 2 — KEY SLOT MANAGEMENT".

- [ ] `config/acuity_encrypt.slots.json` — slot registry
- [ ] View Keys tab: show all slots with status and encrypted-value counts
- [ ] Add Slot form (same storage options as current Set Keys form)
- [ ] Set Active slot
- [ ] Hard Rotate batch (re-encrypt enc{old}: → enc{new}: in Batch API)
- [ ] Missing key prominent warning (shown on any admin page if detected)
- [ ] Slot value count maintenance on presave/delete

---

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
