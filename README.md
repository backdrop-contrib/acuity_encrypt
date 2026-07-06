# Encryption (Acuity Utils)
**Acuity Encrypt** — AES-256-GCM field encryption for Backdrop CMS with multi-slot key management and zero-downtime key rotation.

Acuity Encrypt is a Backdrop CMS module that adds at-rest encryption to any standard text field using PHP's built-in OpenSSL extension — no external libraries. Encryption is enabled by selecting a custom widget in Field UI: no new field type, no data migration, no schema changes. Values are decrypted transparently on load, so Views, tokens, and search keep working, while database dumps expose nothing without the key. A public API (including caller-supplied-key variants) powers the companion `acuity_secure_link` and `acuity_secure_message` modules.

## Beta Release Notes
As a beta release, the encryption core, field widget/formatters, key slot management, and hard rotation are fully functional and verified by CLI test harnesses against a live site (including rotation forward and back on real content with revisions). Browser click-through of the newer slot admin pages is still in progress. Before deploying to production, ensure your Backdrop CMS environment is running PHP 8.0+, clear your system caches after installation, and **back up your encryption key to a password manager before encrypting anything** — a lost key means permanently unreadable data.

## Features

* **Encrypt any text field:** Works on `text`, `text_long`, and `text_with_summary` field types. Enable by selecting the **Acuity Encrypt (masked text)** widget in Manage Fields; disable by switching back. Existing fields can be encrypted in-place.
* **AES-256-GCM authenticated encryption:** Fresh random IV per value, 16-byte authentication tag — tampered or corrupted ciphertext is detected and rejected automatically. Zero external dependencies (PHP's built-in `openssl`).
* **Multi-slot key management:** Multiple key slots, each with its own independent storage method. Every ciphertext carries its slot id (`enc1:`, `enc2:` …) so the correct key is always used automatically, even mid-rotation.
* **Zero-downtime key rotation:** Add a new slot, set it active (new encryptions switch instantly; old data still decrypts), then run the Hard Rotate batch to re-encrypt everything — **including revisions** — under the new key. The slot table's live entry counts tell you exactly when the old key is safe to retire.
* **Missing-key alarm:** If encrypted data exists whose key slot has no key configured, a prominent error banner appears on every admin page until it is restored.
* **Keys never touch the database:** Three storage methods per slot — `settings.php` (recommended), private filesystem, or an external file outside the webroot. Keys never appear in CMI config exports or database dumps.
* **Click-to-reveal display:** Shows `••••••••` by default with a Reveal button that decrypts in-page. Users without the `view encrypted fields` permission see a locked placeholder on both display and edit, and their saves preserve the encrypted value.
* **WYSIWYG support:** Long text fields retain full CKEditor support behind a "Reveal to edit" overlay mask.
* **Encrypted-length guard:** Encryption inflates storage (~1.34 × plaintext + ~45 characters). The widget shows the effective limit, caps input, and byte-validates on submit; programmatic saves that would overflow the column are blocked with a clear exception instead of silently truncating into undecryptable data.
* **Transparent to consumers:** Views, tokens, search indexes, and Rules all receive decrypted plaintext automatically. Encryption is at-rest protection (database dumps), not an access control mechanism.
* **VBO bulk encryption:** Views Bulk Operations action to encrypt existing plain text values in one batch.
* **Public API:** `acuity_encrypt_encrypt()` / `acuity_encrypt_decrypt()`, plus `_with_key()` variants for caller-supplied 32-byte keys (used by `acuity_secure_link` for token-derived per-link encryption).
* **GDPR / UK DPA 2026:** Database dumps without the key expose no sensitive personal data.
* **Backdrop Native:** Built exclusively for Backdrop CMS using strict PHP 8.0+ standards and Backdrop APIs throughout.

## Requirements

- Backdrop CMS 1.x
- PHP 8.0+ (While the code may technically function with PHP 7.4 at this time, we strictly require PHP 8.0+ and will not address issues related to older PHP versions.)
- PHP `openssl` extension (enabled by default in most PHP installs)
- Views Bulk Operations module (optional — only needed for the bulk encryption action)

## Installation

Install this module using the official Backdrop CMS instructions at https://docs.backdropcms.org/documentation/extend-with-modules

Enable the module, then clear your system caches to register the admin menu items.

## Configuration

1. Navigate to **Admin → Configuration → Acuity Utils → Encryption Settings → Set keys** and configure a key for Slot 1 using one of the three storage methods. Each section has inline step-by-step instructions and a **Generate key** button.
2. **Back up the key immediately** using the reveal panel — copy it to a password manager (e.g. Bitwarden). A database backup without the key is unreadable; losing the key makes all encrypted values permanently unrecoverable.
3. Grant the `view encrypted fields` permission to trusted roles at **Admin → People → Permissions**. Neither of this module's permissions is granted to any role by default.
4. Go to **Admin → Structure → Content types → [Type] → Manage fields** and change the widget of any text field to **Acuity Encrypt (masked text)**. All future saves encrypt the value.
5. Under **Manage display**, select **Acuity Encrypt: masked with reveal button** (or the plain decrypted formatter) for the field.
6. To encrypt values that existed before you enabled the widget, run the included VBO action **Acuity Encrypt: encrypt existing field values** from a View with bulk operations, then verify `enc1:` ciphertext in the database.

## Key Rotation

Rotation moves all encrypted data from one key to another with no downtime:

1. **Add a new slot** — View keys tab → *Add key slot*, with its own storage method.
2. **Set it active** — new encryptions immediately use the new slot; existing data stays readable because every ciphertext carries its slot id.
3. **Hard Rotate** — a batch re-encrypts every old-slot value (revisions included) under the active slot.
4. When the old slot shows **Available** (zero entries), retire its key.

The slot table shows live "encrypted entries" counts — stored field values including revision copies, not a count of keys. Never delete a key while its slot shows **In use** or **Missing**.

## Storage Overhead

Encrypted values are stored as `enc{slot}:` + base64(IV + tag + ciphertext):

    stored length ≈ (plaintext bytes × 1.34) + 45

Roughly +33% for long text; proportionally more for short values. A default 255-character single-line text field holds at most ~158 bytes of plaintext — the module enforces this at input and at save (see Features).

## Important Notes for Site Builders

* **Switching the widget away** from Acuity Encrypt while the field has data leaves raw ciphertext displaying on the site. Re-enabling the widget restores normal behaviour.
* **Views, tokens, and search receive plaintext** by design — use Backdrop's role permissions and Views access rules to control who sees content on screen. The `view encrypted fields` permission gates only this module's own widget and formatters.
* **Text format / HTML is preserved** through the encrypt/decrypt cycle.

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

* **File encryption (Phase 3):** Envelope encryption for uploaded files — per-file key wrapped by the active slot key, so rotation re-wraps tiny keys instead of re-encrypting gigabytes. Design agreed; see module notes.
* **Field UI warning** when switching a field away from the encrypt widget while it holds ciphertext.
* **Summary masking** in the edit widget for `text_with_summary` fields (the summary currently renders unmasked in the edit form).
* **Per-field "last encrypted" timestamp** for audit trails.
* **BEE rotate command** for running hard rotation from the command line.

## Credits
- Steve Moorhouse — Zulip (DrAlbany)
- Assisted by AI.

- Current development is sponsored by [Albany Computer Services](https://www.albany-computers.co.uk), providers of computer support, [web design](https://www.albanywebdesign.co.uk), and [web hosting](https://www.albany-hosting.co.uk).

## License
This project is GPL v2 or later software. See the LICENSE.txt file in this directory for complete text.
