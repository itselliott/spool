# Changelog

All notable changes to SPOOL. Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [1.0.1] — 2026-05-22

Production-ready release. Major polish + new features on top of 1.0.0.

### Added
- **MIDI sampler** — polyphonic 8-voice playback of the loaded sample, chromatic via incoming MIDI notes. C4 (note 60) is the root; everything else is pitch-shifted by 2^((note − 60) / 12). `NEEDS_MIDI_INPUT TRUE` so DAWs route MIDI to SPOOL on insert tracks. Linear-interpolated reads, attack/sustain/release envelopes, oldest-voice steal. Works alongside the PLAY-button transport so you can perform live MIDI over a running loop.
- **On-screen mini keyboard** — toggle with the **♪ KEYS** pill above the transport row. Editor grows by 130 px to reveal a 2-octave SP-L-themed keyboard, plus pitch-bend and mod wheels flanking it. Plays through the same MIDI sampler.
- **Computer-keyboard input** — `a w s e d f t g y h u j k o l` plays notes chromatically (`a` = C4 = sample root). **`z`** lowers octave, **`x`** raises. View auto-scrolls so the active range stays visible. Focus stays with the editor after clicking any control, so typing notes keeps working without re-clicking.
- **Pitch wheel** — ±12 semitones (one octave). Snaps back to centre on release. Wide enough to actually hear on the sampler's pitched playback.
- **Mod wheel** — drives a 5 Hz amplitude tremolo on every active voice when ARP is OFF. When ARP is ON, the same wheel selects the arp rate in four zones (0-25% = 1/4, 25-50% = 1/8, 50-75% = 1/16, 75-100% = 1/32).
- **Arpeggiator** — engage with the on-keyboard **ARP** pill. Held notes go into a 16-deep list and the audio thread steps through them at BPM-synced rate, dedicating one voice slot to the arp. **PATTERN** cycle: UP / DN / UPDN / RND.
- **LO-FI master mode** — framed pink-purple pill under the SP-L wordmark engages a one-button "cassette / 90s sampler" character on the final output: warm tanh saturation + gentle 5-bit crush + 7 kHz HF rolloff, blended via the LO-FI dry/wet knob (rotary directly below the pill). Default knob 80 → 40 % wet; max 100 → 50 % wet. RESET returns the knob to 80.
- **LO-FI visual mode** — when LO-FI is on, the entire chassis re-skins to a pink/magenta/aubergine palette (no hue overlay — the actual body gradient + panel + seam colours change), the SP-L wordmark text re-tints dark purple, and an animated film-grain overlay shimmers across the device at ~10 Hz.
- **DAW state persistence** — `getStateInformation` / `setStateInformation` now fully serialize current sample audio + all 8 slot buffers + every effect parameter + BPM + signal-path order + LO-FI mode / mix (state v3, backward-compatible to v1). Save your Ableton project, reopen, everything's back. Standalone auto-restores the last session from `%APPDATA%/SPOOL/last-session.spoolset` on launch.
- **Insert-FX behaviour** — when no sample is playing, host audio passes straight through. Effects (FILTER, GHOST, HAZE, TAPE, LO-FI) still apply, so SPOOL can be used as a pure effects plugin on any track.
- **Host BPM sync** — the DAW's transport tempo auto-overrides internal BPM each block. All tempo-synced FX (loop SIZE buttons, FILTER LFO, GHOST delay, ARP rate) follow.
- **Drag-out the ↓ export button** — drops the current loop region as a 24-bit WAV onto any DAW track, Explorer, or Finder. Effects baked in.
- **First-launch welcome card** — credits + donation prompt on first open per machine. Dismissable for good.
- **Extended standalone Options menu** — JUCE's hamburger now includes: About / Credits, Report a Bug (mailto:elliottdevs@gmail.com), GitHub, Tip on Ko-fi, plus the version label.
- **Red RESET LED** in the header — one click wipes all knobs / effects / signal-order back to factory default. Loaded sample + 8 slots untouched.
- **GitHub Sponsors + Ko-fi** wired via `.github/FUNDING.yml` (Sponsor button on repo) and the landing page Support section.

