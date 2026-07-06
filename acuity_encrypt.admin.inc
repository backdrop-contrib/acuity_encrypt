<?php

/**
 * @file
 * Admin settings page for Acuity Encrypt.
 *
 * Three self-contained collapsible sections — one per key storage method.
 * Each section has its own Save button and inline step-by-step instructions.
 * The admin never needs to consult external docs or a README.
 */

// ── Private file helpers ──────────────────────────────────────────────────────

/**
 * Returns the private:// URI for the key file for the given slot.
 *
 * @param int $slot  Key slot number (default 1).
 */
function _acuity_encrypt_private_key_uri(int $slot = 1): string {
  return 'private://keys/acuity_encrypt_key' . $slot . '.key';
}

/**
 * Returns the real filesystem path for the private key file, or FALSE.
 *
 * @param int $slot  Key slot number (default 1).
 */
function _acuity_encrypt_private_key_realpath(int $slot = 1): string|false {
  return backdrop_realpath(_acuity_encrypt_private_key_uri($slot)) ?: FALSE;
}

/**
 * Returns TRUE if the private files directory is safely outside the webroot.
 */
function _acuity_encrypt_private_dir_is_safe(): bool {
  $private_real = backdrop_realpath('private://');
  $webroot      = backdrop_realpath(BACKDROP_ROOT);

  if (!$private_real || !$webroot) {
    return FALSE;
  }
  return strpos($private_real, $webroot) !== 0;
}

/**
 * Returns the raw (pre-hash) key string for the backup reveal panel.
 *
 * Reads the ACTIVE slot's key from whichever storage that slot uses.
 * Returns NULL if no key is configured.
 */
function _acuity_encrypt_get_raw_key_for_display(): string|null {
  global $settings;

  $slot = acuity_encrypt_active_slot();
  $registry = acuity_encrypt_slots();

  if (!empty($settings['acuity_encrypt_key' . $slot])) {
    return $settings['acuity_encrypt_key' . $slot];
  }

  $info = $registry['slots'][$slot] ?? NULL;
  if ($info === NULL) {
    return NULL;
  }

  if ($info['storage'] === 'external_file') {
    $path = $info['external_path'];
    if (!empty($path) && is_readable($path)) {
      return trim(file_get_contents($path)) ?: NULL;
    }
    return NULL;
  }

  if ($info['storage'] === 'private_file') {
    $realpath = backdrop_realpath('private://keys/acuity_encrypt_key' . $slot . '.key');
    if ($realpath && is_readable($realpath)) {
      return trim(file_get_contents($realpath)) ?: NULL;
    }
    return NULL;
  }

  return NULL;
}

// ── Reveal-key AJAX callback ──────────────────────────────────────────────────

/**
 * Page callback: returns the raw active key as JSON.
 *
 * Only reachable by users with 'administer acuity_encrypt' (menu access) and
 * a valid per-session CSRF token, so the key is never present in the initial
 * page markup — it is fetched on demand only when "Reveal key" is clicked.
 * The 'delivery callback' of backdrop_json_output() (set in hook_menu())
 * serializes the return value.
 */
function acuity_encrypt_reveal_key_callback(): array {
  $token = isset($_GET['token']) ? $_GET['token'] : '';
  if (!backdrop_valid_token($token, 'acuity_encrypt_reveal_key')) {
    return ['error' => t('Invalid or expired request.')];
  }

  $raw_key = _acuity_encrypt_get_raw_key_for_display();
  if ($raw_key === NULL) {
    return ['error' => t('No key available.')];
  }

  return ['key' => $raw_key];
}

// ── Key slots overview page ───────────────────────────────────────────────────

/**
 * Returns the human label + identifier markup for a slot's storage.
 */
function _acuity_encrypt_slot_storage_cells(int $slot_id, array $info): array {
  $method_labels = [
    'settings_php'  => t('settings.php'),
    'private_file'  => t('Private files'),
    'external_file' => t('External file'),
  ];
  $method_label = $method_labels[$info['storage']] ?? $info['storage'];

  switch ($info['storage']) {
    case 'settings_php':
      $identifier = '<code>$settings[\'acuity_encrypt_key' . $slot_id . '\']</code>';
      break;

    case 'private_file':
      $uri = 'private://keys/acuity_encrypt_key' . $slot_id . '.key';
      $realpath = backdrop_realpath($uri);
      $identifier = '<code>' . check_plain($realpath ?: $uri) . '</code>';
      break;

    case 'external_file':
      $identifier = !empty($info['external_path'])
        ? '<code>' . check_plain($info['external_path']) . '</code>'
        : '<em>' . t('(path not configured)') . '</em>';
      break;

    default:
      $identifier = '<em>' . t('Unknown') . '</em>';
  }
  return [$method_label, $identifier];
}

/**
 * Page callback for the "View keys" tab.
 *
 * Shows every registered key slot with its storage, live encrypted-value
 * count (current + revision rows), and derived status. Visiting this page
 * also refreshes the missing-key warning state.
 */
