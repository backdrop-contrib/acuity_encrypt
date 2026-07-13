# Encryption (Acuity Utils)
**Acuity Encrypt** - AES-256-GCM field encryption for Backdrop CMS with multi-slot key management and zero-downtime key rotation.

Acuity Encrypt is a Backdrop CMS module that adds at-rest encryption to any standard text field using PHP's built-in OpenSSL extension - no external libraries. Encryption is enabled by selecting a custom widget in Field UI: no new field type, no data migration, no schema changes. Values are decrypted transparently on load **for users with the `view encrypted fields` permission** - Views, tokens, and other consumers receive plaintext for those users, while everyone else (and database dumps) sees only ciphertext. A public API (including caller-supplied-key variants) powers the companion `acuity_secure_link` and `acuity_secure_message` modules.

## Beta Release Notes
As a beta release, the encryption core, field widget/formatters, key slot management, and hard rotation are fully functional and verified by CLI test harnesses against a live site (including rotation forward and back on real content with revisions). Browser click-through of the newer slot admin pages is still in progress. Before deploying to production, ensure your Backdrop CMS environment is running PHP 8.0+, clear your system caches after installation, and **back up your encryption key to a password manager before encrypting anything** - a lost key means permanently unreadable data.

## Features

* **Encrypt any text field:** Works on `text`, `text_long`, and `text_with_summary` field types. Enable by selecting the **Acuity Encrypt (masked text)** widget in Manage Fields; disable by switching back. Existing fields can be encrypted in-place.
* **AES-256-GCM authenticated encryption:** Fresh random IV per value, 16-byte authentication tag - tampered or corrupted ciphertext is detected and rejected automatically. Zero external dependencies (PHP's built-in `openssl`).
* **Multi-slot key management:** Multiple key slots, each with its own independent storage method. Every ciphertext carries its slot id (`enc1:`, `enc2:` …) so the correct key is always used automatically, even mid-rotation.
* **Zero-downtime key rotation:** Add a new slot, set it active (new encryptions switch instantly; old data still decrypts), then run the Hard Rotate batch to re-encrypt everything - **including revisions** - under the new key. The slot table's live entry counts tell you exactly when the old key is safe to retire.
* **Missing-key alarm:** If encrypted data exists whose key slot has no key configured, a prominent error banner appears on every admin page until it is restored.
* **Keys never touch the database:** Three storage methods per slot - `settings.php` (recommended), private filesystem, or an external file outside the webroot. Keys never appear in CMI config exports or database dumps.
* **Three fail-closed display formatters:** *Masked with reveal button* (a clickable "Encrypted" word that toggles the value in-page, with a "Hide" control while revealed), *plain decrypted text*, and *summary or trimmed* (a safe replacement for core's version, which would render raw ciphertext to unauthorised viewers). All three render the mask for anyone without the `view encrypted fields` permission - or, with the per-display "Hide completely" setting, render nothing at all. Unauthorised users' edits preserve the encrypted value untouched.
* **The mask never lies:** the "Encrypted" reveal toggle is only shown when there is actually something to decrypt. A value that is still plaintext in the database (for example mid-migration, before the bulk encrypt has run) is displayed to authorised users as ordinary text - so the display doubles as an honest migration indicator, and you can never mistake an unencrypted value for a protected one. Viewers without the permission see the mask regardless.
* **WYSIWYG support:** Long text fields retain full CKEditor support behind a "Reveal to edit" overlay mask.
* **Encrypted-length guard:** Encryption inflates storage (~1.34 × plaintext + ~45 characters). The widget shows the effective limit, caps input, and byte-validates on submit; programmatic saves that would overflow the column are blocked with a clear exception instead of silently truncating into undecryptable data.
* **Permission-gated decryption:** Values decrypt on load only for users with `view encrypted fields`. For those users Views, tokens, and Rules receive plaintext automatically; for everyone else the ciphertext stays in place - this module's widget and formatters render a mask, and core formatters render harmless ciphertext (fail closed). Unprivileged machine contexts (cron search indexing, anonymous-triggered emails) therefore see ciphertext: encrypted fields are deliberately **not searchable**, and no plaintext ever reaches the search index.
* **No plaintext in the display/render caches:** Persistent entity/field caching is automatically disabled for any bundle containing an encrypted field, so decrypted values are never written to `cache_entity_*` / `cache_field` database tables when content is *viewed* - a database dump exposes ciphertext only. (One known, narrow exception is tracked for a future release: an authorised user's *edit* form can be written to the `cache_form` table if that form also contains an AJAX element such as a file/image field. See "Threat model" below.)
* **VBO bulk encryption:** Views Bulk Operations action to encrypt existing plain text values in one batch.
* **VBO field copy (safety net):** Views Bulk Operations action to copy one text field's values into another in batch - back up a plaintext field to a temporary field before encrypting the original, and restore in the other direction if needed by swapping source and destination.
* **Public API:** `acuity_encrypt_encrypt()` / `acuity_encrypt_decrypt()`, plus `_with_key()` variants for caller-supplied 32-byte keys (used by `acuity_secure_link` for token-derived per-link encryption).
* **GDPR / UK DPA / Data (Use and Access) Act (DUAA):** Database dumps without the key expose no sensitive personal data.
* **Backdrop Native:** Built exclusively for Backdrop CMS using strict PHP 8.0+ standards and Backdrop APIs throughout.

## Requirements

- Backdrop CMS 1.x
- PHP 8.0+ (While the code may technically function with PHP 7.4 at this time, we strictly require PHP 8.0+ and will not address issues related to older PHP versions.)
- PHP `openssl` extension (enabled by default in most PHP installs)
- Views Bulk Operations module (optional - only needed for the bulk encryption and field copy actions)

## Installation

Install this module using the official Backdrop CMS instructions at https://docs.backdropcms.org/documentation/extend-with-modules

Enable the module, then clear your system caches to register the admin menu items.

## Configuration

1. Navigate to **Admin → Configuration → Acuity Utils → Encryption Settings → Set keys** and configure a key for Slot 1 using one of the three storage methods. Each section has inline step-by-step instructions and a **Generate key** button.
2. **Back up the key immediately** using the reveal panel - copy it to a password manager (e.g. Bitwarden). A database backup without the key is unreadable; losing the key makes all encrypted values permanently unrecoverable.
3. Grant the `view encrypted fields` permission to trusted roles at **Admin → People → Permissions**. Neither of this module's permissions is granted to any role by default.
4. Go to **Admin → Structure → Content types → [Type] → Manage fields** and change the widget of any text field to **Acuity Encrypt (masked text)**. All future saves encrypt the value.
5. Under **Manage display**, select **Acuity Encrypt: masked with reveal button** (or the plain decrypted formatter) for the field.
6. To encrypt values that existed before you enabled the widget, run the included VBO action **Acuity Encrypt: encrypt existing field values** from a View with bulk operations, then verify `enc1:` ciphertext in the database.

## Try It Before You Trust It (Encryption Test Phase)

Encryption is unforgiving - if the key is lost, the data is gone. Before you rely on this module for anything that matters, prove it to yourself first. We actively encourage this; it is exactly what we would do.

**Golden rules for the test phase**

* Test on a **development or staging copy**, never on the only copy of data that matters.
* **Back up your database first**, and back up your key the moment you create it (the reveal panel on the Set Keys tab).
* During the trial you are deliberately testing what happens when a key goes missing - so keep a **separate, unencrypted copy** of your test data until you are done. Don't let the encrypted field become the only copy of anything you would hate to lose.

**A good side-by-side test, using your own data**

Pick any field holding data you would want encrypted - an API token, a licence key, a private note, a password, an account number, whatever fits your use case. The example below calls it `field_secret`, but substitute your own field. This keeps your original plaintext field intact next to a new encrypted field, so you can watch the same value in both places and confirm the round-trip:

1. On a copy of your site, pick an existing plaintext field - here, `field_secret` on some content type.
2. Add a **second** field, e.g. `field_secret_enc`, and set its widget to **Acuity Encrypt (masked text)** under Manage fields. Configure a key first if you have not already (see Configuration).
3. Copy the values across. The easiest bulk method is the included VBO action **Acuity Encrypt: copy field value to another field**: create a View of the content type with a Views Bulk Operations field, select the rows, pick `field_secret` as the source and `field_secret_enc` as the destination, and run it. (Leave *Overwrite* unticked - rows whose destination already holds a value are skipped, so re-running is safe.) For a few rows you can simply open each node and paste the value into the new field and save, or run a short script (via the Devel module's *Execute PHP*, or `bee php-script`):
   ```php
   foreach (node_load_multiple(FALSE, array('type' => 'YOUR_TYPE')) as $node) {
     $lang = LANGUAGE_NONE;
     if (!empty($node->field_secret[$lang][0]['value'])) {
       // Presave encrypts the new field automatically on save.
       $node->field_secret_enc[$lang][0]['value'] = $node->field_secret[$lang][0]['value'];
       node_save($node);
     }
   }
   ```

   The copy method does not matter - encryption is not tied to how the value arrives. It happens in `hook_field_attach_presave()` when the entity is saved, so *anything* that writes the plaintext into the encrypted-widget field and then saves the node will encrypt it: the bundled copy action, manual edits, the script above, a VBO + Rules component, Feeds, or Migrate. Use whichever you are comfortable with.
4. **Verify - this is the whole point:**
   * Look in the database (Adminer / phpMyAdmin, or `bee sql-query`) at table `field_data_field_secret_enc`: the value column now begins with `enc1:` (ciphertext), while your original `field_data_field_secret` is still plaintext. The same value, encrypted and not, side by side.
   * View a node with the *masked with reveal* formatter and click **Reveal** - the decrypted value matches the original plaintext exactly.
   * Log in as a user **without** the `view encrypted fields` permission: confirm they see only the mask, and that saving one of their edits does not corrupt the stored value.
   * Temporarily remove the key (rename it in `settings.php`) and reload any admin page - the **missing-key alarm** should fire. Restore the key and decryption returns.
   * Try a rotation (add a second slot, set it active, run Hard Rotate) and confirm the data re-encrypts to `enc2:` and still reads back correctly.

**When you are satisfied**

For the real switch you normally do not keep two fields - you encrypt the field in place, with a temporary plaintext backup as your safety net:

1. Create a backup field of the **same type** (e.g. `field_secret_saved`) with an ordinary text widget, and run the VBO action **Acuity Encrypt: copy field value to another field** with `field_secret` as source and `field_secret_saved` as destination. Your plaintext now survives whatever happens next.
2. Set the existing field's widget to **Acuity Encrypt (masked text)**.
3. In the same View, run **Acuity Encrypt: encrypt existing field values**. This resaves each entity, encrypting existing plaintext in place (already-encrypted values are skipped).
4. Confirm `enc1:` ciphertext in `field_data_field_secret`, reveal a few values, and compare them against the backup field. If anything went wrong, restore by running the copy action in reverse (`field_secret_saved` → `field_secret`, with *Overwrite* ticked) - the restored values are re-encrypted on save. With the *masked with reveal button* formatter, the View itself shows your progress: rows already encrypted show the "Encrypted" toggle, rows still awaiting encryption show their value as plain text.
5. When you are confident, **securely dispose of the plaintext**: delete the backup field, and remember that **old database backups still contain the plaintext** - retire or rotate them per your data-retention policy.

> Note: the copy action refuses to copy ciphertext. If you run it on an encrypted source as a user without the `view encrypted fields` permission (or with the key missing), affected entities are skipped and logged rather than filling your backup field with unreadable ciphertext.

## Views Bulk Operations Actions

Two entity actions ship with this module (both need the Views Bulk Operations module and a View with a VBO field):

**Acuity Encrypt: encrypt existing field values** - resaves each selected entity so the presave hook encrypts any plaintext in fields using the Acuity Encrypt widget. Already-encrypted values are skipped, and fields with ordinary widgets are never touched. Use it after switching an existing field's widget to encrypt its historical data in place.

**Acuity Encrypt: copy field value to another field** - copies one text field's values into another for every selected row. Designed as the safety net around encryption migrations (see the walkthrough above), but it is a general field-to-field copy:

* **Source and destination** dropdowns list text fields that exist on the content types of the rows you actually selected - a View filtered to one content type offers only that type's fields. Both fields must be the same field type (`text`, `text_long`, or `text_with_summary`); create the backup field with the same type as the original.
* **Overwrite** checkbox (off by default): rows whose destination already holds a value are skipped, so re-running the backup is always safe. Tick it for restores.
* **Direction-agnostic:** backup (plaintext → plaintext) and restore (backup → encrypted field) are the same action with the fields swapped - whether a value ends up encrypted is decided by the destination field's widget when the entity saves.
* **Skips rather than corrupts** (each skip logged to the watchdog): a source value still in ciphertext (you lack `view encrypted fields`, or the key is missing), a value too long for a single-line destination column (encryption overhead included), or either field missing from that row's content type.
* Copies the value, summary, and text format; values beyond the destination's number-of-values limit are dropped with a warning.

Both actions update each saved entity's *changed* timestamp. Neither can encrypt a narrower set than "every encrypt-widget field on the entity" - any save encrypts them all by design, so per-field migration control comes from switching one field's widget at a time.

## Key Rotation

Rotation moves all encrypted data from one key to another with no downtime:

1. **Add a new slot** - View keys tab → *Add key slot*, with its own storage method.
2. **Set it active** - new encryptions immediately use the new slot; existing data stays readable because every ciphertext carries its slot id.
3. **Hard Rotate** - a batch re-encrypts every old-slot value (revisions included) under the active slot.
4. When the old slot shows **Available** (zero entries), retire its key.

The slot table shows live "encrypted entries" counts - stored field values including revision copies, not a count of keys. Never delete a key while its slot shows **In use** or **Missing**.

### Slot ids, old backups, and key reuse

The stale-ciphertext edge cases have been thought through, and they fail **safe**:

* **A wrong key can never produce wrong plaintext.** Every value is AES-GCM authenticated - decrypting `enc1:` data with a different slot-1 key fails tag verification and renders the mask, never garbage or another value's content.
* **Restored old backups:** a database restore can resurrect `enc{N}:` values belonging to a slot you have since retired. If that slot id is no longer in the registry, the View Keys page flags them as **orphans**; re-add the slot with its *original* key and the data decrypts again. This is why you should keep every retired key in your password manager permanently - a key is only truly disposable when no backup you might ever restore contains its ciphertext.
* **The residual gap (planned fix):** if a slot id is *reused* with a different key while stale data encrypted under the old key exists (a hand-edited registry, or an old backup restored after the id was reused), those values are attributed to the new key instead of being flagged as orphans - undecryptable, but not alarmed. Slot ids are therefore planned to become strictly monotonic (never reused, checked against the highest id present in the data) - see the TODO. Until then: never hand-edit slot ids in `acuity_encrypt.slots.json`, and treat retired keys as permanent archive material.

## Storage Overhead

Encrypted values are stored as `enc{slot}:` + base64(IV + tag + ciphertext):

    stored length ≈ (plaintext bytes × 1.34) + 45

Roughly +33% for long text; proportionally more for short values. A default 255-character single-line text field holds at most ~158 bytes of plaintext - the module enforces this at input and at save (see Features).

## Important Notes for Site Builders

* **Switching the widget away** from Acuity Encrypt while the field has data leaves raw ciphertext displaying on the site. Re-enabling the widget restores normal behaviour.
* **Views, tokens, and Rules receive plaintext only for users with `view encrypted fields`** - for anyone else they receive ciphertext. Still combine this with Backdrop's role permissions and Views access rules: any user *with* the permission sees plaintext through core formatters and Views, not just through this module's masked formatters.
* **Encrypted fields are not searchable.** Search indexing runs unprivileged during cron and receives ciphertext. If you enabled encryption on a site that had already indexed those fields, rebuild the search index (Admin → Configuration → Search settings → Re-index site) to flush previously indexed plaintext.
* **Content types with encrypted fields are not entity-cached** (automatic, see Features). On busy sites this is a modest performance cost - the price of keeping plaintext out of cache tables.
* **Text format / HTML is preserved** through the encrypt/decrypt cycle.
* **Empty encrypted fields accept input from any user who can edit the node - by design (for now).** On a node/add form (or any empty encrypted field), users *without* `view encrypted fields` get a normal input: they can enter an initial value, which is encrypted on save, and from then on they can never view or change it (write-once-then-locked). There is no confidentiality leak - an empty field holds no secret - but if you prefer that unprivileged users cannot enter anything at all, note that a **per-field widget setting ("allow initial entry" vs "locked") is planned and on the TODO list**, with *locked* as the intended default. Until then, grant node-edit access accordingly, and be aware that locking a *required* encrypted field would prevent users without the permission from creating that content type at all.

## Encrypted Fields in Views

Views renders content fields through the same field formatters, so encrypted fields work in Views - with a few rules:

* **Always select one of this module's formatters** for encrypted fields in a view (*masked with reveal button*, *plain decrypted text*, or *summary or trimmed*). Core formatters are safe but ugly for unauthorised viewers: they render the raw ciphertext string rather than a mask. In particular, use **Acuity Encrypt: summary or trimmed** instead of core's *Summary or trimmed* on `text_with_summary` fields.
* **Hiding the field for unauthorised viewers:** tick the formatter setting **"Hide completely for viewers without the view encrypted fields permission"** and set the Views field's **No results behavior → Hide if empty**. The field, its label, and its row space all disappear for viewers without the permission; permitted viewers see it normally.
* **No filtering, sorting, grouping, or exposed search on encrypted fields.** The database column holds ciphertext, so any SQL-level operation (Views filters/sorts, `LIKE` queries, aggregation) operates on the ciphertext and returns meaningless results. Filter and sort on unencrypted fields (title, dates, taxonomy) instead.
* **Row access is unchanged:** Views access settings, node access, and role permissions apply as usual - the `view encrypted fields` permission only controls whether the *content* of encrypted fields is decrypted for the viewer.

## Threat Model - Encryption at Rest, Not End-to-End

**This module provides encryption at rest. It is not end-to-end (E2E) or "zero-knowledge" encryption.** Understanding the difference matters before you decide what to store in an encrypted field.

What "encryption at rest" protects: the encryption key lives only on the server (in `settings.php`, a private file, or an external file), never in the database. So a **stolen database dump, a leaked backup, or an attacker with SQL-only access sees ciphertext and nothing else**. This is the right protection for the large majority of sensitive-data obligations (GDPR / UK DPA / DUAA) - personal data, contact details, notes, reference numbers, client documents' metadata.

What it deliberately does **not** protect against, by design:

* **A fully compromised web server.** The running site must be able to decrypt in order to display content, so it holds the key in memory. Anyone who fully compromises the server (root/PHP execution) can read the key and therefore the data. No server-side encryption scheme can avoid this - if the server can show plaintext to a user, a server-level attacker can obtain it too.
* **The server seeing plaintext during normal operation.** When an authorised user views or edits content, plaintext exists transiently in server memory, in the rendered HTML sent to that user, and in the POST body when they save.
* **Hiding data from your own administrators.** Anyone with the key and the `view encrypted fields` permission - including site admins - can read every encrypted value. There is no per-user secret.

### Caveat - edit forms and the form cache (known, tracked)

There is one known place where an authorised user's **edit** form can put plaintext into the database briefly. It is disclosed here in full rather than glossed over.

When an authorised user opens a node edit form, the field is decrypted so they can edit it. If that same form also contains an **AJAX element - most commonly a file or image field** (their upload buttons use AJAX) - Backdrop caches the in-progress form to the `tempstore` database table so the AJAX callback can rebuild it. That cached copy includes the decrypted field value. The entry:

* only exists for forms that mix an encrypted field with an AJAX/file field;
* holds only the value that same user is already authorised to read;
* is removed automatically after its expiry (default ~6 hours).

It is a genuine at-rest exposure, but a narrow and transient one - a database dump would have to be taken during that window, while that specific form is open. **We investigated simply disabling the form cache; that is not viable - it would break the AJAX/file-upload elements whose presence is the only reason the caching happens.** The correct fix (keeping plaintext out of the cache entirely) is designed and on the roadmap for a release shortly after this beta.

**If this window is unacceptable for your data, you can reduce it now** by shortening the form cache lifetime in `settings.php`, e.g. `$settings['form_cache_expiration'] = 300;` (5 minutes), and/or by keeping encrypted fields on content types that do not also carry file/image fields until the fix ships.

**If you are storing information that must remain secret even from the server operator or a server-level compromise** - the classic examples being passwords/secrets a user should be able to store but nobody else (not even an admin) should ever read, or messages only the intended recipient may open - then you need **true end-to-end encryption**, where the decryption key is held by the user (a passphrase or client-held key) and the server never has it. That is a fundamentally different model with hard trade-offs: no server-side display, search, tokens, or Views output; no key rotation the server can perform; and no recovery if the user loses their passphrase (the data is simply gone). Because those trade-offs conflict with everything this module is designed to do, **E2E is explicitly out of scope here.**

We consider a dedicated E2E / zero-knowledge module a genuine but **fringe/edge-case** request. If you have a concrete need for it, we will consider building it as a separate module - please [open an issue](https://github.com/backdrop-contrib/acuity_encrypt/issues) to discuss - but note that, as specialised bespoke work outside this module's remit, such development **may be chargeable**.

## Developer API

```php
$ciphertext = acuity_encrypt_encrypt('secret value');        // active slot
$plaintext  = acuity_encrypt_decrypt($ciphertext);           // slot auto-detected
acuity_encrypt_is_encrypted($value);                         // strict ciphertext test
acuity_encrypt_get_slot($ciphertext);                        // int|NULL
acuity_encrypt_key_available();                              // bool
acuity_encrypt_max_plaintext_bytes($max_length);             // column fit limit

// Caller-supplied 32-byte key (no slot prefix on the wire format):
$ct = acuity_encrypt_encrypt_with_key($plaintext, $key32);
$pt = acuity_encrypt_decrypt_with_key($ct, $key32);

// Slot registry:
acuity_encrypt_slots();                // active slot + per-slot storage
acuity_encrypt_slot_value_counts();    // live per-slot entry counts
```

Ciphertext wire format: `enc{slot}:base64( iv[12] . tag[16] . ciphertext )`

## Issues

Bugs and feature requests should be reported in the Issue Queue: https://github.com/backdrop-contrib/acuity_encrypt/issues

## Current Maintainer(s)
- Steve Moorhouse (albanycomputers) (https://github.com/albanycomputers)
- Additional maintainers and contributors welcome.

## Planned Features

The following are on the roadmap but not yet implemented:

* **File encryption (Phase 3):** Envelope encryption for uploaded files - per-file key wrapped by the active slot key, so rotation re-wraps tiny keys instead of re-encrypting gigabytes. Design agreed; see module notes.
* **Field UI warning** when switching a field away from the encrypt widget while it holds ciphertext.
* **Per-field "last encrypted" timestamp** for audit trails.
* **BEE rotate command** for running hard rotation from the command line.
* **Pluggable key storage providers (on request):** Refactor the per-slot storage methods into a provider hook so add-on modules can fetch keys from other locations - an environment variable, an authenticated HTTPS endpoint on a separate server, or a cloud secrets manager (AWS Secrets Manager, HashiCorp Vault). Remote storage means the key never lives on the same server as the data. Not currently scheduled - if your hosting setup needs this, please open an issue and it will be prioritised. **Caution:** any remote provider involves access credentials; keep them in `settings.php` or server environment variables only - never in CMI config, and never committed to version control.

## Credits
- Steve Moorhouse - Zulip (DrAlbany)
- Assisted by AI.

- Current development is sponsored by [Albany Computer Services](https://www.albany-computers.co.uk), providers of computer support, [web design](https://www.albanywebdesign.co.uk), and [web hosting](https://www.albany-hosting.co.uk).

## License
This project is GPL v2 or later software. See the LICENSE.txt file in this directory for complete text.
