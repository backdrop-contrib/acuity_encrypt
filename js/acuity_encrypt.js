(function ($) {
  'use strict';

  Backdrop.behaviors.acuityEncrypt = {
    attach: function (context) {

      // ── Display formatter: reveal/hide toggle ───────────────────────────

      $('.acuity-encrypt-reveal-btn', context).once('acuity-encrypt').on('click', function () {
        var $wrap   = $(this).closest('.acuity-encrypt-wrap');
        var $masked = $wrap.find('.acuity-encrypt-masked');
        var $value  = $wrap.find('.acuity-encrypt-value');
        var $btn    = $(this);
        var keyUrl  = $wrap.data('key-url');

        if ($value.is(':hidden')) {
          // Key backup panel: the raw key is never in the page markup — fetch
          // it on demand the first time "Reveal key" is clicked.
          if (keyUrl && !$value.data('loaded')) {
            $btn.prop('disabled', true);
            $.getJSON(keyUrl).done(function (response) {
              if (response && response.key) {
                $value.text(response.key).data('loaded', true);
                $masked.attr('aria-hidden', 'true').hide();
                $value.show();
                $btn.text(Backdrop.t('Hide'));
              }
              else {
                $value.text(response && response.error ? response.error : Backdrop.t('Could not retrieve key.'));
              }
            }).fail(function () {
              $value.text(Backdrop.t('Could not retrieve key.'));
            }).always(function () {
              $btn.prop('disabled', false);
            });
            return;
          }

          $masked.attr('aria-hidden', 'true').hide();
          $value.show();
          $btn.text(Backdrop.t('Hide'));
        }
        else {
          // Key backup panel: scrub the fetched key from the DOM on hide —
          // the next Reveal re-fetches it. (Field formatters have no keyUrl;
          // their hidden value is embedded server-side for authorised users
          // by design.)
          if (keyUrl) {
            $value.text('').removeData('loaded');
          }
          $value.hide();
          $masked.removeAttr('aria-hidden').show();
          $btn.text(Backdrop.t('Reveal'));
        }
      });

      // ── Edit widget: reveal the editable field ──────────────────────────
      //
      // Two modes, selected by data-mode on the button:
      //
      // inline  — single-line text. The real input is display:none (still
      //           submits). Clicking swaps the mask row for the real input.
      //
      // overlay — long text / WYSIWYG. CKEditor initialises normally under a
      //           position:absolute overlay. Clicking removes the overlay;
      //           the editor is already initialised and ready.

      $('.acuity-widget-reveal-btn', context).once('acuity-widget-reveal').on('click', function () {
        var mode = $(this).data('mode') || 'inline';

        if (mode === 'overlay') {
          $(this).closest('.acuity-wysiwyg-mask').hide();
        }
        else {
          var $wrap = $(this).closest('.acuity-encrypt-widget-wrap');
          $wrap.find('.acuity-widget-mask-row').hide();
          $wrap.find('.acuity-widget-edit-input').show().trigger('focus');
        }
      });

    }
  };

}(jQuery));
