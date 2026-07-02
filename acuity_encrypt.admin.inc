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
 * Reads from whichever source is active. Returns NULL if no key is configured.
 */
function _acuity_encrypt_get_raw_key_for_display(): string|null {
  global $settings;

  if (!empty($settings['acuity_encrypt_key1'])) {
    return $settings['acuity_encrypt_key1'];
  }

  $method = config_get('acuity_encrypt.settings', 'key_storage_method') ?: 'settings_php';

  if ($method === 'external_file') {
    $path = config_get('acuity_encrypt.settings', 'external_file_path');
    if (!empty($path) && is_readable($path)) {
      return trim(file_get_contents($path)) ?: NULL;
    }
    return NULL;
  }

  if ($method === 'private_file') {
    $realpath = backdrop_realpath('private://keys/acuity_encrypt_key1.key');
    if ($realpath && is_readable($realpath)) {
      return trim(file_get_contents($realpath)) ?: NULL;
    }
    return NULL;
  }

  return NULL;
}

// ── Key slots overview page ───────────────────────────────────────────────────

/**
 * Page callback for the "View keys" tab.
 *
 * Displays a table of key slots and their current status. Only Slot 1 is
 * active in this version; the table is structured for future multi-slot
 * support.
 *
 * @return array
 *   Render array.
 */
function acuity_encrypt_keys_overview(): array {
  $config = config('acuity_encrypt.settings');
  $method = $config->get('key_storage_method') ?: 'settings_php';

  // ── Slot 1 row ─────────────────────────────────────────────────────────

  $key_available = acuity_encrypt_key_available();

  if ($key_available) {
    $status_html = '<span class="acuity-status-ok">&#10003; ' . t('Active') . '</span>';
  }
  else {
    $status_html = '<span class="acuity-status-missing">&#10007; ' . t('Key missing') . '</span>';
  }

  $method_labels = [
    'settings_php'  => t('settings.php'),
    'private_file'  => t('Private files'),
    'external_file' => t('External file'),
  ];
  $method_label = isset($method_labels[$method]) ? $method_labels[$method] : $method;

  switch ($method) {
    case 'settings_php':
      $key_identifier = '<code>$settings[\'acuity_encrypt_key1\']</code> in settings.php';
      break;

    case 'private_file':
      $uri      = 'private://keys/acuity_encrypt_key1.key';
      $realpath = backdrop_realpath($uri);
      $key_identifier = '<code>' . check_plain($realpath ?: $uri) . '</code>';
      break;

    case 'external_file':
      $path = $config->get('external_file_path') ?: '';
      $key_identifier = $path
        ? '<code>' . check_plain($path) . '</code>'
        : '<em>' . t('(path not configured)') . '</em>';
      break;

    default:
      $key_identifier = '<em>' . t('Unknown') . '</em>';
  }

  $rows = [
    [
      ['data' => '1'],
      ['data' => t('Default')],
      ['data' => $method_label],
      ['data' => $key_identifier],
      ['data' => $status_html],
    ],
  ];

  $header = [
    t('Slot'),
    t('Label'),
    t('Storage method'),
    t('Key identifier'),
    t('Status'),
  ];

  // ── Build render array ──────────────────────────────────────────────────

  $build = [];

  $build['#attached'] = [
    'css' => [backdrop_get_path('module', 'acuity_encrypt') . '/css/acuity_encrypt.css'],
  ];

  $build['table'] = [
    '#theme'      => 'table',
    '#header'     => $header,
    '#rows'       => $rows,
    '#attributes' => ['class' => ['acuity-encrypt-keys-table']],
  ];

  $build['note'] = [
    '#markup' => '<p class="description" style="margin-top:1em">'
      . t('<strong>Note:</strong> Only Slot 1 is available in this version. '
        . 'Multiple key slots and key rotation — for migrating existing encrypted data to a new key '
        . 'without downtime — are planned for a future release.')
      . '</p>',
  ];

  $build['link'] = [
    '#markup' => '<p style="margin-top:0.8em">'
      . l(t('Configure key storage »'), 'admin/config/acuity-utils/acuity_encrypt/settings')
      . '</p>',
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
    $raw_key = _acuity_encrypt_get_raw_key_for_display();
    if ($raw_key !== NULL) {
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
        '#markup' => '<div class="acuity-encrypt-wrap">'
          . '<span class="acuity-encrypt-masked">'
          . str_repeat('•', min(strlen($raw_key), 64))
          . '</span>'
          . '<span class="acuity-encrypt-value" style="display:none;font-family:monospace;word-break:break-all">'
          . check_plain($raw_key)
          . '</span>'
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
          'js'  => [backdrop_get_path('module', 'acuity_encrypt') . '/js/acuity_encrypt.js'],
        ],
      ];
    }
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

  $form['section_private_file']['submit'] = [
    '#type'                  => 'submit',
    '#value'                 => t('Save — use private files'),
    '#name'                  => 'submit_private_file',
    '#submit'                => ['acuity_encrypt_admin_private_file_submit'],
    '#limit_validation_errors' => [['section_private_file', 'private_key_value']],
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
 * Submit: save key to private filesystem and switch to private_file method.
 */
function acuity_encrypt_admin_private_file_submit(array $_form, array &$form_state): void {
  config('acuity_encrypt.settings')
    ->set('key_storage_method', 'private_file')
    ->set('external_file_path', '')
    ->save();

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

  backdrop_set_message(t('Storage method set to external file at <code>@path</code>.', ['@path' => $path]));
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
