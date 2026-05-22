#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <atomic>
#include <memory>

//==============================================================================
/**
    A loaded audio sample, ref-counted so the audio thread can hold a local
    reference while the message thread swaps in a new one.
*/
struct Sample : public juce::ReferenceCountedObject
{
    using Ptr = juce::ReferenceCountedObjectPtr<Sample>;

    juce::AudioBuffer<float> buffer;
    double sourceSampleRate = 0.0;
    juce::String name;
};

//==============================================================================
/**
    SPOOL — TP-7-inspired sampler/recorder.

    v0.1: single tape track, drag/drop sample, in-memory playback,
          transport, WAV export.
*/
class SpoolAudioProcessor : public juce::AudioProcessor
{
public:
    SpoolAudioProcessor();
    ~SpoolAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    //==============================================================================
    const juce::String getName() const override { return "SPOOL"; }
    bool   acceptsMidi() const override  { return false; }
    bool   producesMidi() const override { return false; }
    bool   isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int  getNumPrograms() override     { return 1; }
    int  getCurrentProgram() override  { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    // --- Sample loading & playback (message-thread API) -----------------------
    //
    // Returns true on success. Safe to call while audio is running; the audio
    // thread will pick up the new sample on its next block.
    bool loadFile (const juce::File& file);

    void play();
    void stop();
    void togglePlay();
    bool isPlaying() const noexcept            { return playing.load(); }

    void setLooping (bool shouldLoop) noexcept { looping.store (shouldLoop); }
    bool isLooping() const noexcept            { return looping.load(); }

    // 0..1 across the loaded sample, regardless of host sample rate.
    float getNormalisedPosition() const noexcept;
    double getDurationSeconds() const noexcept;

    // Jump to a 0..1 normalised position in the sample.
    void seekNormalised (float norm) noexcept;

    // Clear the currently loaded sample (and stop playback / reset thumbnail).
    void clearSample();

    // --- Folder browsing: select a folder + cycle through its audio files ---
    void setFolder        (const juce::File& folder);   // scan + load first
    void loadNextInFolder();                            // wraps around
    void loadPrevInFolder();
    int   getFolderFileCount() const noexcept { return folderFiles.size(); }
    int   getFolderIndex()     const noexcept { return folderIndex; }
    bool  isFolderActive() const noexcept     { return folderFiles.size() > 0; }

    // Manually set the LOOP anchor to a 0..1 normalised position in the
    // current sample. Picked up by the audio thread on the next block.
    void setLoopAnchorFromNormalised (float norm) noexcept;

    // Custom drag-defined loop region (start/end in sample positions). When
    // set, overrides the LOOP knob's length-based window. Pass normalised
    // 0..1 values; the processor converts to source samples internally.
    void setCustomLoopRegionNormalised (float startNorm, float endNorm) noexcept;
    void clearCustomLoop() noexcept;
    bool hasCustomLoop() const noexcept;

    // Set the loop window to a tempo-synced length, anchored at the current
    // playhead. beats = how many beats long (e.g. 0.25 = 1/16, 4 = 1 bar).
    void setLoopSizeBeats (float beats) noexcept;

    // For the editor to draw the loop highlight on the waveform.
    // Returns the current loop region in normalised 0..1 of the source sample
    // (either from the custom drag region or from the LOOP knob calculation).
    float getLoopRegionStartNormalised() const noexcept;
    float getLoopRegionEndNormalised()   const noexcept;

    // --- INPUT: wet/dry preamp (HEAVY 12:1 compression + warmth) -------------
    //
    // The knob is a 0..1 wet/dry mix between the raw input (dry) and a one-knob
    // preamp chain (wet): full 12:1 compressor → makeup → asymmetric saturation
    // → tape HF rolloff → DC blocker. Three compressor voices: VINTAGE (slow,
    // glue), FET (fast, punchy), OPTO (smooth, transparent) — selected via the
    // button under the knob.
    void  setInputMix (float m) noexcept { inputMix.store (juce::jlimit (0.0f, 1.0f, m)); }
    float getInputMix() const noexcept   { return inputMix.load(); }

    static constexpr int kNumCompModes = 3;
    enum { CompVintage = 0, CompFet = 1, CompOpto = 2 };
    void setInputCompMode (int mode) noexcept { inputCompMode.store (juce::jlimit (0, kNumCompModes - 1, mode)); }
    int  getInputCompMode() const noexcept    { return inputCompMode.load(); }
    static const char* getInputCompLabel (int mode) noexcept;

    // --- Varispeed (0.5x .. 2x) — affects pitch like real tape --------------
    void  setPlaybackSpeed (float s) noexcept { playbackSpeed.store (juce::jlimit (0.5f, 2.0f, s)); }
    float getPlaybackSpeed() const noexcept   { return playbackSpeed.load(); }

    // --- Sample gain (trim) — playback volume of the loaded sample ---------
    void  setSampleGain (float g) noexcept { sampleGain.store (juce::jlimit (0.0f, 2.0f, g)); }
    float getSampleGain() const noexcept   { return sampleGain.load(); }

    // --- DJ scratch — drag the vinyl wheel to scrub forward/backward -------
    // When scratchActive is true, the playback RATE is taken from scratchRate
    // instead of the SPEED knob. Negative rate = reverse playback. Setting
    // scratchActive=false returns control to the SPEED knob.
    void setScratching (bool on)        noexcept { scratchActive.store (on); }
    void setScratchRate (float rate)    noexcept
    {
        // Clamp aggressively — a fast mouse-flick can produce huge angular
        // velocities and we don't want pos to overflow on the audio thread.
        scratchRate.store (std::isfinite (rate) ? juce::jlimit (-16.0f, 16.0f, rate) : 0.0f);
    }
    bool isScratching() const           noexcept { return scratchActive.load(); }

    // --- FILTER: DJ-style LP/HP sweep (0=LP, 0.5=bypass, 1=HP) -------------
    void  setFilterPos (float p)  noexcept { filterPos.store (juce::jlimit (0.0f, 1.0f, p)); }
    float getFilterPos() const    noexcept { return filterPos.load(); }

    static constexpr int kNumFilterQModes = 3;     // LOW / MID / HI
    void setFilterQMode (int m)   noexcept { filterQMode.store (juce::jlimit (0, kNumFilterQModes - 1, m)); }
    int  getFilterQMode() const   noexcept { return filterQMode.load(); }
    static const char* getFilterQLabel (int m) noexcept;

    // Filter LFO: tempo-synced sine that modulates filterPos.
    // 0 = OFF, 1..7 = 1/2, 1/4, 1/8, 1/16, 1/32, 1/64, 1/128 of a beat (period).
    static constexpr int kNumFilterLfoRates = 8;
    void setFilterLfoRate (int r)  noexcept { filterLfoRate.store (juce::jlimit (0, kNumFilterLfoRates - 1, r)); }
    int  getFilterLfoRate() const  noexcept { return filterLfoRate.load(); }
    static const char* getFilterLfoRateLabel (int r) noexcept;

    // LFO depth — 0 = subtle (10% sweep), 1 = medium (25%), 2 = big (50%).
    static constexpr int kNumFilterLfoRanges = 3;
    void setFilterLfoRange (int r) noexcept { filterLfoRange.store (juce::jlimit (0, kNumFilterLfoRanges - 1, r)); }
    int  getFilterLfoRange() const noexcept { return filterLfoRange.load(); }
    static const char* getFilterLfoRangeLabel (int r) noexcept;

    // --- Signal path order (the three JUICE effects) -----------------------
    // Defaults to {0,1,2} = FILTER → GHOST → HAZE. Each entry is an effect
    // INDEX (0=filter, 1=ghost, 2=haze). The editor can reorder.
    enum Effect { EffectFilter = 0, EffectGhost = 1, EffectHaze = 2 };
    static constexpr int kNumEffects = 3;
    void setSignalOrder (int pos0, int pos1, int pos2) noexcept
    {
        signalPathOrder[0].store (juce::jlimit (0, kNumEffects - 1, pos0));
        signalPathOrder[1].store (juce::jlimit (0, kNumEffects - 1, pos1));
        signalPathOrder[2].store (juce::jlimit (0, kNumEffects - 1, pos2));
    }
    int getSignalOrder (int pos) const noexcept
    {
        return signalPathOrder[juce::jlimit (0, kNumEffects - 1, pos)].load();
    }

    // --- GHOST: tempo-synced filtered feedback delay -----------------------
    // Each delayed repeat is lowpassed in the feedback path so the tail
    // darkens. As the knob turns up, both feedback AND filter cut-off
    // intensify — at full crank you get a cascading dub-style wash.
    void  setGhostAmount (float a) noexcept { ghostAmount.store (juce::jlimit (0.0f, 1.0f, a)); }
    float getGhostAmount() const noexcept   { return ghostAmount.load(); }

    static constexpr int kNumGhostTimes = 3;  // 1/16, 1/8, 1/4 of a beat
    void setGhostTimeMode (int m) noexcept { ghostTimeMode.store (juce::jlimit (0, kNumGhostTimes - 1, m)); }
    int  getGhostTimeMode() const noexcept { return ghostTimeMode.load(); }
    static const char* getGhostTimeLabel (int m) noexcept;

    // --- TAPE: wet/dry mix of one of three tape-machine flavours -----------
    void  setTapeMix (float m) noexcept { tapeMix.store (juce::jlimit (0.0f, 1.0f, m)); }
    float getTapeMix() const noexcept   { return tapeMix.load(); }

    static constexpr int kNumTapeMachines = 3;
    void setTapeMachine (int m) noexcept { tapeMachine.store (juce::jlimit (0, kNumTapeMachines - 1, m)); }
    int  getTapeMachine() const noexcept { return tapeMachine.load(); }
    static const char* getTapeMachineLabel (int m) noexcept;

    // --- HAZE: reverb size knob + FREEZE toggle ----------------------------
    void  setHazeAmount (float a) noexcept { hazeAmount.store (juce::jlimit (0.0f, 1.0f, a)); }
    float getHazeAmount() const noexcept   { return hazeAmount.load(); }

    void setHazeFrozen (bool on) noexcept { hazeFrozen.store (on); }
    bool isHazeFrozen() const noexcept    { return hazeFrozen.load(); }

    // HAZE preset cycle (Valhalla/Lexicon-inspired). 0..5 mapping below.
    // The knob remains the wet/dry; each preset shapes the reverb character.
    static constexpr int kNumHazePresets = 6;
    enum HazePreset { PresetHall = 0, PresetPlate, PresetRoom,
                      PresetChamber, PresetCave, PresetShimmer };
    void setHazePreset (int p) noexcept { hazePreset.store (juce::jlimit (0, kNumHazePresets - 1, p)); }
    int  getHazePreset() const noexcept { return hazePreset.load(); }
    static const char* getHazePresetLabel (int p) noexcept;

    // --- Overdub: when ON, recording layers input on top of current playback
    void setOverdubEnabled (bool on) noexcept { overdubEnabled.store (on); }
    bool isOverdubEnabled() const noexcept    { return overdubEnabled.load(); }

    // Peak level (0..1) of the input bus over the last processBlock — for a
    // small VU dot on the INPUT knob so the user can see audio is arriving.
    float getInputPeakLevel()    const noexcept { return inputPeakLevel.load(); }
    int   getInputChannelCount() const noexcept { return inputChannelCount.load(); }

    // --- LOOP edge cutoff: fade-curve style at loop wraps and on stop/pause --
    //
    // 0 = -12 (soft, ~30ms),  1 = -24 (steeper, ~8ms),  2 = BRICK (no fade).
    static constexpr int kNumLoopCutoffModes = 3;
    void setLoopCutoffMode (int mode) noexcept { loopCutoffMode.store (juce::jlimit (0, kNumLoopCutoffModes - 1, mode)); }
    int  getLoopCutoffMode() const noexcept    { return loopCutoffMode.load(); }
    static const char* getLoopCutoffLabel (int mode) noexcept;

    juce::String getLoadedSampleName() const;

    // Used by the editor to draw the waveform.
    juce::AudioThumbnail& getThumbnail() noexcept           { return thumbnail; }
    juce::AudioFormatManager& getFormatManager() noexcept   { return formatManager; }

    // Export the currently loaded sample to a WAV file.
    // For v0 this is a straight copy of the loaded buffer; once the JUICE chain
    // is in place we'll upgrade this to an offline render of the processed signal.
    bool exportToWav (const juce::File& destination);

    // Render the currently selected loop region (custom drag-highlight,
    // knob-based loop, or — fallback — the whole sample) to a temp WAV
    // with the full effect chain baked in. Used by the drag-out so the
    // dropped file already sounds like it does live. Returns the file
    // (empty if no sample loaded). The caller does not need to delete the
    // file — it lives in the OS temp dir and gets cleaned up on session end.
    juce::File renderLoopToTempWav();

    // --- Recording (message-thread API) ---------------------------------------
    //
    // Starts capturing input audio into a pre-allocated buffer. When stopped,
    // the captured audio is swapped in as the new currentSample.
    void startRecording();
    void stopRecording();
    bool isRecording() const noexcept { return recording.load(); }
    float getRecordProgress() const noexcept;     // 0..1, capacity fill

    // Live-recording read-only access for waveform feedback. Audio thread
    // appends to recordBuffer up to recordPos samples; the editor reads it
    // (benign race — worst case we draw a slightly old snapshot).
    int  getRecordedSampleCount() const noexcept { return recordPos.load(); }
    const juce::AudioBuffer<float>& getRecordBuffer() const noexcept { return recordBuffer; }
    double getHostSampleRate() const noexcept { return hostSampleRate; }

    static constexpr double kMaxRecordSeconds = 300.0; // 5 minutes

    // --- Loop slots: capture and recall up to 8 loop snapshots ---------------
    //
    // Save captures the current loop window AUDIO plus a snapshot of all the
    // knob settings (input mix/comp, sample gain, speed, loop length/cutoff,
    // nudge offset). Load restores both — same sound, ready to tweak further.
    static constexpr int kNumSlots = 8;

    struct SlotSnapshot
    {
        // INPUT preamp.
        float inputMix             = 0.0f;
        int   inputCompMode        = 0;
        // Sample trim + tempo.
        float sampleGain           = 1.0f;
        float playbackSpeed        = 1.0f;
        // FILTER (DJ LP/HP).
        float filterPos            = 0.5f;
        int   filterQMode          = 1;
        int   filterLfoRate        = 0;
        int   filterLfoRange       = 1;
        double bpm                 = 120.0;
        // GHOST.
        float ghostAmount          = 0.0f;
        int   ghostTimeMode        = 1;
        // HAZE.
        float hazeAmount           = 0.0f;
        bool  hazeFrozen           = false;
        int   hazePreset           = 0;
        // TAPE.
        float tapeMix              = 0.0f;
        int   tapeMachine          = 0;
        // LOOP cutoff fade slope.
        int   loopCutoffMode       = 0;
        // Signal order.
        int   signalOrder[3]       = { 0, 1, 2 };
        bool  valid                = false;
    };

    bool saveCurrentLoopToSlot (int slot);
    bool loadSlot              (int slot);
    void clearSlot             (int slot);
    bool isSlotFilled (int slot) const noexcept;

    // --- JUICE: LOOP -----------------------------------------------------------
    //
    // Dynamic loop-length knob. 0 = off (normal playback), 1..7 = 2 bars down
    // to 1/32. When turned from 0 to non-zero, the current playhead position is
    // captured as the loop anchor; playback then loops within [anchor, anchor+N].
    static constexpr int kNumLoopLengths = 8;
    void setLoopLength (int mode) noexcept       { loopLengthMode.store (juce::jlimit (0, kNumLoopLengths - 1, mode)); }
    int  getLoopLength() const noexcept          { return loopLengthMode.load(); }
    bool isLoopWrappedThisBlock() const noexcept { return loopWrapped.exchange (false); }

    // NUDGE: shift the captured loop anchor forward/backward in seconds.
    // Range is -kMaxNudgeSeconds .. +kMaxNudgeSeconds.
    static constexpr float kMaxNudgeSeconds = 2.0f;
    void  setLoopAnchorOffset (float seconds) noexcept
        { loopAnchorOffsetSeconds.store (juce::jlimit (-kMaxNudgeSeconds, kMaxNudgeSeconds, seconds)); }
    float getLoopAnchorOffset() const noexcept   { return loopAnchorOffsetSeconds.load(); }
    // The editor calls this each tick: returns true once after each fresh
    // anchor capture so the slider can snap back to 0.
    bool  consumeNudgeResetFlag() noexcept       { return loopShouldResetNudge.exchange (false); }

    // Bake the current nudge offset into the active loop region — translates
    // [customLoopStart, customLoopEnd] (or knob-based loopAnchorBase) by
    // offset*sr samples, clamped to sample bounds, then resets the offset
    // to 0. Called when the NUDGE slider is released so the slider snaps
    // back to centre and you can keep nudging from the new position.
    void  commitNudgeOffset() noexcept;

    // Internal BPM (used in standalone; later we'll override from host playhead).
    // Implemented out-of-line in the .cpp so it can recompute the active loop
    // window when BPM changes (the size buttons store a beat count we replay).
    void   setBpm (double bpm) noexcept;
    double getBpm() const noexcept { return internalBpm.load(); }

    // For UI labels.
    static const char* getLoopLengthLabel (int mode) noexcept;

private:
    // Offline render of a source buffer through the current effect chain.
    // Used by both EXPORT and slot SAVE so the slot stores audio that
    // already sounds the way it does live.
    void renderProcessed (const juce::AudioBuffer<float>& src,
                          double srcSampleRateValue,
                          juce::AudioBuffer<float>& out);

    juce::AudioFormatManager   formatManager;
    juce::AudioThumbnailCache  thumbnailCache { 4 };
    juce::AudioThumbnail       thumbnail      { 512, formatManager, thumbnailCache };

    // The currently-loaded sample, swapped atomically under sampleLock.
    juce::SpinLock sampleLock;
    Sample::Ptr    currentSample;

    // Transport state.
    std::atomic<bool>    playing      { false };
    std::atomic<bool>    looping      { true };
    std::atomic<double>  playPosition { 0.0 }; // SOURCE samples (fractional for rate-correct playback)

    // Shared per-channel state caps for filters / envelopes.
    static constexpr int kMaxFilterChannels = 8;

    // INPUT preamp.
    std::atomic<float>   inputMix          { 0.0f };  // wet/dry of preamp chain
    std::atomic<int>     inputCompMode     { 0 };     // 0 = VINTAGE, 1 = FET, 2 = OPTO
    std::atomic<float>   inputPeakLevel    { 0.0f };  // updated each block for UI meter
    std::atomic<int>     inputChannelCount { 0 };     // updated each block; 0 = no input bus

    // Varispeed + overdub + sample trim.
    std::atomic<float>   playbackSpeed   { 1.0f };
    std::atomic<bool>    overdubEnabled  { false };
    std::atomic<float>   sampleGain      { 1.0f };

    // DJ scratch state.
    std::atomic<bool>    scratchActive   { false };
    std::atomic<float>   scratchRate     { 0.0f };
    // Audio-thread-only: smoothed rate (lerps toward target per sample to
    // kill zipper noise) + per-channel 1-pole LP state for cartridge warmth.
    double               scratchRateSmoothed { 0.0 };
    float                scratchLpState[kMaxFilterChannels] {};

    // Per-channel state for the OUTPUT comp chain (separate from input state).
    float outCompEnv [kMaxFilterChannels] {};
    float outLpState [kMaxFilterChannels] {};
    float outHpState [kMaxFilterChannels] {};
    float outHpPrev  [kMaxFilterChannels] {};

    // SPEED degradation: as varispeed drifts from 1.0× (especially when
    // slowed down), introduce bit-crush + sample-rate reduction for that
    // crunchy, harmonic, "chingy" lo-fi tape character.
    int   srrCounter                       = 0;
    float srrHold [kMaxFilterChannels]     {};

    // FILTER (DJ LP/HP sweep) state.
    std::atomic<float>       filterPos     { 0.5f };       // 0.5 = bypass
    std::atomic<int>         filterQMode   { 1 };          // default MID
    std::atomic<int>         filterLfoRate  { 0 };         // 0 = OFF
    std::atomic<int>         filterLfoRange { 1 };         // default MED
    double                   filterLfoPhase { 0.0 };       // audio-thread, 0..2π
    float                    svfLow  [kMaxFilterChannels] {};
    float                    svfBand [kMaxFilterChannels] {};

    // GHOST: filtered feedback delay state.
    std::atomic<float>       ghostAmount   { 0.0f };
    std::atomic<int>         ghostTimeMode { 1 };          // default 1/8
    juce::AudioBuffer<float> ghostDelayBuf;
    int                      ghostWritePos = 0;
    float                    ghostFbLp [kMaxFilterChannels] {};

    // Signal path order — read on audio thread.
    std::array<std::atomic<int>, 3> signalPathOrder = { { {0}, {1}, {2} } };

    // TAPE machine state.
    std::atomic<float>       tapeMix     { 0.0f };
    std::atomic<int>         tapeMachine { 0 };  // 0=SAT, 1=WOW, 2=LOFI
    juce::AudioBuffer<float> tapeDelayBuf;       // ~20ms for wow-mod
    int                      tapeDelayWritePos = 0;
    float                    tapeLpState [kMaxFilterChannels] {};
    float                    tapeLfoPhase = 0.0f;

    // HAZE + FREEZE state.
    std::atomic<float> hazeAmount   { 0.0f };
    std::atomic<bool>  hazeFrozen   { false };
    std::atomic<int>   hazePreset   { 0 };

    // SHIMMER preset state: a delay line read at 2× the write rate produces
    // a one-octave-up signal that's fed back into the reverb input, creating
    // the classic cascading shimmer-pad sound.
    juce::AudioBuffer<float> shimmerBuf;
    int                      shimmerWritePos { 0 };
    double                   shimmerReadPos  { 0.0 };
    // Per-channel SHIMMER feedback state — small high-pass to keep the
    // cascading octaves from accumulating low-end mud.
    float shimmerHpState[kMaxFilterChannels] {};
    float shimmerHpPrev [kMaxFilterChannels] {};
    // Per-channel pre/post EQ state for HAZE presets (PLATE high-pass,
    // CAVE low-pass) — single 1-pole filter per slot.
    float hazePreHpState[kMaxFilterChannels] {};
    float hazePreHpPrev [kMaxFilterChannels] {};
    float hazePostLpState[kMaxFilterChannels] {};
    juce::Reverb       hazeReverb;
    juce::AudioBuffer<float> hazeFreezeTail;   // captured when frozen
    int hazeFreezeReadPos = 0;

    // LOOP cutoff (edge fade).
    std::atomic<int>     loopCutoffMode { 0 };     // 0 = -12, 1 = -24, 2 = BRICK

    // Audio-thread-only smoothing for transport stop/pause fade.
    float playGainSmoothed = 0.0f;

    // Compressor state (per-channel envelope follower for the preamp wet path).
    float compEnv[kMaxFilterChannels] {};

    // Stack-friendly pre-allocated dry buffer for the wet/dry mix.
    juce::AudioBuffer<float> dryBuffer;

    // Recording state.
    juce::AudioBuffer<float> recordBuffer;       // pre-allocated to kMaxRecordSeconds * sr
    std::atomic<bool>        recording   { false };
    std::atomic<int>         recordPos   { 0 };  // samples written so far

    // Loop slot storage. Slots are owned by the message thread; the audio
    // thread only sees them indirectly when one is loaded into currentSample.
    std::array<Sample::Ptr, kNumSlots>   slots          {};
    std::array<SlotSnapshot, kNumSlots>  slotSnapshots  {};
    juce::File sessionDir;

    // Folder browsing state.
    juce::File                currentFolder;
    juce::Array<juce::File>   folderFiles;
    int                       folderIndex = -1;

    // Diagnostic logging — writes to C:/repo/spool/spool.log so we can see
    // what was happening leading up to a crash.
    std::unique_ptr<juce::FileLogger> diagLogger;

    // LOOP state (dynamic loop-length knob).
    std::atomic<int>          loopLengthMode { 0 };
    mutable std::atomic<bool> loopWrapped    { false };
    std::atomic<float>        loopAnchorOffsetSeconds { 0.0f };
    std::atomic<bool>         loopShouldResetNudge    { false };
    std::atomic<double>       pendingLoopAnchor       { -2.0 };  // user-requested anchor; -2 = none
    std::atomic<double>       customLoopStart         { -1.0 };  // user-drag region start (samples); -1 = unset
    std::atomic<double>       customLoopEnd           { -1.0 };
    int    prevLoopLengthMode = 0;     // audio-thread only — transition detection
    double loopAnchorBase     = -1.0;  // audio-thread only — captured at 0→non-zero transition
    std::atomic<double> internalBpm { 120.0 };

    // Last SIZE button pressed, in beats (0 = none / drag-highlight loop).
    // Stored so setBpm() can recompute the loop end-point on tempo change.
    std::atomic<float>  lastLoopSizeBeats { 0.0f };

    // Input-warmth filter state (one-pole HP "DC blocker" + one-pole LP tape-loss).
    float warmthHpState[kMaxFilterChannels] {};
    float warmthHpPrev [kMaxFilterChannels] {};
    float warmthLpState[kMaxFilterChannels] {};
    float warmthHpCoeff = 0.99f;   // computed in prepareToPlay (~20Hz)
    float warmthLpCoeff = 0.35f;   // computed in prepareToPlay (~10kHz)

    // Host sample rate (set in prepareToPlay).
    double hostSampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpoolAudioProcessor)
};