### Changed
- **bg.png embedded** as a binary resource via `juce_add_binary_data` — backdrop renders in installed builds (release zips were missing the `assets/` folder).
- **Rebranded from `ksamples` to `itselliott`** — company name, BUNDLE_ID, PLUGIN_MANUFACTURER_CODE, all docs + landing-page links.
- **Diagnostic logger** moved from `C:/repo/spool/spool.log` to per-user app-data dir (cross-platform).
- **Background loader** scans cwd/assets, `%APPDATA%/SPOOL/assets`, and `<exe dir>/assets` for `bg.jpg/png` overrides, falls back to embedded.
- **Sample-gain value text** strips the ` dB` suffix.
- **HAZE preset labels** renamed to evocative names: VAULT, CHROME, NEST, MIST, ABYSS, AURA (was HALL/PLATE/ROOM/CHAMBER/CAVE/SHIMMER).

### Fixed
- **Scratch overhaul** — DJ scratch wheel now behaves like a real turntable / waveform scrubber. Position-based: cursor angle drives audio position 1:1, mouse stops → audio stops, slow wind backwards → smooth slow reverse. Clicking the wheel engages immediately (no drag required) and audio is audible from sample 1. Underlying fixes: editor was resetting the accumulator on every drag (clobbering cumulative motion); the sample-read block was being skipped when PLAY transport was off (silenced scratch entirely); reverse wrap was clamping pos to 0 (stuck-at-sample-0 stutter); 1 ms lerp time constant was too tight (audio bursts then silenced between mouse events) — now 25 ms so audio plays continuously through scrub. Cartridge LP cutoff tracks rate magnitude.
- **Vinyl reel angle locked to audio position** — the visual disc angle is now `playPosSec × baseRotPerSec × 2π` everywhere. Whether you're playing, scratching, MIDI-triggering, or scrubbing, the indicator on the disc is always exactly where the audio is. Release the wheel and re-grab: it's still there. Accurate scratching restored.
- **Waveform playhead follows MIDI** — playback marker now tracks the most-recently-triggered MIDI voice when the PLAY transport is off (previously froze).
- **Slot keys 1-8 no longer auto-loop** — loading a slot used to force `looping = true`; now slots play once unless the LOOP button is engaged.
- **Keyboard focus survives clicks** — every interactive child has `setMouseClickGrabsKeyboardFocus(false)`; deferred grabKeyboardFocus on launch + after welcome overlay + after LO-FI toggle. Computer-keyboard notes + slot keys keep working without re-clicking the panel.
- **BPM didn't propagate** — loop SIZE buttons stored fixed sample positions; changing BPM didn't recompute. Now atomically tracked + setBpm rescales the active loop window.
- **Reel rotation orbited off-axis** — switched to SMIL `<animateTransform>` with absolute viewport-coord centre (CSS `transform-box` was unreliable across browsers).
- **AURA shimmer was broken** — write/read positions desynced across channels, read pointer never advanced per-sample. Fixed with all-channels-per-sample processing + crossfaded dual read pointers to mask delay-line lap clicks.
- **Knob hit-box** — clicks on the centre of a knob were eaten by the value label. Made the label non-interactive; slider now fills the full area below the icon strip.
- **Filter cutoff Nyquist guard** — clamped below `sr * 0.49` so `tan()` doesn't overflow at low sample rates (22.05 / 32 kHz).
- **OVERDUB safety** — NaN/Inf playPosition + negative odPos + zero-channel buffers all guarded.
- **prepareToPlay** rejects bad sample rates with a 48 kHz fallback.
- **DJ scratch overhaul** — per-sample rate smoothing kills zipper noise, bit-crush disabled while scratching, 6 kHz cartridge LP adds vinyl warmth.
- **Slot save trims silence** + applies a 2 ms equal-power crossfade so loops wrap cleanly with no click.
- **Theme propagation** — double-click SP·L now retints **all** accent widgets (was missing INPUT knob, LOOP/OVERDUB buttons, value labels, several cycle pills).
- **Welcome overlay text** uses ASCII separators (`/`, `--`) — earlier UTF-8 bullet/em-dash didn't render in JUCE's default Windows font.
- **C4456 `bitLevels` shadowing warning** fixed by renaming the tape-stage local.
- **REC captured MIDI-triggered playback** — recording previously snapshotted the input buffer BEFORE the MIDI voices rendered, so MIDI-played notes never made it into the recording. Now both input and MIDI voices write to a shared block slice; `recordPos` advances uniformly at the end of the block.
- **Slot keys 1-8 no longer auto-loop** — loading a slot used to force `looping = true` regardless of the LOOP button state. Now slots play once unless LOOP is engaged, matching the LOOP button's actual setting.
- **Keyboard focus survives clicks** — every interactive child has `setMouseClickGrabsKeyboardFocus(false)`, plus the editor re-grabs focus at construction, after welcome-overlay dismissal, after LO-FI toggle. Computer-keyboard notes / slot keys / spacebar keep working without needing to click the panel between knob adjustments.