function acuity_encrypt_keys_overview(): array {
  $registry = acuity_encrypt_slots();
  $counts = acuity_encrypt_slot_value_counts();
  $active = acuity_encrypt_active_slot();

  $status_labels = [
    'active'       => '<span class="acuity-status-ok">&#10003; ' . t('Active') . '</span>',
    'in_use'       => '<span class="acuity-status-ok">' . t('In use') . '</span>',
    'available'    => t('Available'),
    'missing'      => '<span class="acuity-status-missing">&#10007; ' . t('KEY MISSING — data at risk') . '</span>',
    'unconfigured' => '<em>' . t('No key configured') . '</em>',
  ];

  $rows = [];
  foreach ($registry['slots'] as $slot_id => $info) {
    $status = acuity_encrypt_slot_status($slot_id, $counts);
    list($method_label, $identifier) = _acuity_encrypt_slot_storage_cells($slot_id, $info);

    $operations = [];
    $operations[] = l(t('Edit'), 'admin/config/acuity-utils/acuity_encrypt/slots/' . $slot_id . '/edit');
    if ($slot_id !== $active && in_array($status, ['in_use', 'available'], TRUE)) {
      $operations[] = l(t('Set active'), 'admin/config/acuity-utils/acuity_encrypt/slots/' . $slot_id . '/activate');
    }

    $rows[] = [
      ['data' => $slot_id],
      ['data' => check_plain($info['label'])],
      ['data' => $method_label],
      ['data' => $identifier],
      ['data' => (string) ($counts['slots'][$slot_id] ?? 0)],
      ['data' => $status_labels[$status]],
      ['data' => implode(' · ', $operations)],
    ];
  }

  $header = [
    t('Slot'),
    t('Label'),
    t('Storage method'),
    t('Key identifier'),
    t('Encrypted entries'),
    t('Status'),
    t('Operations'),
  ];

  $build = [];

  $build['#attached'] = [
    'css' => [backdrop_get_path('module', 'acuity_encrypt') . '/css/acuity_encrypt.css'],
  ];

  if ($counts['orphan'] > 0) {
    $build['orphan_warning'] = [
      '#markup' => '<p class="messages error">' . t(
        '@count encrypted value(s) reference a key slot that is not in the registry. Add the matching slot (with its original key) so this data can be decrypted.',
        ['@count' => $counts['orphan']]
      ) . '</p>',
    ];
  }

  $build['table'] = [
    '#theme'      => 'table',
    '#header'     => $header,
    '#rows'       => $rows,
    '#attributes' => ['class' => ['acuity-encrypt-keys-table']],
  ];

  $build['explain'] = [
    '#markup' => '<p class="description" style="margin-top:1em">'
      . t('<strong>Active</strong> — all new encryptions use this slot. '
        . '<strong>In use</strong> — holds data; its key must remain configured. '
        . '<strong>Available</strong> — key configured, zero entries; safe to retire or reuse. '
        . '"Encrypted entries" counts stored field values protected by the slot\'s key — one field on many nodes counts many entries, and revision copies are included. It is not a count of keys (each slot holds exactly one key).')
      . '</p>',
  ];

  $links = [
    l(t('+ Add key slot'), 'admin/config/acuity-utils/acuity_encrypt/slots/add'),
    l(t('Rotate keys »'), 'admin/config/acuity-utils/acuity_encrypt/rotate'),
    l(t('Guided Slot 1 setup »'), 'admin/config/acuity-utils/acuity_encrypt/settings'),
  ];
  $build['links'] = [
    '#markup' => '<p style="margin-top:0.8em">' . implode(' &nbsp;|&nbsp; ', $links) . '</p>',
  ];

  return $build;
}

// ── Main form ─────────────────────────────────────────────────────────────────

/**
 * Admin form: three collapsible sections, each self-contained with a Save button.
 */
