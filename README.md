# Encryption (Acuity Utils)

AES-256-GCM field encryption for Backdrop CMS using PHP's built-in OpenSSL
extension. No external library dependencies.

Encryption is added to any existing standard text field by selecting a custom
widget in Field UI — no new field type, no data migration, no schema changes.

## Features

- **Zero external dependencies** — uses PHP's built-in `openssl_encrypt()`.
- **AES-256-GCM** — authenticated encryption; tampered or corrupted ciphertext
  is detected and rejected automatically.
- **Encrypt any text field** — works on `text`, `text_long`, and
  `text_with_summary` field types. Enable by selecting the widget in Manage
  Fields; disable by switching back. Existing fields can be encrypted in-place.
- **WYSIWYG support** — long text fields retain full CKEditor support. An overlay
  mask hides the content until the author clicks "Reveal to edit".
- **Click-to-reveal display** — shows `••••••` by default; a Reveal button
  decrypts and shows the value in-page without a page reload.
- **Permission-aware** — users without `view encrypted fields` see a locked
  `••••••` placeholder on both display and edit. Their saves preserve the
  encrypted value (no accidental overwrites).
- **Transparent to consumers** — Views, tokens, search indexes, and Rules all
  receive the decrypted plaintext automatically. Encryption is at-rest
  protection (database dumps), not an access control mechanism.
- **VBO bulk encryption** — Views Bulk Operations action to re-save entities in
  bulk, encrypting any existing plain text values in one operation.
- **Public API** — `acuity_encrypt_encrypt()` / `acuity_encrypt_decrypt()` for
  use by other modules (e.g. `acuity_secure_message`).
- **GDPR / UK DPA 2018** — database dumps without the key expose no sensitive
  personal data.

## Requirements

- Backdrop CMS 1.x
- PHP 8.0+
- PHP `openssl` extension (enabled by default in most PHP installs)
- Views Bulk Operations module (optional — only needed for the bulk encryption action)

## Installation

1. Place the `acuity_encrypt` directory in your `modules/` folder.
2. Enable via **Administration › Modules**.
3. Visit **Administration › Configuration › Acuity Utils › Encryption Settings**
   and configure an encryption key (see Key Setup below).
4. Grant the `view encrypted fields` permission to trusted roles.

## Setting up an encrypted field

### Step 1 — Select the widget

Go to **Administration › Structure › Content types › [Type] › Manage fields**.

In the **Widget** column for any text, long text, or text with summary field,
change the widget to **Acuity Encrypt (masked text)**. Save.

That field is now encrypted. All future saves will encrypt the value. Existing
values in the database remain plaintext until you run the VBO bulk action.

### Step 2 — Select the display formatter

Go to **Manage display** for the content type.

Change the formatter for the encrypted field to **Acuity Encrypt: masked with
reveal button**. Save.

The field will show `••••••` on display, with a Reveal button for users who
have the `view encrypted fields` permission.

---

## Key Setup

The encryption key is **never stored in the database or config exports**. Choose
one of three storage methods at **Administration › Configuration › Acuity Utils
› Encryption Settings › Set keys**. Full step-by-step instructions are shown
on the page for each option.

### Option 1 — settings.php (recommended)

Add to your site's `settings.php` (in the root folder of your Backdrop site):

```php
$settings['acuity_encrypt_key1'] = 'replace-with-a-long-random-string';
```

Use the **Generate Key** button on the admin page to produce a
cryptographically random key and a ready-to-paste `$settings[...]` line.

This key never touches the database or config exports. Keep a separate copy
somewhere safe (a password manager such as Bitwarden is recommended).

### Option 2 — private filesystem

The admin page writes the key to `private://keys/acuity_encrypt_key1.key`.
The private files directory must be outside the webroot.

### Option 3 — external file

Place the key file anywhere on the server outside the webroot and enter the
absolute path on the admin page. The path is stored in config; the key is not.

### Key naming convention

| Slot | settings.php key | File name |
|------|-----------------|-----------|
| 1 | `acuity_encrypt_key1` | `acuity_encrypt_key1.key` |
| 2 | `acuity_encrypt_key2` | `acuity_encrypt_key2.key` |

### Key backup

**Back up your key separately from your database and config exports.**
A database dump without the key is unreadable — which is the point — but
losing the key makes all encrypted values permanently unrecoverable.

The **View keys** tab on the admin page reveals your active key behind a click
and prompts you to save it to a password manager.

---

## Encrypting existing plain text data

Use the included VBO bulk action after enabling the encrypt widget on a field
that already has data:

1. Create a View of the content type, add the **Bulk operations** field.
2. Select all records, choose **Acuity Encrypt: encrypt existing field values**.
3. Run the batch. Each record is re-saved; `hook_field_attach_presave()` encrypts
   any plain text values in fields using the encrypt widget.
4. Verify ciphertext (`enc1:...`) in the database.

Note: re-saving updates the entity's `changed` timestamp.

---

## Permissions

| Permission | Purpose |
|---|---|
| `administer acuity_encrypt` | Access the key settings page |
| `view encrypted fields` | Decrypt and reveal values on display and in edit forms |

Both have `restrict access: TRUE` — not granted to any role by default.

---

## Important notes for site builders

**Switching the widget away from Acuity Encrypt** while the field has data
will leave ciphertext (`enc1:...`) in the database. That ciphertext will be
displayed raw by the standard text formatter. Re-enabling the widget restores
normal behaviour.

**Views, tokens, and search** receive decrypted plaintext. This is intentional —
the encryption protects data at rest (database dumps), not data in use. Use
Backdrop's role permissions and Views access rules to control who sees the
content on screen.

**Text format / HTML** is preserved through the encrypt/decrypt cycle. CKEditor
output is encrypted as a string and decrypted back to the same HTML.

---

## Admin page

**Administration › Configuration › Acuity Utils › Encryption Settings**
`admin/config/acuity-utils/acuity_encrypt`

Two tabs:
- **View keys** — slot table showing storage method, key identifier, and status.
- **Set keys** — accordion form for configuring a key via any storage method.

---

## Developer API

```php
// Encrypt any string (uses slot 1 by default).
$ciphertext = acuity_encrypt_encrypt('secret value');
// Returns: 'enc1:base64encodeddata...' or FALSE on failure.

// Decrypt. Slot is read from the ciphertext prefix automatically.
$plaintext = acuity_encrypt_decrypt($ciphertext);
// Returns: original string, or FALSE if key is wrong / data tampered.

// Test whether a value is already encrypted.
if (acuity_encrypt_is_encrypted($value)) {
  $plaintext = acuity_encrypt_decrypt($value);
}

// Check key availability before attempting encryption.
if (!acuity_encrypt_key_available()) {
  backdrop_set_message(t('Encryption key not configured.'), 'warning');
}

// Read the slot number from a ciphertext string.
$slot = acuity_encrypt_get_slot($ciphertext); // int or NULL
```

### Ciphertext format

```
enc{slot}:base64( iv[12 bytes] . tag[16 bytes] . ciphertext )
```

Example: `enc1:AAECAwQFBgcICQoLDA0OD...`

The slot prefix allows future key rotation: old ciphertext carries its slot so
the correct key is always used automatically on decryption, even after a new
active slot is set.

---

## License

GPL-2.0-or-later. See LICENSE.txt.

## Maintainer

Albany Computer Services — https://www.albany-computers.co.uk
