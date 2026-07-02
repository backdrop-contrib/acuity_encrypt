/**
 * @file
 * Admin page JS for Acuity Encrypt.
 *
 * Handles:
 *  - "Generate key" button: creates a cryptographically random key in the
 *    browser using window.crypto.getRandomValues() and populates the
 *    appropriate display area and/or form field.
 *  - "Copy to clipboard" button: copies the generated value to the clipboard
 *    with a visible "Copied!" confirmation, falling back to execCommand for
 *    older browsers.
 *  - Key reveal on the backup panel (copy button that appears after reveal).
 */

(function ($) {
  'use strict';

  Backdrop.behaviors.acuityEncryptAdmin = {
    attach: function (context) {

      // ── Generate key button ───────────────────────────────────────────────

      $('.acuity-generate-key-btn', context).once('acuity-generate-key').on('click', function (e) {
        e.preventDefault();

        var $wrap = $(this).closest('.acuity-generate-wrap');
        var mode  = $wrap.data('mode');

        // Generate 32 random bytes → 64-character hex string.
        var bytes = new Uint8Array(32);
        window.crypto.getRandomValues(bytes);
        var key = Array.prototype.map.call(bytes, function (b) {
          return ('0' + b.toString(16)).slice(-2);
        }).join('');

        // Set the textarea value based on mode.
        if (mode === 'settings_php') {
          $wrap.find('.acuity-key-line').val("$settings['acuity_encrypt_key1'] = '" + key + "';");
        } else {
          // private_file or external_file: show raw key.
          $wrap.find('.acuity-key-line').val(key);

          // Also fill the key placeholder in the external file command hint.
          $wrap.find('.acuity-key-placeholder').text(key);

          // Fill the associated form field if specified.
          var fillSelector = $wrap.data('fill-field');
          if (fillSelector) {
            $(fillSelector).val(key);
          }
        }

        // Show the generated area.
        $wrap.find('.acuity-generated-area').slideDown(200);

        // Change button label.
        $(this).text(Backdrop.t('Regenerate key'));
      });

      // ── Copy to clipboard ─────────────────────────────────────────────────

      $('.acuity-copy-key-btn', context).once('acuity-copy-key').on('click', function (e) {
        e.preventDefault();

        var $wrap     = $(this).closest('.acuity-generate-wrap');
        var text      = $wrap.find('.acuity-key-line').val();
        var $feedback = $wrap.find('.acuity-copy-feedback');

        _acuityCopyText(text, $feedback);
      });

      // Copy button shown after the key reveal on the backup panel.
      $('.acuity-copy-reveal-btn', context).once('acuity-copy-reveal').on('click', function (e) {
        e.preventDefault();

        var $panel    = $(this).closest('.acuity-encrypt-wrap');
        var text      = $panel.find('.acuity-encrypt-value').text();
        var $feedback = $panel.find('.acuity-copy-feedback');

        _acuityCopyText(text, $feedback);
      });

      // Show the copy button alongside the backup panel reveal button, once
      // the value has been revealed.
      $('.acuity-encrypt-reveal-btn', context).once('acuity-reveal-copy-toggle').on('click', function () {
        var $wrap = $(this).closest('.acuity-encrypt-wrap');
        // Short delay so the reveal behavior fires first.
        setTimeout(function () {
          var isRevealed = $wrap.find('.acuity-encrypt-value').is(':visible');
          $wrap.find('.acuity-copy-reveal-btn').toggle(isRevealed);
        }, 50);
      });

    }
  };

  // ── Shared clipboard helper ───────────────────────────────────────────────

  function _acuityCopyText(text, $feedback) {
    if (!text) {
      return;
    }

    if (navigator.clipboard && navigator.clipboard.writeText) {
      navigator.clipboard.writeText(text).then(function () {
        _acuityShowFeedback($feedback);
      }).catch(function () {
        _acuityFallbackCopy(text, $feedback);
      });
    } else {
      _acuityFallbackCopy(text, $feedback);
    }
  }

  function _acuityFallbackCopy(text, $feedback) {
    // Create a temporary textarea, select its content, and execCommand copy.
    var $tmp = $('<textarea></textarea>')
      .css({ position: 'fixed', opacity: 0, top: 0, left: 0 })
      .val(text)
      .appendTo(document.body);
    try {
      $tmp[0].select();
      document.execCommand('copy');
      _acuityShowFeedback($feedback);
    } catch (err) {
      // Could not copy — do nothing (the textarea is still visible for manual copy).
    }
    $tmp.remove();
  }

  function _acuityShowFeedback($feedback) {
    $feedback.stop(true, true).show().delay(2200).fadeOut(300);
  }

})(jQuery);