function acuity_encrypt_admin_form(array $form, array &$_form_state): array {
  global $settings;

  $config         = config('acuity_encrypt.settings');
  $current_method = $config->get('key_storage_method') ?: 'settings_php';
  $external_path  = $config->get('external_file_path') ?: '';

  $key_available    = acuity_encrypt_key_available();
  $settings_php_key = !empty($settings['acuity_encrypt_key1']);

  // Attach admin JS + shared CSS.
  $form['#attached'] = [
    'js'  => [backdrop_get_path('module', 'acuity_encrypt') . '/js/acuity_encrypt.admin.js'],
    'css' => [backdrop_get_path('module', 'acuity_encrypt') . '/css/acuity_encrypt.css'],
  ];

  // ── Status banner ────────────────────────────────────────────────────────

  if ($key_available) {
    $source      = $settings_php_key ? t('settings.php') : t('the configured key file');
    $status_html = '<p class="messages status"><strong>' . t('Encryption is active.') . '</strong> '
      . t('A valid key was found in @source.', ['@source' => $source]) . '</p>';
  }
  else {
    $status_html = '<p class="messages error"><strong>' . t('No encryption key found — encryption is not active.') . '</strong> '
      . t('Expand one of the sections below, generate or enter a key, and click Save.') . '</p>';
  }
  $form['status_banner'] = ['#markup' => $status_html];

  // ── Key backup panel (visible only when a key is active) ─────────────────

  if ($key_available) {
    // The raw key is intentionally NOT embedded in this page's markup — it is
    // fetched via AJAX only when "Reveal key" is clicked, so it never sits in
    // page source, browser cache/history, or HTTP-layer logging by default.
    $token = backdrop_get_token('acuity_encrypt_reveal_key');

    $form['key_backup'] = [
      '#type'        => 'fieldset',
      '#title'       => t('Your encryption key — back this up now'),
      '#collapsible' => TRUE,
      '#collapsed'   => FALSE,
      '#description' => t(
        'This is your active encryption key. '
        . '<strong>Copy it to a password manager (e.g. <a href="https://bitwarden.com" target="_blank">Bitwarden</a>) before anything else.</strong> '
        . 'If the server fails and you have not backed this up, all encrypted data is gone forever.'
      ),
    ];
    $form['key_backup']['key_display'] = [
      '#markup' => '<div class="acuity-encrypt-wrap" data-key-url="'
        . check_plain(url('admin/config/acuity-utils/acuity_encrypt/reveal-key', ['query' => ['token' => $token]]))
        . '">'
        . '<span class="acuity-encrypt-masked">' . str_repeat('•', 40) . '</span>'
        . '<span class="acuity-encrypt-value" style="display:none;font-family:monospace;word-break:break-all"></span>'
        . '&nbsp;<button type="button" class="acuity-encrypt-reveal-btn button">'
        . t('Reveal key')
        . '</button>'
        . '&nbsp;<button type="button" class="acuity-copy-reveal-btn button" style="display:none" data-copy-target="reveal">'
        . t('Copy')
        . '</button>'
        . '<span class="acuity-copy-feedback" style="display:none;margin-left:8px;color:green">'
        . t('Copied!')
        . '</span>'
        . '</div>',
      '#attached' => [
        'js' => [backdrop_get_path('module', 'acuity_encrypt') . '/js/acuity_encrypt.js'],
      ],
    ];
  }

  // ── Section helpers ───────────────────────────────────────────────────────

  // Build short status badges for section titles.
  $sp_badge      = $settings_php_key ? ' ✓' : '';
  $private_real  = backdrop_realpath('private://');
  $priv_key_real = _acuity_encrypt_private_key_realpath();
  $priv_key_ok   = $priv_key_real && file_exists($priv_key_real);
  $priv_badge    = ($current_method === 'private_file' && $priv_key_ok) ? ' ✓' : '';
  $ext_badge     = ($current_method === 'external_file' && $key_available && !$settings_php_key && !$priv_key_ok) ? ' ✓' : '';

  // ════════════════════════════════════════════════════════════════════════════
  // SECTION 1 — settings.php
  // ════════════════════════════════════════════════════════════════════════════

  $form['section_settings_php'] = [
    '#type'        => 'fieldset',
    '#title'       => t('settings.php — recommended for most sites') . $sp_badge,
    '#collapsible' => TRUE,
    '#collapsed'   => TRUE,
    '#attributes'  => ['class' => ['acuity-encrypt-section']],
  ];

  $form['section_settings_php']['intro'] = [
    '#markup' => '<p>' . t(
      'You add one line to your site\'s <code>settings.php</code> file. '
      . 'The module reads the key from that file but <strong>never writes to it</strong>, '
      . 'so there is zero risk of the file being corrupted by the module. '
      . 'The key never appears in database backups or configuration exports.'
    ) . '</p>',
  ];

  // Live status.
  $sp_status = $settings_php_key
    ? '<p class="acuity-status-ok">&#10003; ' . t('Key found in settings.php — encryption is active.') . '</p>'
    : '<p class="acuity-status-missing">&#10007; ' . t('No key found in settings.php yet. Follow the steps below.') . '</p>';
  $form['section_settings_php']['key_status'] = ['#markup' => $sp_status];

  // Generate key widget.
  $form['section_settings_php']['generate'] = [
    '#markup' => '<div class="acuity-generate-wrap" data-mode="settings_php">'
      . '<p><strong>' . t('Step 1 — generate a key') . '</strong></p>'
      . '<p>' . t('Click the button to generate a secure random key. A ready-to-paste line will appear below.') . '</p>'
      . '<button type="button" class="button acuity-generate-key-btn">' . t('Generate key') . '</button>'
      . '<div class="acuity-generated-area" style="display:none;margin-top:1em">'
      . '<p><strong>' . t('Step 2 — copy this line') . '</strong></p>'
      . '<p class="description">' . t('This is the complete line to add to settings.php. Click Copy, then paste it at the bottom of the file.') . '</p>'
      . '<textarea class="acuity-key-line" readonly rows="2" style="width:100%;max-width:700px;font-family:monospace;font-size:0.85em"></textarea>'
      . '<br>'
      . '<button type="button" class="button acuity-copy-key-btn" style="margin-top:4px">' . t('Copy to clipboard') . '</button>'
      . '<span class="acuity-copy-feedback" style="display:none;margin-left:8px;color:green">' . t('Copied!') . '</span>'
      . '</div>'
      . '</div>',
  ];

  $form['section_settings_php']['steps'] = [
    '#markup' => '<div style="margin-top:1.2em"><p><strong>' . t('Step 3 — paste into settings.php') . '</strong></p>'
      . '<ol>'
      . '<li>' . t('Open <code>settings.php</code> from the <strong>root folder</strong> of your Backdrop site — for example <code>/home/username/public_html/settings.php</code>. <em>(Note: unlike Drupal, Backdrop keeps settings.php in the site root, not in sites/default.)</em>') . '</li>'
      . '<li>' . t('Scroll to the bottom of the file.') . '</li>'
      . '<li>' . t('Paste the copied line. If the file ends with <code>?></code>, paste <em>before</em> that line.') . '</li>'
      . '<li>' . t('Save settings.php.') . '</li>'
      . '<li>' . t('Click <strong>"Save — use settings.php"</strong> below. The status above will turn green.') . '</li>'
      . '</ol>'
      . '<p class="description">' . t('<strong>Key naming:</strong> The key is called <code>acuity_encrypt_key1</code> (slot 1). If you ever add a second key for rotation it will be <code>acuity_encrypt_key2</code>, and so on — always consistent.') . '</p>'
      . '</div>',
  ];

  $form['section_settings_php']['submit'] = [
    '#type'                  => 'submit',
    '#value'                 => t('Save — use settings.php'),
    '#name'                  => 'submit_settings_php',
    '#submit'                => ['acuity_encrypt_admin_settings_php_submit'],
    '#limit_validation_errors' => [],
  ];

  // ════════════════════════════════════════════════════════════════════════════
  // SECTION 2 — Private files
  // ════════════════════════════════════════════════════════════════════════════

  $form['section_private_file'] = [
    '#type'        => 'fieldset',
    '#title'       => t('Private files — easiest for managed hosting') . $priv_badge,
    '#collapsible' => TRUE,
    '#collapsed'   => TRUE,
    '#attributes'  => ['class' => ['acuity-encrypt-section']],
  ];

  $form['section_private_file']['intro'] = [
    '#markup' => '<p>' . t(
      'The module generates and saves the key for you inside the private files directory — '
      . 'a folder that Backdrop serves through PHP rather than directly through the web server. '
      . 'This means the key file cannot be downloaded via a browser URL. '
      . 'Ideal for managed or shared hosting where you cannot edit server configuration files.'
    ) . '</p>',
  ];

  // Private directory status.
  if (!$private_real) {
    $priv_dir_html = '<p class="acuity-status-missing">&#10007; '
      . t('The private files directory is not configured. Go to <a href="@url">File system settings</a> and set a private files path first.',
          ['@url' => url('admin/config/media/file-system')])
      . '</p>';
  }
  elseif (!_acuity_encrypt_private_dir_is_safe()) {
    $priv_dir_html = '<p class="acuity-status-missing">&#10007; '
      . t('Your private files directory (<code>@path</code>) is inside the webroot — key files there could be downloaded via a browser URL. Move the private files directory outside the webroot first.',
          ['@path' => $private_real])
      . '</p>';
  }
  else {
    $priv_dir_html = '<p class="acuity-status-ok">&#10003; '
      . t('Private files directory is outside the webroot (<code>@path</code>).', ['@path' => $private_real])
      . '</p>';
  }

  $priv_key_html = $priv_key_ok
    ? '<p class="acuity-status-ok">&#10003; ' . t('Key file exists at <code>@path</code>.', ['@path' => $priv_key_real]) . '</p>'
    : '<p class="acuity-status-missing">&#10007; ' . t('No key file yet — generate or paste one below.') . '</p>';

  $form['section_private_file']['dir_status'] = ['#markup' => $priv_dir_html];
  $form['section_private_file']['key_status'] = ['#markup' => $priv_key_html];

  // Generate key widget — generates client-side and populates the key field.
  $form['section_private_file']['generate'] = [
    '#markup' => '<div class="acuity-generate-wrap" data-mode="private_file" data-fill-field=".acuity-private-key-input">'
      . '<p><strong>' . t('Option A — let the module generate a key for you (recommended)') . '</strong></p>'
      . '<p>' . t('Click the button. A random key is generated in your browser and placed in the field below — it never travels to any server until you click Save.') . '</p>'
      . '<button type="button" class="button acuity-generate-key-btn">' . t('Generate key') . '</button>'
      . '<div class="acuity-generated-area" style="display:none;margin-top:0.8em">'
      . '<p class="description" style="color:#c00"><strong>' . t('Copy this key to your password manager NOW, before clicking Save.') . '</strong> '
      . t('Once saved to the filesystem, you can retrieve it from this page, but keep a copy somewhere safe regardless.') . '</p>'
      . '<div style="display:flex;align-items:center;gap:8px;flex-wrap:wrap">'
      . '<textarea class="acuity-key-line" readonly rows="1" style="flex:1;min-width:200px;font-family:monospace;font-size:0.85em"></textarea>'
      . '<button type="button" class="button acuity-copy-key-btn">' . t('Copy') . '</button>'
      . '<span class="acuity-copy-feedback" style="display:none;color:green">' . t('Copied!') . '</span>'
      . '</div>'
      . '</div>'
      . '</div>',
  ];

  // Key input field (visible textfield so admin can see/verify what is being saved).
  $form['section_private_file']['private_key_value'] = [
    '#type'        => 'textfield',
    '#title'       => t('Option B — paste your own key'),
    '#description' => t('If you already have a key you want to use, paste it here. Leave blank to keep the existing key unchanged (if one exists), or to use a key you generated with the button above.'),
    '#size'        => 70,
    '#maxlength'   => 512,
    '#attributes'  => [
      'autocomplete' => 'off',
      'class'        => ['acuity-private-key-input'],
    ],
  ];

  $priv_file_label = 'acuity_encrypt_key1.key';
  $form['section_private_file']['how_it_works'] = [
    '#markup' => '<div style="margin-top:0.8em" class="description">'
      . t('The key is saved to <code>private://keys/@filename</code> — outside the webroot, not accessible via a browser URL. '
        . 'File permissions are set to owner-read-only (0400).', ['@filename' => $priv_file_label])
      . '</div>',
  ];

  if ($priv_key_ok) {
    $form['section_private_file']['confirm_overwrite'] = [
      '#type'        => 'checkbox',
      '#title'       => t('I understand that if I save a different key here, any data already encrypted with the current key becomes permanently unreadable.'),
      '#description' => t('Only tick this if you are intentionally replacing the key. Back up the current key first (see the panel above).'),
    ];
  }

  $form['section_private_file']['submit'] = [
    '#type'                  => 'submit',
    '#value'                 => t('Save — use private files'),
    '#name'                  => 'submit_private_file',
    '#validate'              => ['acuity_encrypt_admin_private_file_validate'],
    '#submit'                => ['acuity_encrypt_admin_private_file_submit'],
    '#limit_validation_errors' => [['section_private_file', 'private_key_value'], ['section_private_file', 'confirm_overwrite']],
  ];

  // ════════════════════════════════════════════════════════════════════════════
  // SECTION 3 — External file
  // ════════════════════════════════════════════════════════════════════════════

  $form['section_external_file'] = [
    '#type'        => 'fieldset',
    '#title'       => t('External file — advanced, maximum control') . $ext_badge,
    '#collapsible' => TRUE,
    '#collapsed'   => TRUE,
    '#attributes'  => ['class' => ['acuity-encrypt-section']],
  ];

  $form['section_external_file']['intro'] = [
    '#markup' => '<p>' . t(
      'You create and manage the key file yourself, anywhere on the server. '
      . 'The module reads the file but never writes to it. '
      . 'Gives you full control — good for dedicated servers where you can place '
      . 'the key file on a separate volume or mount.'
    ) . '</p>',
  ];

  // External file status.
  $ext_file_status_html = '';
  if (!empty($external_path)) {
    if (!file_exists($external_path)) {
      $ext_file_status_html = '<p class="acuity-status-missing">&#10007; '
        . t('File not found at <code>@path</code>.', ['@path' => $external_path]) . '</p>';
    }
    elseif (!is_readable($external_path)) {
      $ext_file_status_html = '<p class="acuity-status-missing">&#10007; '
        . t('File exists but cannot be read by the web server at <code>@path</code>. Check file permissions.', ['@path' => $external_path]) . '</p>';
    }
    else {
      $webroot  = backdrop_realpath(BACKDROP_ROOT);
      $ext_real = realpath($external_path);
      if ($ext_real && strpos($ext_real, $webroot) === 0) {
        $ext_file_status_html = '<p class="acuity-status-missing">&#10007; '
          . t('Warning: the file at <code>@path</code> is inside the webroot and could be downloaded via a browser URL. Move it outside the webroot.', ['@path' => $external_path]) . '</p>';
      }
      else {
        $ext_file_status_html = '<p class="acuity-status-ok">&#10003; '
          . t('Key file found and readable at <code>@path</code>.', ['@path' => $external_path]) . '</p>';
      }
    }
  }
  if ($ext_file_status_html) {
    $form['section_external_file']['file_status'] = ['#markup' => $ext_file_status_html];
  }

  // Generate key widget for external file.
  $form['section_external_file']['generate'] = [
    '#markup' => '<div class="acuity-generate-wrap" data-mode="external_file">'
      . '<p><strong>' . t('Step 1 — generate a key') . '</strong></p>'
      . '<button type="button" class="button acuity-generate-key-btn">' . t('Generate key') . '</button>'
      . '<div class="acuity-generated-area" style="display:none;margin-top:0.8em">'
      . '<p class="description">' . t('Your key (copy this to your password manager):') . '</p>'
      . '<div style="display:flex;align-items:center;gap:8px;flex-wrap:wrap">'
      . '<textarea class="acuity-key-line" readonly rows="1" style="flex:1;min-width:200px;font-family:monospace;font-size:0.85em"></textarea>'
      . '<button type="button" class="button acuity-copy-key-btn">' . t('Copy key') . '</button>'
      . '<span class="acuity-copy-feedback" style="display:none;color:green">' . t('Copied!') . '</span>'
      . '</div>'
      . '<p style="margin-top:0.8em" class="description">' . t('<strong>Step 2</strong> — run this command on the server to write the key to a file (replace <code>YOUR_KEY</code> with the key above and adjust the path):') . '</p>'
      . '<pre class="acuity-ext-command" style="background:#f5f5f5;padding:8px;font-size:0.85em;overflow-x:auto">'
      . 'mkdir -p /home/username/private/keys' . "\n"
      . 'printf \'%s\' \'<span class="acuity-key-placeholder">YOUR_KEY</span>\' &gt; /home/username/private/keys/acuity_encrypt_key1.key' . "\n"
      . 'chmod 0400 /home/username/private/keys/acuity_encrypt_key1.key'
      . '</pre>'
      . '</div>'
      . '</div>',
  ];

  $form['section_external_file']['ext_steps'] = [
    '#markup' => '<div style="margin-top:0.8em"><p><strong>' . t('Step 3 — enter the path below') . '</strong></p>'
      . '<ol>'
      . '<li>' . t('Choose a directory <strong>outside the webroot</strong>. If your site is at <code>/home/username/public_html/</code>, use something like <code>/home/username/private/keys/</code>.') . '</li>'
      . '<li>' . t('Create the file using the command above (or ask your host to do it).') . '</li>'
      . '<li>' . t('Enter the full absolute path to the file in the field below.') . '</li>'
      . '<li>' . t('Click <strong>"Save — use external file"</strong>.') . '</li>'
      . '</ol>'
      . '<p class="description">' . t('<strong>Key naming convention:</strong> Name the file <code>acuity_encrypt_key1.key</code> for Slot 1, <code>acuity_encrypt_key2.key</code> for Slot 2, etc. — the same pattern used across all storage methods.') . '</p>'
      . '</div>',
  ];

  $form['section_external_file']['external_file_path'] = [
    '#type'          => 'textfield',
    '#title'         => t('Full path to key file'),
    '#default_value' => $external_path,
    '#description'   => t('Absolute server path — for example: <code>/home/username/private/keys/acuity_encrypt_key1.key</code>. Do not use a browser URL.'),
    '#size'          => 80,
    '#maxlength'     => 512,
    '#attributes'    => ['autocomplete' => 'off'],
  ];

  $form['section_external_file']['submit'] = [
    '#type'                  => 'submit',
    '#value'                 => t('Save — use external file'),
    '#name'                  => 'submit_external_file',
    '#submit'                => ['acuity_encrypt_admin_external_file_submit'],
    '#validate'              => ['acuity_encrypt_admin_external_file_validate'],
    '#limit_validation_errors' => [['section_external_file', 'external_file_path']],
  ];

  return $form;
}

