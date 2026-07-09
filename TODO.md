# Acuity Encrypt — TODO

Priority order within each section. Items in **bold** are blockers for the
next release.

---

## Testing (must complete before 1.x-0.2.0 release)

- [x] **Round-trip: single-line text field**
      - Create node → value visible masked on display → edit shows ••••••••
        → reveal → confirm plaintext → save → view confirms display masked
      - Confirm ciphertext in DB (`enc1:...` prefix present)

- [x] **Round-trip: long text / WYSIWYG**
      - Same as above but with a `text_long` field + CKEditor widget
      - Confirm CKEditor loads under overlay → reveal shows working editor
      - Confirm formatted HTML is preserved through encrypt/decrypt cycle

- [x] **Round-trip: text with summary**
      - Confirm both `value` and `summary` are encrypted/decrypted correctly
      - Confirm `safe_summary` refreshed after decrypt

- [x] **Unauthorised user workflow**
      - Log in as user without `view encrypted fields`
      - Edit node: confirm locked placeholder shown, no plaintext in DOM
      - Save: confirm value remains encrypted (not blanked)

- [x] **Empty field: first encryption**
      - New node, empty field: confirm plain widget shown (no masking)
      - Save: confirm value encrypted in DB on first save

- [x] **Display formatter: acuity_encrypt_reveal**
      - Authorised user: mask text ("Encrypted text - If you have permission
        to view, click Reveal") + Reveal button → click → plaintext visible
        → click Hide → masked again
      - Unauthorised user: mask text only, no button, no value in DOM

- [x] **Display formatter: acuity_encrypt_plain**
      - Authorised user: plaintext shown directly (no masking)
      - Unauthorised user: mask text, no value in DOM

- [x] **Display formatter: acuity_encrypt_summary (summary or trimmed)**
      - Authorised, node with summary: decrypted summary shown
      - Authorised, node without summary: decrypted value trimmed to the
        trim_length setting
      - Unauthorised user: mask text (compare: core's "Summary or trimmed"
        on the same field shows raw ciphertext — that's why ours exists)

- [x] **Formatter setting: "Hide completely" (hide_denied)**
      - Tick on a formatter in Manage display → unauthorised user sees NO
        field output at all (no label, no wrapper); authorised user
        unaffected
      - Untick → unauthorised user sees the mask text again

- [x] **Views integration**
      - Add an encrypted field to a view with an Acuity Encrypt formatter:
        authorised user sees plaintext/reveal; unauthorised sees mask
      - With hide_denied + "No results behavior: Hide if empty":
        unauthorised user sees no trace of the field in the row
      - Confirm a Views FILTER on the encrypted field does NOT match
        plaintext terms (DB holds ciphertext — documented limitation)

- [ ] **VBO bulk encryption**
      - Populate field with plain text (manually insert, bypass presave)
      - Enable acuity_encrypt_widget on that field
      - Run VBO action on all records
      - Confirm all values now have `enc1:` prefix in DB

- [x] **Key storage — settings.php**
      - Add key to settings.php → status banner shows Active → encrypt/decrypt works

- [ ] **Key storage — private file**
      - Write key via admin UI → confirm file created → encrypt/decrypt works

- [ ] **Key storage — external file**
      - Place key file → enter path → encrypt/decrypt works

- [x] **Generate Key button** (all three modes)
      - settings.php mode: output is `$settings['acuity_encrypt_key1'] = '...'`
      - private/external modes: output is raw 64-char hex key
      - Copy to clipboard works

- [x] **Key reveal panel** on Set Keys tab
      - Confirm raw key NOT present in page source before clicking Reveal
      - Click Reveal → key fetched via AJAX and shown; Copy button works
      - Click Hide → key REMOVED from the DOM (inspect the
        .acuity-encrypt-value span — must be empty, not just display:none);
        Reveal again re-fetches (fixed 2026-07-09)
      - Confirm the reveal-key URL 403s / returns an error for a user without
        `administer acuity_encrypt`, and for a stale/missing token

- [x] **Cache clear safety**
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

- [x] **Double-encryption guard on realistic input**
      - Save a field value like `enc1:not really ciphertext` → confirm it is
        encrypted (not stored verbatim) and decrypts back to the original text

- [x] **Permission-gated decrypt (added 2026-07-09)**
      - As a user WITHOUT `view encrypted fields`: view a node whose
        encrypted field uses a CORE text formatter → raw ciphertext shown
        (not plaintext); with this module's formatters → static mask
      - Same user edits + saves the node → DB value unchanged byte-for-byte
      - As a user WITH the permission: core formatter shows plaintext
      - Confirm `cache_entity_node` holds NO plaintext after a privileged
        user views an encrypted node (bundle cache disabled)
      - Switch the field's widget away from Acuity Encrypt → confirm
        ciphertext (never stale plaintext) renders for all users
      - Run cron with search enabled → confirm search_index holds ciphertext
        for encrypted fields, and searching the plaintext finds nothing

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
- [x] Build `acuity_secure_message` as secure_link consumer (Phase B)
      - Depends on acuity_encrypt being stable and tested

- [x] Update `.info` description to reflect widget-based approach and
      Phase 2 slots/rotation (done 2026-07-06)

- [ ] Add warning in Field UI when admin switches a field AWAY from the
      acuity_encrypt_widget while the field has data — existing ciphertext
      will display raw if the widget is changed.

- [ ] **Keep edit-form plaintext out of the form cache (tempstore)**
      (at-rest leak, investigated 2026-07-09; post-beta). An authorised
      user's edit form holds decrypted plaintext because attach_load ran.
      If the SAME form contains any AJAX element (a file/image field is the
      common case — managed_file uploads set `$form_state['cache'] = TRUE`
      via ajax_process_form()), core caches the form to the DB `tempstore`
      table (collections `form` + `form_state`, keyed by form_build_id,
      ~6h TTL). Same CLASS as the cache_entity bug (fixed), but narrower:
      edit-only, transient, only the editor's own authorised data.

      PLAINTEXT LEAKS IN TWO PLACES (both must be fixed):
      1. Form structure: `$unprocessed_form` snapshot (form.inc:909, taken
         BEFORE form_builder) contains the widget's plaintext #default_value.
      2. Form state: form_set_cache persists $form_state minus
         form_state_keys_no_cache() (form.inc:595/603) — that list excludes
         `values`/`input` but NOT `node`, so the DECRYPTED node object in
         $form_state['node'] is cached too.

      "JUST DISABLE CACHING" IS A DEAD END: ajax_get_form() (ajax.inc:333)
      rehydrates AJAX callbacks purely from form_get_cache() with NO
      rebuild-from-scratch fallback — empty cache logs "Invalid form POST
      data" and backdrop_exit()s. So forcing no_cache breaks the very AJAX
      element (file upload / add-another) whose presence caused the caching.
      The leak only exists WHEN there's an AJAX element, and that element
      REQUIRES the cache — you cannot win with a global toggle.

      CLEAR-ON-SAVE IS ONLY PARTIAL: tempstore_clear('form', $build_id) +
      tempstore_clear('form_state', $build_id) in a submit handler (build_id
      is stable for authenticated users — page not cacheable → not
      immutable) shrinks the window for SAVED edits, but does NOTHING for
      abandoned/never-saved sessions (open edit, walk away) which sit until
      TTL — probably the larger real-world window. Worth adding as cheap
      defence-in-depth, but it is not the fix.

      REAL FIX (keep plaintext out of tempstore entirely):
      - Defer #default_value to a #process/#after_build callback (runs
        INSIDE form_builder, AFTER the $unprocessed_form snapshot) so the
        form structure caches ciphertext/nothing, not plaintext. On AJAX
        rebuilds the field value comes from POST input anyway, so the
        deferred default isn't needed there.
      - ALSO scrub or re-encrypt the encrypted fields on $form_state['node']
        before it is cached (no clean core "pre-cache" hook — likely a late
        #after_build on the form). This is the awkward half.
      - Site-level interim mitigation (document, don't impose): shorten
        `form_cache_expiration` in settings.php.
      - Test matrix: plain text, long text/WYSIWYG, text_with_summary,
        validation-error rebuild, AJAX rebuild, node with an image field.
      - README "Threat model" + the form-cache caveat already disclose this
        honestly; tighten once fixed.

- [x] **Presave length guard for single-line `text` fields** (done
      2026-07-06, 12-assertion CLI test): widget shows the effective limit
      in its description, caps #maxlength, and byte-validates on submit;
      presave throws LengthException for programmatic saves. Helper:
      `acuity_encrypt_max_plaintext_bytes($max_length, $slot)`.

- [x] `text_with_summary` widget: summary sub-element added (2026-07-09),
      mirroring core's text_textarea_with_summary construction including
      text.js ("Edit summary" link). Previously the widget had NO summary
      element, so the stored summary was silently dropped on save and there
      was no way to edit it. The summary sits inside the WYSIWYG overlay
      wrapper, so it is masked/revealed together with the main value;
      unauthorised users round-trip it via a hidden value element.

- [ ] Consider `hook_field_widget_settings_form()` for per-instance row count
      configuration (currently defaults to 5 rows for all long text fields)

- [ ] **Per-field widget setting: empty-field entry for users without
      `view encrypted fields`** (decided 2026-07-09). Today an empty
      encrypted field (e.g. node/add) renders a plain input for ANY user —
      write-once-then-locked. No confidentiality leak, but the site owner
      should choose per field: "allow initial entry" vs "locked (no
      input)". Steve's preference: default LOCKED — unprivileged users
      shouldn't be able to enter anything. Implementation notes:
      - Same hook_field_widget_settings_form() as the row-count item above.
      - Locked + required field = users without the permission cannot
        create the node at all — call this out in the setting description.
      - Locked rendering: reuse the existing locked placeholder markup.
      - When entry IS allowed, show unprivileged users a description:
        "Encrypted field — once saved you will not be able to view or
        change this value."

---

## Phase 2 — Key slot management (BUILT + CLI-TESTED 2026-07-06)

Full design + implementation notes in CLAUDE.md under "PHASE 2".

- [x] `config/acuity_encrypt.slots.json` — slot registry, the sole config
      (legacy single-slot settings removed 2026-07-09; update_1001 deletes
      the stale file; Set Keys accordion reads/writes registry slot 1)
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

- **DECISION (2026-07-09): E2E / zero-knowledge encryption is OUT OF SCOPE
  for this module.** This module is encryption-at-rest: the key lives
  server-side so the server can display/search/rotate. True E2E (user-held
  key, server never decrypts) is incompatible with all of that and is a
  separate product. Documented for users in README "Threat model —
  Encryption at Rest, Not End-to-End". Classed as a fringe/edge request; a
  bespoke E2E module may be built separately and may be chargeable. Do not
  bolt a client-side-key mode onto this module. (See also the earlier
  analysis rejecting client-side JS decryption: it needs the key in the
  browser, converting a transient per-field exposure into permanent
  full-key exposure.)

- [ ] **Never reuse slot ids — monotonic counter** (fringe case, analysed
      2026-07-09). If slot N is removed from the registry while stale
      `enc{N}:` data remains, and a NEW slot N is later created with a
      different key: GCM fails safe (wrong key → tag mismatch → mask, no
      wrong plaintext), BUT the stale values stop being counted as orphans —
      they're attributed to the new slot N, which shows "In use" with a key
      that cannot decrypt them, and the missing-key alarm stays quiet.
      Silent, misleading. Realistic trigger is a RESTORED OLD BACKUP:
      the restored DB carries enc{N}: data from a retired slot while the
      current registry has since reused (or could reuse) that id — also
      reachable by hand-editing slots.json. Low exposure through the UI
      today (Add slot = max(ids)+1, no slot-delete UI), but MUST be done
      before any slot-delete feature ships. README "Slot ids, old backups,
      and key reuse" documents the safe-failure behaviour for users. Fix:
      - Persist a `next_slot_id` high-water mark in acuity_encrypt.slots;
        new id = max(next_slot_id, max(registry ids) + 1, highest enc{N}
        found in data + 1). Never decrement.
      - The data scan reuses the orphan-detection REGEXP from
        acuity_encrypt_slot_value_counts().

- [ ] **Pluggable key storage providers** (FEATURE REQUEST — build only if a
      user asks; design agreed 2026-07-09):
      - Enabler refactor: turn the hard-coded storage branches in
        `_acuity_encrypt_get_key()` (and the slot form's storage radios)
        into providers declared via `hook_acuity_encrypt_key_providers()` —
        each provider = label + slot-form config snippet + fetch-raw-key
        callback. Core ships the existing three (settings_php,
        private_file, external_file) as built-in providers; remote options
        become small add-on modules.
      - Candidate providers, simplest first:
        1. Environment variable — ~20 lines, works on most panels/Docker.
        2. Authenticated HTTPS endpoint — slot stores URL + secret header
           token; fetch over TLS, cache in backdrop_static for the request,
           fail closed into the existing missing-key warning. Endpoint can
           be anything (second server, Cloudflare Worker).
        3. Cloud secret managers (AWS Secrets Manager / HashiCorp Vault) —
           last resort; needs SDK or hand-rolled request signing.
      - No-code alternative to document meanwhile: mount remote storage as
        a filesystem (s3fs / rclone mount / NFS) and use the existing
        external_file method.
      - SECURITY NOTE for any provider docs: a dev running an AWS-secrets
        key setup committed his AWS credentials to git and was compromised
        (heard 2026-07-09). Provider credentials belong in settings.php or
        server env vars ONLY — never in CMI config (it exports), never in
        git. Docs must say this loudly.

- [ ] Per-field "last encrypted" timestamp for audit trail
- [ ] Drush command: `drush acuity-encrypt-rotate --from=1 --to=2`
- [ ] `acuity_encrypt_long_text` formatter variant that renders HTML safely
      (currently uses `safe_value` which applies the stored text format —
      consider whether a separate "strip tags" plain variant is useful)
- [ ] Token support: `[node:field-name]` currently returns decrypted plaintext
      (this is intentional — document it clearly in README)
- [ ] Search index: encrypted values decrypt before indexing (intentional —
      document it clearly; site builder may want to exclude from search)