### Aesthetic
- **Oversized vinyl reel with SP·L wordmark** — the central vinyl now dominates the chassis, with a giant white "SP·L" printed across the black surface (clipped to the disc edge) and a small orange centre dot. Spins as one piece, also serves as a clear spin indicator.
- **Brushed-metal notched-pot knobs** replace the flat digital dials — 3D radial gradient body, faint brushing rings, dark recessed well with specular highlight on the rim, 13 tick marks where the active position glows accent.
- **OLED-style BPM window** with neon-glow cascade (alpha layers under sharp text). Separate TAP TEMPO button next to it.
- **LED-circle indicators** for INPUT comp voice and TAPE machine (was flat colored pills). Vector snowflake on FREEZE button (font-independent).
- **Loop control row** slimmed; LOOP CUTOFF visually differentiated as an accent-filled mode pill.
- **Live recording waveform** — scrolling 6-second peak meter from the live record buffer while REC is armed.

### Build / packaging
- **Version bumped** to 1.0.1 in CMakeLists + welcome overlay + standalone app.
- **GitHub Actions release workflow** auto-builds VST3 + Standalone for Windows + macOS on tag push (`v*`). Linux best-effort, won't block. Release zips bundle `assets/bg.png` alongside the binary.
- **GitHub Pages workflow** auto-publishes `docs/` to `itselliott.github.io/spool` on every push to main.

## [1.0.0] — 2026-05-21

First production release. VST3 + Standalone for Windows / macOS / Linux.

### Sampler / looper
- Drag-and-drop sample loading: WAV / AIF / FLAC / MP3 / OGG.
- Live field recorder with one-button capture from channel input.
- RC-505-style flow: REC stop → auto-loop playback.
- Live scrolling waveform during recording (6s rolling window).
- 8 loop slots — click to save (current loop region + full effect state), right-click to clear, keys 1–8 to recall.
- Per-slot snapshot persists: filter/Q/LFO, ghost amount/time, haze amount/preset/freeze, tape mix/machine, speed, BPM, signal-path order.
- Auto-trim silence on slot save with 2 ms equal-power crossfade for click-free wrap.
- Folder browse with `◫ − +` to step through audio files.

### Loop region
- Drag across the waveform to highlight a custom loop.
- Tempo-relative size buttons: 1/16, 1/8, 1/4, 1/2, 1B, 2B.
- NUDGE slider translates the loop region live; commits on release.
- Loop wrap fade slope: −12 dB, −24 dB, or brick-wall (BKWL).
- Ctrl+scroll the waveform to zoom in/out.