// ── Submit handlers ───────────────────────────────────────────────────────────

/**
 * Submit: switch to settings.php method.
 */
function acuity_encrypt_admin_settings_php_submit(array $_form, array &$_form_state): void {
  config('acuity_encrypt.settings')
    ->set('key_storage_method', 'settings_php')
    ->set('external_file_path', '')
    ->save();
  _acuity_encrypt_registry_set_slot(1, ['storage' => 'settings_php', 'external_path' => '']);

  global $settings;
  if (!empty($settings['acuity_encrypt_key1'])) {
    backdrop_set_message(t('Encryption is active — key found in settings.php.'));
  }
  else {
    backdrop_set_message(
      t('Storage method set to settings.php. Encryption will become active once you add '
        . '<code>$settings[\'acuity_encrypt_key1\'] = \'your-key\';</code> to settings.php.'),
      'warning'
    );
  }
}

/**
 * Validate: block overwriting an existing private-file key unless the admin
 * has ticked the "I understand" confirmation checkbox.
 *
 * A new key value only replaces an existing key file if it actually differs
 * from what's currently stored — pasting back the same key (or leaving the
 * field blank) never requires confirmation.
 */
function acuity_encrypt_admin_private_file_validate(array $_form, array &$form_state): void {
  $new_key = trim($form_state['values']['private_key_value'] ?? '');
  if ($new_key === '') {
    return;
  }

  $realpath = _acuity_encrypt_private_key_realpath();
  if (!$realpath || !file_exists($realpath)) {
    return;
  }

  $existing_key = trim((string) file_get_contents($realpath));
  if ($existing_key === '' || $existing_key === $new_key) {
    return;
  }

  if (empty($form_state['values']['confirm_overwrite'])) {
    form_set_error('section_private_file][confirm_overwrite',
      t('A key is already saved here. Tick the confirmation checkbox to replace it — doing so will permanently make any data encrypted with the current key unreadable.'));
  }
}

