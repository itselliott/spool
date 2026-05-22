# Changelog

All notable changes to SPOOL. Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

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

[1.0.0]: https://github.com/itselliott/spool/releases/tag/v1.0.0