### Effects (drag knob icons to reorder the chain)
- **FILTER** — DJ-style state-variable LP/HP sweep, 0=LP, 0.5=bypass, 1=HP. Three Q modes. Tempo-synced sine LFO (OFF / 1/2 / 1/4 / 1/8 / 1/16 / 1/32 / 1/64 / 1/128 of a beat) with SM/MD/LG depth.
- **GHOST** — filtered feedback delay, tempo-synced 1/16 / 1/8 / 1/4 with a 1-pole LP in the feedback path.
- **HAZE** — six reverb presets: VAULT (lush hall), CHROME (bright plate + pre-HPF), NEST (small room), MIST (medium warm chamber), ABYSS (huge dark with post-LP), AURA (cascading +1-oct shimmer with crossfaded dual read pointers). FREEZE toggle for infinite tail.
- **TAPE** — wet/dry saturation with three machines: SAT (warm transformer), WOW (modulated delay), LO-FI (bit-crush + steep LP).

### Tempo
- Internal BPM (40–240) with drag-on-OLED + dedicated TAP TEMPO button.
- Host transport BPM auto-overrides when running as a plugin.
- All tempo-synced FX (loop SIZE, FILTER LFO, GHOST delay) follow.
- Per-slot BPM stored — each slot can have its own groove.

### Preamp
- INPUT wet/dry chain: 24:1 compressor → makeup → asymmetric saturation → tape HF roll-off → DC blocker.
- Three voices: VINTAGE (slow/warm), FET (fast/aggressive), OPTO (smooth/transparent).
- Applied to BOTH record and playback so the INPUT knob shapes what you hear.
- LED meters input peak level continuously.

### Transport & playback
- SPEED knob: 0.5×..2× varispeed. Slowing adds asymmetric tape grime (bit-crush + sample-rate reduction).
- Spacebar toggles play/stop globally.
- Grab the vinyl reel with the mouse to scratch — per-sample rate smoothing, cartridge LP for vinyl warmth.
- OVERDUB toggle to layer the playing sample onto new recordings.

### Plugin / host integration
- Standalone runs silent until a sample is loaded (no mic feedback).
- Plugin mode passes host audio through unchanged when no sample is playing — effects still apply.
- Drag the ↓ export button to drop the current loop region as a 24-bit WAV onto any DAW track, Explorer, or Finder.
- VST3 categorised as `Fx Sampler`. AU type `kAudioUnitType_Effect` on macOS.

### Aesthetic / UX
- Silver brushed-aluminium chassis with optional `bg.jpg/png` overlay.
- Brushed-metal notched-pot knobs with 13 tick marks; active position brightens to accent.
- OLED-style BPM window with neon-glow cascade.
- LED-circle indicators for INPUT comp voice and TAPE machine.
- Vector snowflake FREEZE icon (font-independent).
- Double-click the SP·L wordmark to randomise the accent color across the whole panel.
- Per-user accent theme persists in slot snapshots.

### Safety / production hardening
- All audio-thread atomics are properly typed (`std::atomic<double>` for BPM, etc.).
- Filter cutoff clamped below Nyquist so `tan()` never overflows at low sample rates.
- OVERDUB position guards against NaN / Inf / negative positions.
- `prepareToPlay` rejects bad sample rates with a 48 kHz fallback.
- Sample buffers checked for zero channels in addition to zero length.
- SHIMMER (AURA) uses two staggered read pointers with sine-windowed crossfade to mask delay-line lap clicks.
- Diagnostic log lives in the per-user app data directory (`%APPDATA%\SPOOL\spool.log` on Windows, `~/Library/Application Support/SPOOL/` on macOS, `~/.config/SPOOL/` on Linux). Cleared at each launch.

### Build
- CMake 3.22+, JUCE 8.0.4 via FetchContent.
- C++17, MSVC `/MT` static runtime, juce_recommended_lto_flags.
- Build targets: `SPOOL_Standalone`, `SPOOL_VST3`, and `SPOOL_AU` (macOS only).
- Warning-clean build (no C4456 shadowing, no unused-variable warnings).

[1.0.1]: https://github.com/itselliott/spool/releases/tag/v1.0.1
[1.0.0]: https://github.com/itselliott/spool/releases/tag/v1.0.0