/**
 * Submit: save key to private filesystem and switch to private_file method.
 */
function acuity_encrypt_admin_private_file_submit(array $_form, array &$form_state): void {
  config('acuity_encrypt.settings')
    ->set('key_storage_method', 'private_file')
    ->set('external_file_path', '')
    ->save();
  _acuity_encrypt_registry_set_slot(1, ['storage' => 'private_file', 'external_path' => '']);

  _acuity_encrypt_save_private_key($form_state['values']['private_key_value'] ?? '');
}

/**
 * Validate: external file path must exist and be readable.
 */
function acuity_encrypt_admin_external_file_validate(array $_form, array &$form_state): void {
  $path = trim($form_state['values']['external_file_path'] ?? '');

  if (empty($path)) {
    form_set_error('section_external_file][external_file_path',
      t('Please enter the full path to your key file.'));
    return;
  }
  if (!file_exists($path)) {
    form_set_error('section_external_file][external_file_path',
      t('No file found at %path — create the file first, then save.', ['%path' => $path]));
    return;
  }
  if (!is_readable($path)) {
    form_set_error('section_external_file][external_file_path',
      t('The file at %path exists but cannot be read by the web server. Check file permissions (try chmod 0440).', ['%path' => $path]));
    return;
  }

  // Hard-block paths inside the webroot: if the web server config ever
  // changes (or a .htaccess/nginx rule is missing), a key file inside the
  // webroot is directly downloadable via a browser URL.
  $webroot  = backdrop_realpath(BACKDROP_ROOT);
  $ext_real = realpath($path);
  if ($webroot && $ext_real && strpos($ext_real, $webroot) === 0) {
    form_set_error('section_external_file][external_file_path',
      t('The path %path is inside the webroot and could be served directly over HTTP. Choose a location outside the webroot (e.g. a sibling directory to your Backdrop install).', ['%path' => $path]));
  }
}

