/* =====================================================================
   SPOOL landing-page interactivity.

   1. Theme randomiser — double-click the SP·L mark or hero logo to
      retint every accent on the page (mirrors the in-app SP·L easter egg).
   2. Copy-to-clipboard for install command blocks.
   3. Reveal-on-scroll for sections.
   4. Spinning vinyl in the SVG already runs via CSS; nothing to do here.
   ===================================================================== */

(function () {
    'use strict';

    // ---- 1. THEME RANDOMISER ----------------------------------------------
    // Random hue, fixed saturation/value so the vibe stays consistent.
    function randomiseTheme() {
        const h = Math.random();                          // 0..1
        const accent       = hsvToHex(h, 0.78, 0.95);
        const accentBright = hsvToHex(h, 0.70, 1.0);
        const accentDim    = hsvToHex(h, 0.85, 0.30);
        const oledDim      = hsvToHex(h, 0.65, 0.35);

        const root = document.documentElement.style;
        root.setProperty('--accent',        accent);
        root.setProperty('--accent-bright', accentBright);
        root.setProperty('--accent-dim',    accentDim);
        root.setProperty('--oled-dim',      oledDim);

        document.querySelector('meta[name="theme-color"]')
            ?.setAttribute('content', accent);
    }

    function hsvToHex(h, s, v) {
        const i = Math.floor(h * 6);
        const f = h * 6 - i;
        const p = v * (1 - s);
        const q = v * (1 - f * s);
        const t = v * (1 - (1 - f) * s);
        let r, g, b;
        switch (i % 6) {
            case 0: r = v; g = t; b = p; break;
            case 1: r = q; g = v; b = p; break;
            case 2: r = p; g = v; b = t; break;
            case 3: r = p; g = q; b = v; break;
            case 4: r = t; g = p; b = v; break;
            case 5: r = v; g = p; b = q; break;
        }
        const toHex = n => Math.round(n * 255).toString(16).padStart(2, '0');
        return '#' + toHex(r) + toHex(g) + toHex(b);
    }

    const themeTriggers = document.querySelectorAll('.brand-mark, .hero-logo, .footer-mark');
    themeTriggers.forEach(el => {
        el.addEventListener('dblclick', randomiseTheme);
        el.title = 'double-click to randomise theme';
        el.style.cursor = 'pointer';
    });

    // Click the TAP button mockup → randomise theme too, for a little easter.
    const tap = document.querySelector('.tap-button');
    if (tap) {
        tap.addEventListener('click', () => {
            tap.classList.add('flash');
            randomiseTheme();
            setTimeout(() => tap.classList.remove('flash'), 220);
        });
    }

    // ---- 2. COPY-TO-CLIPBOARD ---------------------------------------------
    document.querySelectorAll('.copy-block').forEach(block => {
        const btn   = block.querySelector('.copy-btn');
        const text  = block.dataset.copy || block.querySelector('code')?.textContent || '';
        if (!btn) return;
        btn.addEventListener('click', async () => {
            try {
                await navigator.clipboard.writeText(text);
                btn.textContent = 'copied';
                btn.classList.add('ok');
                setTimeout(() => {
                    btn.textContent = 'copy';
                    btn.classList.remove('ok');
                }, 1400);
            } catch {
                // Fallback for older browsers / non-secure contexts.
                const range = document.createRange();
                range.selectNodeContents(block.querySelector('code'));
                window.getSelection().removeAllRanges();
                window.getSelection().addRange(range);
            }
        });
    });

    // ---- 3. REVEAL-ON-SCROLL ----------------------------------------------
    const revealTargets = document.querySelectorAll(
        '.feature, .install-card, .control-block, .showcase-row, .section-head'
    );
    revealTargets.forEach(el => el.classList.add('reveal'));

    if ('IntersectionObserver' in window) {
        const io = new IntersectionObserver(entries => {
            for (const e of entries) {
                if (e.isIntersecting) {
                    e.target.classList.add('in');
                    io.unobserve(e.target);
                }
            }
        }, { rootMargin: '0px 0px -80px 0px', threshold: 0.05 });
        revealTargets.forEach(el => io.observe(el));
    } else {
        // No IO support — just show everything.
        revealTargets.forEach(el => el.classList.add('in'));
    }

})();