/**
 * Submit: switch to external file method and save the path.
 */
function acuity_encrypt_admin_external_file_submit(array $_form, array &$form_state): void {
  $path = trim($form_state['values']['external_file_path'] ?? '');

  config('acuity_encrypt.settings')
    ->set('key_storage_method', 'external_file')
    ->set('external_file_path', $path)
    ->save();
  _acuity_encrypt_registry_set_slot(1, ['storage' => 'external_file', 'external_path' => $path]);

  backdrop_set_message(t('Storage method set to external file at <code>@path</code>.', ['@path' => $path]));
}

// ── Slot management (Phase 2) ─────────────────────────────────────────────────

/**
 * Form callback: add or edit a key slot.
 *
 * @param string|null $slot_arg
 *   Slot id from the URL when editing, NULL when adding.
 */
function acuity_encrypt_slot_form(array $form, array &$form_state, string|null $slot_arg = NULL): array {
  $registry = acuity_encrypt_slots();
  $is_new = $slot_arg === NULL;

  if ($is_new) {
    $slot_id = max(array_keys($registry['slots'])) + 1;
    $info = ['label' => '', 'storage' => 'settings_php', 'external_path' => ''];
    backdrop_set_title(t('Add key slot @id', ['@id' => $slot_id]));
  }
  else {
    $slot_id = (int) $slot_arg;
    if (!isset($registry['slots'][$slot_id])) {
      backdrop_not_found();
      backdrop_exit();
    }
    $info = $registry['slots'][$slot_id];
    backdrop_set_title(t('Edit key slot @id', ['@id' => $slot_id]));
  }

  $form_state['slot_id'] = $slot_id;
  $form_state['is_new'] = $is_new;

  $form['#attached'] = [
    'js'  => [backdrop_get_path('module', 'acuity_encrypt') . '/js/acuity_encrypt.admin.js'],
    'css' => [backdrop_get_path('module', 'acuity_encrypt') . '/css/acuity_encrypt.css'],
  ];

  if (!$is_new) {
    $counts = acuity_encrypt_slot_value_counts();
    $count = $counts['slots'][$slot_id] ?? 0;
    if ($count > 0) {
      $form['data_warning'] = [
        '#markup' => '<p class="messages warning">' . format_plural(
          $count,
          'The key in this slot protects 1 encrypted database entry (a stored field value; revision copies are counted too). Changing the storage method is fine as long as the SAME key ends up readable from the new location — pointing this slot at a different key makes that data unreadable.',
          'The key in this slot protects @count encrypted database entries (stored field values, including revision copies — not @count separate keys). Changing the storage method is fine as long as the SAME key ends up readable from the new location — pointing this slot at a different key makes that data unreadable.'
        ) . '</p>',
      ];
    }
  }

  $form['label'] = [
    '#type'          => 'textfield',
    '#title'         => t('Label'),
    '#description'   => t('Shown in the slot table — e.g. "2026 rotation".'),
    '#default_value' => $info['label'],
    '#maxlength'     => 64,
    '#required'      => TRUE,
  ];

  $form['storage'] = [
    '#type'          => 'radios',
    '#title'         => t('Key storage method'),
    '#options'       => [
      'settings_php'  => t('settings.php — add <code>$settings[\'acuity_encrypt_key@id\']</code> manually (recommended)', ['@id' => $slot_id]),
      'private_file'  => t('Private files — module writes <code>private://keys/acuity_encrypt_key@id.key</code>', ['@id' => $slot_id]),
      'external_file' => t('External file — you provide an absolute path outside the webroot'),
    ],
    '#default_value' => $info['storage'],
  ];

  $form['generate'] = [
    '#markup' => '<div class="acuity-generate-wrap" data-mode="settings_php" data-slot="' . $slot_id . '">'
      . '<p><strong>' . t('Need a key? Generate one:') . '</strong></p>'
      . '<button type="button" class="button acuity-generate-key-btn">' . t('Generate key') . '</button>'
      . '<div class="acuity-generated-area" style="display:none;margin-top:0.8em">'
      . '<p class="description" style="color:#c00"><strong>' . t('Copy this to your password manager NOW.') . '</strong> '
      . t('For settings.php storage, paste the whole line into settings.php. For file storage, paste just the key part into the field below / your key file.') . '</p>'
      . '<textarea class="acuity-key-line" readonly rows="2" style="width:100%;max-width:700px;font-family:monospace;font-size:0.85em"></textarea>'
      . '<br><button type="button" class="button acuity-copy-key-btn" style="margin-top:4px">' . t('Copy') . '</button>'
      . '<span class="acuity-copy-feedback" style="display:none;margin-left:8px;color:green">' . t('Copied!') . '</span>'
      . '</div></div>',
  ];

  $form['private_key_value'] = [
    '#type'        => 'textfield',
    '#title'       => t('Key value (private files only)'),
    '#description' => t('The key to write to the private key file. Leave blank to keep an existing key file, or to auto-generate a new one on save.'),
    '#size'        => 70,
    '#maxlength'   => 512,
    '#attributes'  => ['autocomplete' => 'off'],
    '#states'      => [
      'visible' => [':input[name="storage"]' => ['value' => 'private_file']],
    ],
  ];

  $priv_realpath = _acuity_encrypt_private_key_realpath($slot_id);
  if ($priv_realpath && file_exists($priv_realpath)) {
    $form['confirm_overwrite'] = [
      '#type'   => 'checkbox',
      '#title'  => t('A key file already exists for this slot. I understand that saving a DIFFERENT key makes data encrypted with the current key permanently unreadable.'),
      '#states' => [
        'visible' => [':input[name="storage"]' => ['value' => 'private_file']],
      ],
    ];
  }

  $form['external_path'] = [
    '#type'          => 'textfield',
    '#title'         => t('Full path to key file (external file only)'),
    '#default_value' => $info['external_path'],
    '#description'   => t('Absolute server path outside the webroot, e.g. <code>/home/username/private/keys/acuity_encrypt_key@id.key</code>.', ['@id' => $slot_id]),
    '#size'          => 80,
    '#maxlength'     => 512,
    '#attributes'    => ['autocomplete' => 'off'],
    '#states'        => [
      'visible' => [':input[name="storage"]' => ['value' => 'external_file']],
    ],
  ];

  $form['actions'] = ['#type' => 'actions'];
  $form['actions']['submit'] = [
    '#type'  => 'submit',
    '#value' => $is_new ? t('Add slot') : t('Save slot'),
  ];
  $form['actions']['cancel'] = [
    '#markup' => l(t('Cancel'), 'admin/config/acuity-utils/acuity_encrypt'),
  ];

  return $form;
}

/**
 * Validate handler for the slot form.
 */
function acuity_encrypt_slot_form_validate(array $form, array &$form_state): void {
  $values = $form_state['values'];
  $slot_id = $form_state['slot_id'];

  if ($values['storage'] === 'external_file') {
    $path = trim($values['external_path']);
    if ($path === '') {
      form_set_error('external_path', t('Enter the full path to the key file.'));
      return;
    }
    if (!file_exists($path)) {
      form_set_error('external_path', t('No file found at %path — create the file first.', ['%path' => $path]));
      return;
    }
    if (!is_readable($path)) {
      form_set_error('external_path', t('The file at %path cannot be read by the web server.', ['%path' => $path]));
      return;
    }
    $webroot = backdrop_realpath(BACKDROP_ROOT);
    $real = realpath($path);
    if ($webroot && $real && strpos($real, $webroot) === 0) {
      form_set_error('external_path', t('The path %path is inside the webroot and could be served over HTTP. Choose a location outside the webroot.', ['%path' => $path]));
    }
  }

  if ($values['storage'] === 'private_file') {
    $new_key = trim($values['private_key_value'] ?? '');
    $realpath = _acuity_encrypt_private_key_realpath($slot_id);
    if ($new_key !== '' && $realpath && file_exists($realpath)) {
      $existing = trim((string) file_get_contents($realpath));
      if ($existing !== '' && $existing !== $new_key && empty($values['confirm_overwrite'])) {
        form_set_error('confirm_overwrite', t('A key is already saved for this slot. Tick the confirmation checkbox to replace it.'));
      }
    }
  }
}

/**
 * Submit handler for the slot form.
 */
function acuity_encrypt_slot_form_submit(array $form, array &$form_state): void {
  $values = $form_state['values'];
  $slot_id = $form_state['slot_id'];

  _acuity_encrypt_registry_set_slot($slot_id, [
    'label' => trim($values['label']),
    'storage' => $values['storage'],
    'external_path' => $values['storage'] === 'external_file' ? trim($values['external_path']) : '',
  ]);

  if ($values['storage'] === 'private_file') {
    _acuity_encrypt_save_private_key(trim($values['private_key_value'] ?? ''), $slot_id);
  }

  if ($values['storage'] === 'settings_php' && _acuity_encrypt_get_key($slot_id) === NULL) {
    backdrop_set_message(t(
      'Slot @id saved. It becomes usable once you add <code>$settings[\'acuity_encrypt_key@id\'] = \'your-key\';</code> to settings.php.',
      ['@id' => $slot_id]
    ), 'warning');
  }
  else {
    backdrop_set_message(t('Key slot @id saved.', ['@id' => $slot_id]));
  }

  // Keep the legacy single-slot settings in sync when editing slot 1 so the
  // guided "Set keys" tab and older tooling keep reporting the truth.
  if ($slot_id === 1) {
    config('acuity_encrypt.settings')
      ->set('key_storage_method', $values['storage'])
      ->set('external_file_path', $values['storage'] === 'external_file' ? trim($values['external_path']) : '')
      ->save();
  }

  $form_state['redirect'] = 'admin/config/acuity-utils/acuity_encrypt';
}

/**
 * Form callback: confirm making a slot the active one.
 */
function acuity_encrypt_slot_activate_confirm(array $form, array &$form_state, string $slot_arg): array {
  $registry = acuity_encrypt_slots();
  $slot_id = (int) $slot_arg;

  if (!isset($registry['slots'][$slot_id])) {
    backdrop_not_found();
    backdrop_exit();
  }
  if (_acuity_encrypt_get_key($slot_id) === NULL) {
    $form['error'] = [
      '#markup' => '<p class="messages error">' . t(
        'Slot @id has no key configured — it cannot be made active. Configure its key first.',
        ['@id' => $slot_id]
      ) . '</p><p>' . l(t('Back to key slots'), 'admin/config/acuity-utils/acuity_encrypt') . '</p>',
    ];
    return $form;
  }

  $form_state['slot_id'] = $slot_id;
  return confirm_form(
    $form,
    t('Make slot @id ("@label") the active encryption slot?', ['@id' => $slot_id, '@label' => $registry['slots'][$slot_id]['label']]),
    'admin/config/acuity-utils/acuity_encrypt',
    t('All NEW encryptions will use this slot (enc@id: prefix). Existing data stays on its current slot and still decrypts normally — run "Rotate keys" afterwards to migrate it. Make sure this slot\'s key is backed up before proceeding.', ['@id' => $slot_id]),
    t('Set active'),
    t('Cancel')
  );
}

/**
 * Submit handler for the activate confirmation.
 */
function acuity_encrypt_slot_activate_confirm_submit(array $form, array &$form_state): void {
  $slot_id = $form_state['slot_id'];
  config_set('acuity_encrypt.slots', 'active_slot', $slot_id);
  backdrop_static_reset('acuity_encrypt_slots');
  watchdog('acuity_encrypt', 'Active key slot changed to @id.', ['@id' => $slot_id], WATCHDOG_NOTICE);
  backdrop_set_message(t('Slot @id is now active — all new encryptions use it. To move existing data onto it, <a href="@url">rotate keys</a>.', [
    '@id' => $slot_id,
    '@url' => url('admin/config/acuity-utils/acuity_encrypt/rotate'),
  ]));
  $form_state['redirect'] = 'admin/config/acuity-utils/acuity_encrypt';
}

// ── Private file write helper ─────────────────────────────────────────────────

/**
 * Writes the key to private://keys/acuity_encrypt_key{slot}.key.
 *
 * If $key_value is empty and a file already exists, the existing key is kept.
 * If no file exists yet and $key_value is empty, a random key is generated.
 *
 * @param string $key_value  Raw key string, or empty string.
 * @param int    $slot       Key slot number (default 1).
 */
function _acuity_encrypt_save_private_key(string $key_value, int $slot = 1): void {
  $dir      = 'private://keys';
  $filename = 'acuity_encrypt_key' . $slot . '.key';
  $uri      = $dir . '/' . $filename;

  if (!file_prepare_directory($dir, FILE_CREATE_DIRECTORY | FILE_MODIFY_PERMISSIONS)) {
    backdrop_set_message(
      t('Could not create <code>private://keys/</code>. Check the private files directory is writable.'),
      'error'
    );
    return;
  }

  $existing_path = backdrop_realpath($uri);
  $key_exists    = $existing_path && file_exists($existing_path);

  $auto_generated = FALSE;
  if (empty($key_value)) {
    if ($key_exists) {
      backdrop_set_message(t('Private files method active. Existing key preserved at <code>private://keys/@filename</code>.', ['@filename' => $filename]));
      return;
    }
    $key_value      = bin2hex(random_bytes(32));
    $auto_generated = TRUE;
  }

  $dir_real = backdrop_realpath($dir);
  if (!$dir_real || file_put_contents($dir_real . '/' . $filename, $key_value) === FALSE) {
    backdrop_set_message(t('Failed to write the key file. Check the private files directory is writable.'), 'error');
    return;
  }

  $realpath = backdrop_realpath($uri);
  if ($realpath) {
    @chmod($realpath, 0400);
  }

  if ($auto_generated) {
    backdrop_set_message(t(
      'A random key was generated and saved to <code>private://keys/@filename</code>. '
      . '<strong>Back it up now</strong> using the panel at the top of this page.',
      ['@filename' => $filename]
    ));
  }
  else {
    backdrop_set_message(t(
      'Key saved to <code>private://keys/@filename</code>. '
      . '<strong>Keep a copy in your password manager.</strong>',
      ['@filename' => $filename]
    ));
  }
}
