#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    // LOOP: length of the loop window expressed as a fraction of one beat.
    // index 0 = OFF (normal playback); the rest cover 2 bars down to 1/32.
    constexpr double kLoopFractionOfBeat[SpoolAudioProcessor::kNumLoopLengths] =
    {
        0.0,    // OFF
        8.0,    // 2 bars (4/4)
        4.0,    // 1 bar
        2.0,    // 1/2
        1.0,    // 1/4
        0.5,    // 1/8
        0.25,   // 1/16
        0.125,  // 1/32
    };

    constexpr const char* kLoopLengthLabels[SpoolAudioProcessor::kNumLoopLengths] =
    {
        "OFF", "2B", "1B", "1/2", "1/4", "1/8", "1/16", "1/32"
    };
}

const char* SpoolAudioProcessor::getLoopLengthLabel (int mode) noexcept
{
    return kLoopLengthLabels[juce::jlimit (0, kNumLoopLengths - 1, mode)];
}

const char* SpoolAudioProcessor::getLoopCutoffLabel (int mode) noexcept
{
    static const char* kLabels[] = { "-12", "-24", "BKWL" };
    return kLabels[juce::jlimit (0, kNumLoopCutoffModes - 1, mode)];
}

const char* SpoolAudioProcessor::getInputCompLabel (int mode) noexcept
{
    static const char* kLabels[] = { "VINTAGE", "FET", "OPTO" };
    return kLabels[juce::jlimit (0, kNumCompModes - 1, mode)];
}

const char* SpoolAudioProcessor::getGhostTimeLabel (int mode) noexcept
{
    static const char* kLabels[] = { "1/16", "1/8", "1/4" };
    return kLabels[juce::jlimit (0, kNumGhostTimes - 1, mode)];
}

const char* SpoolAudioProcessor::getFilterQLabel (int mode) noexcept
{
    static const char* kLabels[] = { "LOW", "MID", "HI" };
    return kLabels[juce::jlimit (0, kNumFilterQModes - 1, mode)];
}

const char* SpoolAudioProcessor::getTapeMachineLabel (int mode) noexcept
{
    static const char* kLabels[] = { "SAT", "WOW", "LO-FI" };
    return kLabels[juce::jlimit (0, kNumTapeMachines - 1, mode)];
}

const char* SpoolAudioProcessor::getFilterLfoRateLabel (int r) noexcept
{
    static const char* kLabels[] = { "OFF", "1/2", "1/4", "1/8", "1/16", "1/32", "1/64", "1/128" };
    return kLabels[juce::jlimit (0, kNumFilterLfoRates - 1, r)];
}

const char* SpoolAudioProcessor::getFilterLfoRangeLabel (int r) noexcept
{
    static const char* kLabels[] = { "SM", "MD", "LG" };
    return kLabels[juce::jlimit (0, kNumFilterLfoRanges - 1, r)];
}

const char* SpoolAudioProcessor::getHazePresetLabel (int p) noexcept
{
    // Creative names that hint at the character without naming the algorithm:
    //   VAULT  — big lush (was HALL)
    //   CHROME — bright dense (was PLATE)
    //   NEST   — small intimate (was ROOM)
    //   MIST   — medium warm (was CHAMBER)
    //   ABYSS  — huge dark (was CAVE)
    //   AURA   — ethereal shimmer (was SHIMMER)
    static const char* kLabels[] = { "VAULT", "CHROME", "NEST", "MIST", "ABYSS", "AURA" };
    return kLabels[juce::jlimit (0, kNumHazePresets - 1, p)];
}

const char* SpoolAudioProcessor::getArpModeLabel (int m) noexcept
{
    static const char* kLabels[] = { "UP", "DN", "UPDN", "RND" };
    return kLabels[juce::jlimit (0, kNumArpModes - 1, m)];
}

const char* SpoolAudioProcessor::getArpRateLabel (int r) noexcept
{
    static const char* kLabels[] = { "1/4", "1/8", "1/16", "1/32" };
    return kLabels[juce::jlimit (0, kNumArpRates - 1, r)];
}

namespace
{
    // Beat fraction per arp step (1.0 = one beat = quarter note).
    constexpr double kArpRateFractions[SpoolAudioProcessor::kNumArpRates] =
        { 1.0, 0.5, 0.25, 0.125 };

    // Map a 0..1 mod-wheel value to a discrete arp rate index.
    inline int modWheelToArpRate (float w) noexcept
    {
        if (w < 0.25f) return SpoolAudioProcessor::ArpRate14;
        if (w < 0.50f) return SpoolAudioProcessor::ArpRate18;
        if (w < 0.75f) return SpoolAudioProcessor::ArpRate116;
        return                SpoolAudioProcessor::ArpRate132;
    }
}

namespace
{
    // Per-compressor-voice parameters. All three run at 12:1, but their attack,
    // release, saturation drive, and tape-rolloff cutoff give them distinct
    // character — slow/warm, fast/aggressive, smooth/transparent.
    struct CompParams { float attackMs, releaseMs, satDrive, lpHz; };

    inline CompParams getCompParams (int mode) noexcept
    {
        switch (mode)
        {
            case SpoolAudioProcessor::CompFet:    return { 0.5f,  50.0f, 2.2f, 14000.0f };
            case SpoolAudioProcessor::CompOpto:   return { 10.0f, 200.0f, 1.1f, 10000.0f };
            case SpoolAudioProcessor::CompVintage:
            default:                              return { 15.0f, 300.0f, 1.6f, 12000.0f };
        }
    }
}

//==============================================================================
SpoolAudioProcessor::SpoolAudioProcessor()
    : AudioProcessor (BusesProperties()
        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    // ---- Diagnostic logger — first thing so subsequent ops can log ----
    // Production builds write to the OS's per-user app-data directory so we
    // don't depend on (and don't write into) any specific install location.
    // On Windows that's %APPDATA%/SPOOL/, on macOS ~/Library/Logs/SPOOL/,
    // on Linux ~/.config/SPOOL/.
    {
        const auto logDir = juce::File::getSpecialLocation (
            juce::File::userApplicationDataDirectory).getChildFile ("SPOOL");
        logDir.createDirectory();
        const auto logFile = logDir.getChildFile ("spool.log");
        logFile.deleteFile();    // start fresh each launch
        diagLogger.reset (new juce::FileLogger (logFile, "SPOOL launched", 0));
        juce::Logger::setCurrentLogger (diagLogger.get());
    }
    juce::Logger::writeToLog ("ctor: SpoolAudioProcessor starting");

    formatManager.registerBasicFormats();

    const auto stamp = juce::Time::getCurrentTime().formatted ("%Y%m%d_%H%M%S");
    sessionDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                     .getChildFile ("SPOOL")
                     .getChildFile ("session_" + stamp);
    sessionDir.createDirectory();
    juce::Logger::writeToLog ("ctor: sessionDir=" + sessionDir.getFullPathName());

    // Auto-restore the last-session set (slots + effect state + samples).
    // Only fires in standalone use — when SPOOL loads as a plugin, the DAW
    // calls setStateInformation with its own saved state right after the
    // constructor, which overwrites whatever we restored here.
    const auto lastSession = getLastSessionFile();
    if (lastSession.existsAsFile())
    {
        juce::Logger::writeToLog ("ctor: auto-restoring last session from "
                                  + lastSession.getFullPathName());
        loadSetFromFile (lastSession);
    }
}

SpoolAudioProcessor::~SpoolAudioProcessor()
{
    juce::Logger::writeToLog ("dtor: SpoolAudioProcessor");

    // Auto-save current state so standalone re-launch restores the same
    // samples + slots + knob positions. Plugin hosts handle save/restore
    // via getStateInformation, but this file is still a useful backup.
    saveSetToFile (getLastSessionFile());

    if (sessionDir.exists() && sessionDir.getFullPathName().contains ("session_"))
        sessionDir.deleteRecursively();

    // Tear down logger LAST so anything else can still log on the way down.
    if (diagLogger != nullptr)
    {
        juce::Logger::setCurrentLogger (nullptr);
        diagLogger.reset();
    }
}

//==============================================================================
void SpoolAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::Logger::writeToLog (juce::String::formatted ("prepareToPlay: sr=%.0f block=%d",
                                                       sampleRate, samplesPerBlock));
    // Sanity: a malformed host might pass 0 / negative; downstream calcs
    // divide by sampleRate and would produce Inf/NaN coefficients forever.
    if (! (sampleRate > 0.0)) sampleRate = 48000.0;
    hostSampleRate = sampleRate;

    // Pre-allocate the record buffer once we know the host sample rate.
    const int maxFrames = (int) (kMaxRecordSeconds * sampleRate);
    if (recordBuffer.getNumSamples() != maxFrames || recordBuffer.getNumChannels() != 2)
        recordBuffer.setSize (2, maxFrames, false, true, true);

    // Pre-allocate the dry buffer for wet/dry mixing in the preamp.
    dryBuffer.setSize (2, juce::jmax (samplesPerBlock, 4096), false, true, true);

    // Input-warmth filter coefficients.
    const double twoPiOverSr = juce::MathConstants<double>::twoPi / sampleRate;
    warmthHpCoeff = (float) std::exp (-twoPiOverSr * 25.0);
    warmthLpCoeff = (float) (1.0 - std::exp (-twoPiOverSr * 12000.0));   // tape rolloff at 12k

    for (int ch = 0; ch < kMaxFilterChannels; ++ch)
    {
        warmthHpState[ch] = 0.0f;
        warmthHpPrev [ch] = 0.0f;
        warmthLpState[ch] = 0.0f;
        compEnv      [ch] = 0.0f;
        outCompEnv   [ch] = 0.0f;
        outLpState   [ch] = 0.0f;
        outHpState   [ch] = 0.0f;
        outHpPrev    [ch] = 0.0f;
        srrHold      [ch] = 0.0f;
        ghostFbLp    [ch] = 0.0f;
        scratchLpState[ch] = 0.0f;
        lofiSrrHold  [ch] = 0.0f;
        lofiLpState  [ch] = 0.0f;
    }
    lofiSrrCounter = 0;
    scratchRateSmoothed = 0.0;

    // Up to ~600 ms of delay covers 1/4 at 100 BPM.
    const int ghMax = (int) (0.6 * sampleRate);
    if (ghostDelayBuf.getNumSamples() != ghMax || ghostDelayBuf.getNumChannels() != 2)
        ghostDelayBuf.setSize (2, ghMax, false, true, true);
    ghostWritePos = 0;

    // ~25 ms delay line for tape WOW modulation.
    const int tpMax = (int) (0.025 * sampleRate);
    if (tapeDelayBuf.getNumSamples() != tpMax || tapeDelayBuf.getNumChannels() != 2)
        tapeDelayBuf.setSize (2, juce::jmax (tpMax, 256), false, true, true);
    tapeDelayWritePos = 0;
    tapeLfoPhase = 0.0f;

    playGainSmoothed = playing.load() ? 1.0f : 0.0f;

    // Reset MIDI collector — editor sliders (pitch bend / mod wheel) pump
    // messages into it; we drain it at the top of processBlock.
    midiCollector.reset (sampleRate);

    hazeReverb.setSampleRate (sampleRate);

    // SHIMMER octave-up delay line — needs ~500 ms so the read pointer (2×
    // rate) can stay safely behind the write pointer without catching up.
    const int shMax = juce::jmax (8192, (int) (0.5 * sampleRate));
    if (shimmerBuf.getNumSamples() != shMax || shimmerBuf.getNumChannels() != 2)
        shimmerBuf.setSize (2, shMax, false, true, true);
    shimmerBuf.clear();
    shimmerWritePos = 0;
    shimmerReadPos  = 0.0;
    for (int ch = 0; ch < kMaxFilterChannels; ++ch)
    {
        shimmerHpState[ch] = shimmerHpPrev[ch] = 0.0f;
        hazePreHpState[ch] = hazePreHpPrev[ch] = 0.0f;
        hazePostLpState[ch] = 0.0f;
    }
}

void SpoolAudioProcessor::releaseResources()
{
}

bool SpoolAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in  = layouts.getMainInputChannelSet();

    // Output must be mono or stereo.
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    // Input can be mono OR stereo OR disabled — we adapt internally
    // (a mono mic gets duplicated to stereo on capture).
    if (! in.isDisabled()
        && in != juce::AudioChannelSet::mono()
        && in != juce::AudioChannelSet::stereo())
        return false;

    return true;
}

void SpoolAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                        juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numInChannels  = getTotalNumInputChannels();
    const auto numOutChannels = getTotalNumOutputChannels();
    const auto numSamples     = buffer.getNumSamples();

    // Surface channel count to UI for diagnostics ("NO INPUT DEVICE" warning).
    inputChannelCount.store (numInChannels);

    // ---- Snapshot recording window for this block --------------------------
    // Input capture (further down) AND MIDI voice rendering (further down
    // still) both need to write into the SAME slice of recordBuffer so the
    // mix lands together. Capture the start position once here and advance
    // recordPos uniformly at the very end of the block. Without this snapshot
    // MIDI-triggered playback never made it into the recording — REC stored
    // the dry input only and the MIDI voices were summed in AFTER capture.
    const bool recordingNow = recording.load();
    const int  recCap       = recordBuffer.getNumSamples();
    const int  recStartPos  = recordingNow ? recordPos.load() : -1;
    const int  recBlockLen  = (recordingNow && recStartPos >= 0 && recStartPos < recCap)
                                ? juce::jmin (numSamples, recCap - recStartPos) : 0;
    if (recordingNow && recBlockLen > 0)
    {
        // Pre-clear the slice — input copyFrom further down overwrites, but
        // when there's no input device (numInChannels == 0) we still want
        // MIDI addSample writes to land on silence, not on stale buffer data.
        for (int ch = 0; ch < recordBuffer.getNumChannels(); ++ch)
            recordBuffer.clear (ch, recStartPos, recBlockLen);
    }

    // ---- Merge editor MIDI sources --------------------------------------
    // Pitch-bend / mod-wheel slider updates from the editor flow through the
    // MidiMessageCollector; on-screen keyboard taps flow through the shared
    // MidiKeyboardState. Both get spliced into the host MIDI buffer here so
    // the voice-parse loop below treats them identically to host MIDI.
    midiCollector.removeNextBlockOfMessages (midi, numSamples);
    keyboardState.processNextMidiBuffer (midi, 0, numSamples, true);

    // ---- MIDI: allocate / release sampler voices ---------------------------
    // Note-On finds a free slot (or steals the oldest) and starts playback at
    // a rate pitch-shifted from the root note (C4). Note-Off triggers the
    // voice's release stage. The actual audio synthesis happens further down,
    // after the transport sample-read so MIDI voices mix on top of (or in
    // place of) the PLAY-button-driven playback.
    for (const auto meta : midi)
    {
        const auto& m = meta.getMessage();
        if (m.isPitchWheel())
        {
            // 14-bit value (0..16383), centred at 8192. Map to ±12 semitones
            // (one octave) — wide enough to actually hear the bend on the
            // SP sampler's pitched playback. (A standard ±2-semi wheel is
            // barely audible at slow playback rates.)
            const float bendNorm = ((float) m.getPitchWheelValue() - 8192.0f) / 8192.0f;
            midiPitchBendSemis.store (juce::jlimit (-12.0f, 12.0f, bendNorm * 12.0f));
            continue;
        }
        if (m.isController() && m.getControllerNumber() == 1)
        {
            midiModWheel.store (juce::jlimit (0.0f, 1.0f,
                                              (float) m.getControllerValue() / 127.0f));
            continue;
        }
        if (m.isNoteOn())
        {
            const int   note = m.getNoteNumber();
            const float vel  = (float) m.getVelocity() / 127.0f;

            // ARP MODE: collect the note into the held-notes list (sorted
            // ascending) and let the arp tick further down trigger voices.
            if (arpEnabled.load())
            {
                bool already = false;
                for (int i = 0; i < numHeldNotes; ++i)
                    if (heldNotes[i] == note) { already = true; break; }
                if (! already && numHeldNotes < kMaxHeldNotes)
                {
                    int i = 0;
                    while (i < numHeldNotes && heldNotes[i] < note) ++i;
                    for (int j = numHeldNotes; j > i; --j) heldNotes[j] = heldNotes[j - 1];
                    heldNotes[i] = note;
                    ++numHeldNotes;
                    // First note of a new chord — fire it immediately so the
                    // user hears something the instant they touch a key,
                    // rather than waiting up to a step interval.
                    if (numHeldNotes == 1)
                    {
                        arpSamplesUntilStep = 0.0;
                        arpStepIndex        = 0;
                        arpDirection        = 1;
                    }
                }
                // Stash the velocity from the most recent press so the next
                // arp step uses it. Held notes themselves only carry pitch.
                arpRng.setSeedRandomly();
                continue;
            }

            const double rate = std::pow (2.0, (note - kMidiRootNote) / 12.0);

            // Re-trigger an existing voice on the same note, otherwise grab a
            // free slot, otherwise steal the oldest non-attacking voice.
            SamplerVoice* target = nullptr;
            for (auto& v : midiVoices)
                if (v.midiNote == note) { target = &v; break; }
            if (target == nullptr)
                for (auto& v : midiVoices)
                    if (v.stage == VoiceStage::Off) { target = &v; break; }
            if (target == nullptr)
            {
                uint64_t oldest = UINT64_MAX;
                for (auto& v : midiVoices)
                    if (v.ageCounter < oldest) { oldest = v.ageCounter; target = &v; }
            }
            target->midiNote   = note;
            target->velocity   = vel;
            target->pos        = 0.0;          // start of sample / loop region
            target->rate       = rate;
            target->env        = 0.0f;
            target->stage      = VoiceStage::Attack;
            target->ageCounter = ++midiVoiceAge;
        }
        else if (m.isNoteOff())
        {
            const int note = m.getNoteNumber();

            // ARP MODE: remove from held-notes list. If the chord is now
            // empty release any currently sounding arp voice.
            if (arpEnabled.load())
            {
                for (int i = 0; i < numHeldNotes; ++i)
                    if (heldNotes[i] == note)
                    {
                        for (int j = i; j < numHeldNotes - 1; ++j) heldNotes[j] = heldNotes[j + 1];
                        --numHeldNotes;
                        break;
                    }
                if (numHeldNotes == 0)
                {
                    for (auto& v : midiVoices)
                        if (v.stage != VoiceStage::Off && v.stage != VoiceStage::Release)
                            v.stage = VoiceStage::Release;
                    arpCurrentNote = -1;
                }
                continue;
            }

            for (auto& v : midiVoices)
                if (v.midiNote == note && v.stage != VoiceStage::Off
                                       && v.stage != VoiceStage::Release)
                    v.stage = VoiceStage::Release;
        }
        else if (m.isAllNotesOff() || m.isAllSoundOff())
        {
            for (auto& v : midiVoices) v.stage = VoiceStage::Release;
            numHeldNotes   = 0;
            arpCurrentNote = -1;
        }
    }

    // ---- Arpeggiator tick --------------------------------------------------
    // Advance the per-block sample counter; when it reaches zero, release the
    // current arp voice and trigger the next note in the configured pattern.
    // Stepping happens on block boundaries — sub-block precision isn't worth
    // the complexity for a sampler arp; even 1/32 at 240 BPM = ~125ms per
    // step which dwarfs typical block sizes (1-10 ms).
    if (arpEnabled.load() && numHeldNotes > 0)
    {
        // Mod wheel selects the rate when ARP is on (0..1 → 1/4..1/32).
        const int    effectiveRate = modWheelToArpRate (midiModWheel.load());
        const double samplesPerBeat = hostSampleRate * 60.0 / juce::jmax (1.0, internalBpm.load());
        const double samplesPerStep = juce::jmax (32.0,
            samplesPerBeat * kArpRateFractions[effectiveRate]);

        arpSamplesUntilStep -= (double) numSamples;
        if (arpSamplesUntilStep <= 0.0)
        {
            // Pick the next note based on the configured pattern.
            int next = 0;
            switch (arpMode.load())
            {
                case ArpDown:
                    arpStepIndex = (arpStepIndex - 1 + numHeldNotes * 8) % numHeldNotes;
                    next = heldNotes[numHeldNotes - 1 - arpStepIndex];
                    ++arpStepIndex;
                    break;

                case ArpUpDown:
                    if (numHeldNotes == 1) { next = heldNotes[0]; break; }
                    next = heldNotes[juce::jlimit (0, numHeldNotes - 1, arpStepIndex)];
                    arpStepIndex += arpDirection;
                    if (arpStepIndex >= numHeldNotes - 1) { arpStepIndex = numHeldNotes - 1; arpDirection = -1; }
                    else if (arpStepIndex <= 0)          { arpStepIndex = 0;                 arpDirection =  1; }
                    break;

                case ArpRandom:
                    next = heldNotes[arpRng.nextInt (numHeldNotes)];
                    break;

                case ArpUp:
                default:
                    next = heldNotes[arpStepIndex % numHeldNotes];
                    ++arpStepIndex;
                    if (arpStepIndex >= numHeldNotes) arpStepIndex = 0;
                    break;
            }

            // Release the previous arp voice (if any) and allocate a fresh
            // one for the new note. Always steal slot 0 so the arp has a
            // dedicated voice that doesn't fight the rest of the polyphony.
            for (auto& v : midiVoices)
                if (v.stage != VoiceStage::Off && v.stage != VoiceStage::Release)
                    v.stage = VoiceStage::Release;

            auto& slot = midiVoices[0];
            slot.midiNote   = next;
            slot.velocity   = 0.9f;
            slot.pos        = 0.0;
            slot.rate       = std::pow (2.0, (next - kMidiRootNote) / 12.0);
            slot.env        = 0.0f;
            slot.stage      = VoiceStage::Attack;
            slot.ageCounter = ++midiVoiceAge;
            arpCurrentNote  = next;

            arpSamplesUntilStep += samplesPerStep;
            if (arpSamplesUntilStep < 0.0) arpSamplesUntilStep = samplesPerStep;
        }
    }
    else if (! arpEnabled.load() && numHeldNotes > 0)
    {
        // Arp was turned off mid-chord — drop the captured held notes so a
        // subsequent re-enable starts fresh.
        numHeldNotes   = 0;
        arpCurrentNote = -1;
    }

    // ---- INPUT preamp: wet/dry mix of [compress -> makeup -> tanh -> LP -> HP]
    // The knob is the wet/dry fader. At 0.0 the buffer is unchanged (dry recording).
    // At 1.0 the whole preamp chain replaces it. In between it's a parallel mix.
    // The peak meter is updated from the DRY input so the LED reflects actual
    // device input regardless of mix position.
    {
        const float mix = inputMix.load();

        if (numInChannels > 0)
        {
            const int chCount = juce::jmin (numInChannels, kMaxFilterChannels);

            // ---- 1) Measure peak of DRY input for the level meter ----------
            float peak = 0.0f;
            for (int ch = 0; ch < chCount; ++ch)
            {
                const auto* r = buffer.getReadPointer (ch);
                for (int i = 0; i < numSamples; ++i)
                    peak = juce::jmax (peak, std::abs (r[i]));
            }
            // Smooth the meter a bit (fast attack, slow release).
            const float prev = inputPeakLevel.load();
            const float meter = peak > prev ? peak : prev * 0.85f;
            inputPeakLevel.store (meter);

            // ---- 2) If the knob is fully dry, leave buffer alone -----------
            if (mix > 1e-4f)
            {
                // Stash dry into our pre-allocated dryBuffer.
                if (dryBuffer.getNumSamples() < numSamples)
                    dryBuffer.setSize (juce::jmax (2, chCount), numSamples, false, true, true);
                for (int ch = 0; ch < chCount; ++ch)
                    dryBuffer.copyFrom (ch, 0, buffer, ch, 0, numSamples);

                // HEAVY 12:1 compression, per-voice timing and saturation.
                const auto params = getCompParams (inputCompMode.load());

                const float threshold = 0.18f;             // ~-15 dBFS (lower → more reduction)
                const float ratio     = 24.0f;             // FULL squash — twice as nuclear
                const float attackC   = std::exp (-1.0f / (float) (hostSampleRate * params.attackMs  * 0.001));
                const float releaseC  = std::exp (-1.0f / (float) (hostSampleRate * params.releaseMs * 0.001));
                const float makeupLin = 3.16f;             // +10 dB makeup — heavy comp needs it

                // Per-voice tape rolloff (re-derived each block, cheap).
                const float lpCoef = (float) (1.0 - std::exp (-juce::MathConstants<double>::twoPi
                                                             * (double) params.lpHz / hostSampleRate));

                // Saturation params per voice.
                const float drive   = params.satDrive;
                const float bias    = 0.10f;
                const float biasOut = std::tanh (bias * drive);

                for (int ch = 0; ch < chCount; ++ch)
                {
                    auto*       w = buffer.getWritePointer (ch);
                    const auto* d = dryBuffer.getReadPointer (ch);

                    for (int i = 0; i < numSamples; ++i)
                    {
                        const float x = d[i];

                        // Compressor: envelope follower (peak, attack/release).
                        const float rect = std::abs (x);
                        const float coef = rect > compEnv[ch] ? attackC : releaseC;
                        compEnv[ch] = coef * compEnv[ch] + (1.0f - coef) * rect;

                        float gr = 1.0f;
                        if (compEnv[ch] > threshold)
                        {
                            const float over = compEnv[ch] / threshold;
                            gr = std::pow (over, -(1.0f - 1.0f / ratio));
                        }
                        float wet = x * gr * makeupLin;

                        // Asymmetric saturation.
                        wet = (std::tanh ((wet + bias) * drive) - biasOut) / drive;

                        // Tape HF rolloff (per-voice cutoff).
                        warmthLpState[ch] += lpCoef * (wet - warmthLpState[ch]);
                        wet = warmthLpState[ch];

                        // DC blocker.
                        const float y = wet - warmthHpPrev[ch] + warmthHpCoeff * warmthHpState[ch];
                        warmthHpPrev[ch]  = wet;
                        warmthHpState[ch] = y;

                        // Final wet/dry mix.
                        w[i] = x * (1.0f - mix) + y * mix;
                    }
                }
            }
            else
            {
                // Fully dry: keep filter / compressor state from drifting wild.
                for (int ch = 0; ch < chCount; ++ch)
                {
                    warmthLpState[ch] *= 0.9f;
                    warmthHpState[ch] *= 0.9f;
                    warmthHpPrev [ch] *= 0.9f;
                    compEnv      [ch] *= 0.9f;
                }
            }
        }
    }

    // ---- Capture input into the snapshot slice --------------------------
    // recordPos is advanced ONCE at the end of the block so the MIDI voice
    // renderer (further down) can write into the same slice. Auto-stop is
    // also deferred to that end-of-block step.
    if (recordingNow && recBlockLen > 0 && numInChannels > 0)
    {
        const int toCopy  = recBlockLen;
        const int chCount = juce::jmin (numInChannels, recordBuffer.getNumChannels());
        for (int ch = 0; ch < chCount; ++ch)
            recordBuffer.copyFrom (ch, recStartPos, buffer, ch, 0, toCopy);
        // If input is mono, duplicate to right channel for a usable stereo recording.
        if (chCount == 1 && recordBuffer.getNumChannels() >= 2)
            recordBuffer.copyFrom (1, recStartPos, buffer, 0, 0, toCopy);

        // OVERDUB: layer the currently-playing audio on top of the input.
        // Only does anything if a sample is loaded and playback is happening.
        if (overdubEnabled.load() && playing.load())
        {
            Sample::Ptr odSrc;
            {
                const juce::SpinLock::ScopedTryLockType tryLock (sampleLock);
                if (tryLock.isLocked())
                    odSrc = currentSample;
            }
            if (odSrc != nullptr
                && odSrc->buffer.getNumSamples()  > 0
                && odSrc->buffer.getNumChannels() > 0)
            {
                const double odPitch = (odSrc->sourceSampleRate > 0.0)
                                           ? (odSrc->sourceSampleRate / hostSampleRate)
                                           : 1.0;
                const double odRate = odPitch * (double) playbackSpeed.load();
                const int odMax    = odSrc->buffer.getNumSamples();
                const int odChCnt  = odSrc->buffer.getNumChannels();
                double odPos = playPosition.load();
                // Same NaN/Inf guard the main playback loop uses — overdub
                // runs BEFORE that block's guard, so we have to apply it here.
                if (! std::isfinite (odPos) || std::abs (odPos) > 1e9)
                    odPos = 0.0;

                for (int i = 0; i < toCopy; ++i)
                {
                    // Wrap both directions (reverse playback / scratching
                    // can push odPos negative).
                    if (odPos >= (double) odMax) odPos = std::fmod (odPos, (double) odMax);
                    if (odPos < 0.0)             odPos += (double) odMax * std::ceil (-odPos / (double) odMax);
                    if (odPos < 0.0 || odPos >= (double) odMax) odPos = 0.0;  // belt-and-braces
                    const int idx = juce::jlimit (0, odMax - 1, (int) odPos);
                    for (int ch = 0; ch < recordBuffer.getNumChannels(); ++ch)
                    {
                        const int srcCh = ch % odChCnt;
                        recordBuffer.addSample (ch, recStartPos + i,
                                                odSrc->buffer.getSample (srcCh, idx));
                    }
                    odPos += odRate;
                }
            }
        }
    }

    // ---- HOST PLAYHEAD: pick up the DAW's transport BPM if available ----
    // Used for tempo-synced effects (FILTER LFO, GHOST delay, loop SIZE
    // buttons). Falls back to the internal BPM (settable via the BPM
    // window / TAP button) when there's no host or no playhead info.
    if (auto* ph = getPlayHead())
    {
        if (auto info = ph->getPosition())
        {
            if (auto hostBpmOpt = info->getBpm())
            {
                const double hb = *hostBpmOpt;
                if (hb >= 40.0 && hb <= 240.0)
                    internalBpm.store (hb);
            }
        }
    }

    // Transport fade target (1.0 when audible, 0.0 when stopped). Scratching
    // counts as audible too — grabbing the reel and dragging should be heard
    // immediately even if the PLAY-button transport isn't running (real
    // turntable behaviour). Without this the gain envelope would fade
    // scratching to silence whenever PLAY was off.
    const float targetPlayGain = (playing.load() || scratchActive.load()) ? 1.0f : 0.0f;

    // Resolve LOOP-cutoff fade length (in OUTPUT samples).
    const int   cutoffMode = loopCutoffMode.load();
    const float fadeSamples = cutoffMode == 2 ? 0.0f
                            : cutoffMode == 1 ? 0.008f * (float) hostSampleRate    // -24 (8 ms)
                                              : 0.030f * (float) hostSampleRate;   // -12 (30 ms)
    const float fadeStep = fadeSamples > 0.0f ? 1.0f / fadeSamples : 1.0f;

    // Try-lock to grab the current sample. If the message thread is mid-swap
    // we just skip this block — sub-millisecond glitch at most, on a rare event.
    Sample::Ptr local;
    {
        const juce::SpinLock::ScopedTryLockType tryLock (sampleLock);
        if (tryLock.isLocked())
            local = currentSample;
    }

    // Insert-FX behaviour: the buffer arrives from the host containing live
    // audio (the channel SPOOL is inserted on). We REPLACE that audio with
    // the SPOOL sample when one is loaded AND playing; otherwise we let the
    // host audio pass through so the effect chain still does its job.
    const bool haveSample = local != nullptr
                            && local->buffer.getNumSamples()  > 0
                            && local->buffer.getNumChannels() > 0;
    // Sample-read block must run whenever ANY audio source is active. That
    // includes scratching even when PLAY transport is off — otherwise
    // grabbing the reel with PLAY off skips the whole sample-read block
    // and the user hears nothing.
    const bool samplePlaying = haveSample
                            && (playing.load()
                                || playGainSmoothed > 1e-4f
                                || scratchActive.load());

    // PASSTHROUGH gate. In a DAW (plugin mode) the input buffer carries
    // host audio that we want to feed through the effect chain — that's the
    // insert-FX behaviour. In STANDALONE the input is the mic and the
    // output is the speakers, so a pass-through would create an immediate
    // feedback loop. Only the plugin wrapper passes through.
    const bool isStandalone = (wrapperType == wrapperType_Standalone);

    if (samplePlaying)
    {
        // The sample REPLACES the host audio. Clear and write the sample
        // into the buffer below; effects after this block apply to it.
        buffer.clear();
    }
    else if (isStandalone)
    {
        // Silence the mic input so the speakers don't catch it.
        buffer.clear();
        playGainSmoothed = juce::jmax (0.0f, playGainSmoothed - fadeStep);
    }
    else
    {
        // Plugin mode: let host audio pass through. Effects still apply.
        playGainSmoothed = juce::jmax (0.0f, playGainSmoothed - fadeStep);
    }

    if (samplePlaying)
    {
    const auto&  src         = local->buffer;
    const int    srcSamples  = src.getNumSamples();
    const int    srcChannels = src.getNumChannels();
    double       pos        = playPosition.load();
    if (! std::isfinite (pos) || std::abs (pos) > 1e9)
        pos = 0.0;   // sanity reset — never let pos be NaN/inf/huge

    // ---- Rate ratio: how many SOURCE samples to advance per OUTPUT sample.
    // When the user is scratching the vinyl, the rate comes from the scratch
    // velocity (signed — negative = reverse). Otherwise SPEED knob drives it.
    const double pitchRatio = (local->sourceSampleRate > 0.0 && hostSampleRate > 0.0)
                                  ? (local->sourceSampleRate / hostSampleRate)
                                  : 1.0;
    const float  speedAmt   = playbackSpeed.load();
    const bool   scratching = scratchActive.load();

    // Normal playback rate. While scratching, the per-sample loop will
    // bypass this entirely and lerp pos toward desiredPos instead, so
    // targetRate here only matters for non-scratching playback.
    const double targetRate = pitchRatio * (double) speedAmt;

    // SCRUB-style position tracking: when scratching, the desired playhead
    // is "anchor + cumulative platter angle × samples-per-radian". The per-
    // sample loop drags pos toward desiredPos directly. That gives the
    // user a waveform-scrubber feel — mouse holds, audio holds; mouse
    // moves, audio plays from that point at the speed of the mouse.
    double desiredPos = 0.0;
    double samplesPerRadian = 1.0;
    if (scratching)
    {
        float currentAccum = scratchLastReadAngleRad;
        {
            const juce::SpinLock::ScopedTryLockType tryLock (scratchAccumLock);
            if (tryLock.isLocked()) currentAccum = scratchAccumAngleRad;
        }
        // First scratched block: snapshot the current playback position as
        // the anchor so the scrub starts from where the audio actually was,
        // not jumping to wherever stale state left things. Also snap the
        // gain envelope to full so the scratch is audible IMMEDIATELY
        // instead of fading in over the LOOP-cutoff fade window (could
        // mute the first ~30 ms of a short scratch otherwise).
        if (! prevScratchActive)
        {
            scratchAnchorPos        = pos;
            scratchLastReadAngleRad = currentAccum;
            playGainSmoothed        = 1.0f;
        }
        // Convert: one full rotation = 1/baseRotPerSec seconds of audio at
        // 1× playback, so radians × (sourceSr / (baseRotPerSec × 2π)) = samples.
        samplesPerRadian = (double) (local->sourceSampleRate > 0.0
                                     ? local->sourceSampleRate
                                     : hostSampleRate)
                             / ((double) kScratchBaseRotPerSec
                                 * juce::MathConstants<double>::twoPi);
        desiredPos = scratchAnchorPos + (double) currentAccum * samplesPerRadian;
    }

    // Per-sample rate smoothing — kills the zipper noise/clicks that come
    // from snapping the rate to each new mouse-event sample. Used for
    // normal (non-scratch) playback. ~3 ms cartridge-inertia feel.
    const double rateSmoothCoef = 1.0 - std::exp (-1.0 / (0.003 * hostSampleRate));
    // While scratching, the per-sample loop instead pulls pos toward
    // desiredPos with a one-pole filter. The time constant has to be
    // LONGER than the typical mouse-event interval (~16 ms at 60 Hz
    // polling) — otherwise pos catches up in 3 ms then sits silent for
    // 13 ms, producing "burst-silence-burst" instead of continuous
    // scrubbed audio. 25 ms TC keeps pos always chasing the moving
    // target so the audio stays smooth, while still letting pos rest
    // quickly enough after the user releases.
    const double posLerpCoef    = 1.0 - std::exp (-1.0 / (0.025 * hostSampleRate));
    // Snap immediately when scratch ends (returning to SPEED knob), otherwise
    // glide for natural feel.
    if (! scratching && std::abs (scratchRateSmoothed - targetRate) > 0.5)
        scratchRateSmoothed = targetRate;

    // ---- SPEED degradation: bit-crush + sample-rate reduction. Asymmetric:
    // slowing the tape down is gnarlier than speeding up (TP-7 / cassette
    // tape character — slow speeds reveal more crunch and aliasing).
    // *** Disabled while scratching *** — bit-crush stacks on top of pitch
    // shift and turns clean scratching into digital noise.
    const float  degrade   = speedAmt < 1.0f ? (1.0f - speedAmt) * 1.6f      // 0 → 0.8 at 0.5×
                                             : (speedAmt - 1.0f) * 0.35f;    // 0 → 0.35 at 2×
    const float  degAmount = scratching ? 0.0f : juce::jlimit (0.0f, 1.0f, degrade);
    const bool   doDegrade = degAmount > 0.01f;
    const float  bitDepth  = 16.0f - degAmount * 12.0f;        // 16 → 4 bits
    const float  bitLevels = std::pow (2.0f, bitDepth) * 0.5f; // quantisation step
    const int    holdLen   = (int) (degAmount * 7.0f);         // hold sample for N extra frames

    // Cartridge LP — dynamic one-pole, only active while scratching. Cutoff
    // tracks rate magnitude so slow drags sound dark / dragged (~3 kHz) and
    // fast scratches stay bright (~10 kHz). Reverse playback gets a touch
    // more rolloff because the cartridge isn't optimised for it.
    float scratchLpCoef = 1.0f;
    if (scratching)
    {
        const float absRate = (float) std::abs (scratchRateSmoothed);
        float       lpHz    = juce::jlimit (2500.0f, 10000.0f, 2500.0f + absRate * 5500.0f);
        if (scratchRateSmoothed < 0.0) lpHz *= 0.78f;
        scratchLpCoef = (float) (1.0 - std::exp (-juce::MathConstants<double>::twoPi
                                                 * (double) lpHz / juce::jmax (1.0, hostSampleRate)));
    }

    // Direction-change detection: when the smoothed rate flips sign, kick
    // a brief noise envelope — the "tk tk" of the needle scrubbing across
    // the groove. Also feed an onset thump when scratching first starts.
    if (scratching)
    {
        const float sign = scratchRateSmoothed >  0.001 ? 1.0f
                         : scratchRateSmoothed < -0.001 ? -1.0f : 0.0f;
        if (sign != 0.0f && prevScratchSign != 0.0f && sign != prevScratchSign)
            scratchZipEnv = 1.0f;
        if (sign != 0.0f) prevScratchSign = sign;
        if (! prevScratchActive) scratchOnsetEnv = 1.0f;     // needle-drop pulse
    }
    else
    {
        prevScratchSign = 0.0f;
    }
    prevScratchActive = scratching;

    // ---- LOOP setup: window of length N, anchored at engagement point.
    // Loop length is computed in SOURCE samples so the LOOP duration is wall-clock-
    // correct (1 bar = 2 sec @ 120 BPM, regardless of source rate).
    const int    loopLen          = loopLengthMode.load();
    const double srcSampleRate    = local->sourceSampleRate > 0.0 ? local->sourceSampleRate : hostSampleRate;
    const double samplesPerBeat   = srcSampleRate * 60.0 / internalBpm.load();
    const double loopWindowSamps  = loopLen > 0 ? samplesPerBeat * kLoopFractionOfBeat[loopLen] : 0.0;

    // Apply user-requested anchor first (from waveform double-click). Highest
    // priority — overrides the natural 0→non-zero transition behaviour.
    const double requestedAnchor = pendingLoopAnchor.exchange (-2.0);
    if (requestedAnchor >= 0.0)
    {
        loopAnchorBase = requestedAnchor;
        loopAnchorOffsetSeconds.store (0.0f);
        loopShouldResetNudge.store (true);
    }

    // Detect 0 -> non-zero transition: capture loop anchor at current playhead.
    // Skip if the user already explicitly set one this block.
    if (loopLen > 0 && prevLoopLengthMode == 0 && requestedAnchor < 0.0)
    {
        loopAnchorBase = pos;
        loopAnchorOffsetSeconds.store (0.0f);
        loopShouldResetNudge.store (true);
    }
    else if (loopLen == 0 && requestedAnchor < 0.0)
    {
        loopAnchorBase = -1.0;       // released — back to normal playback
        customLoopStart.store (-1.0);
        customLoopEnd  .store (-1.0);
    }
    prevLoopLengthMode = loopLen;

    // Effective anchor = captured base + NUDGE offset (in source samples).
    // Custom drag region (if set) takes priority — NUDGE shifts it too.
    double effectiveAnchor = -1.0;
    double loopEnd         = 0.0;
    if (hasCustomLoop())
    {
        const double offsetSamps = (double) loopAnchorOffsetSeconds.load() * srcSampleRate;
        const double s0     = customLoopStart.load();
        const double e0     = customLoopEnd.load();
        const double winLen = e0 - s0;
        const double maxStart = juce::jmax (0.0, (double) srcSamples - winLen);
        effectiveAnchor = juce::jlimit (0.0, maxStart, s0 + offsetSamps);
        loopEnd         = juce::jmin ((double) srcSamples, effectiveAnchor + winLen);
    }
    else if (loopLen > 0 && loopAnchorBase >= 0.0)
    {
        const double offsetSamps = (double) loopAnchorOffsetSeconds.load() * srcSampleRate;
        const double maxAnchor   = juce::jmax (0.0, (double) srcSamples - loopWindowSamps);
        effectiveAnchor = juce::jlimit (0.0, maxAnchor, loopAnchorBase + offsetSamps);
        loopEnd         = juce::jmin (effectiveAnchor + loopWindowSamps, (double) srcSamples);
    }

    // If the loop is engaged but the playhead is outside the window, snap into it.
    if (effectiveAnchor >= 0.0)
    {
        if (pos < effectiveAnchor || pos >= loopEnd)
        {
            pos = effectiveAnchor;
            loopWrapped.store (true);
        }
    }

    for (int i = 0; i < numSamples; ++i)
    {
        // Reverse-playback wrap (scratching backwards past 0). While the
        // user is grabbing the reel we ALWAYS wrap the position, regardless
        // of the LOOP-button setting — a real turntable doesn't care, and
        // without the wrap the playhead would get stuck at sample 0 and
        // glitch (re-clamping to 0 every iteration). When NOT scratching,
        // the LOOP button decides whether reverse playback wraps or stops.
        // When wrapping during scratch we also shift scratchAnchorPos by
        // the same amount so desiredPos stays aligned with the new pos —
        // otherwise the lerp would immediately drag pos back through the
        // wrap point and spin forever.
        if (pos < 0.0)
        {
            const double oldPos = pos;
            if (effectiveAnchor >= 0.0)
                pos = loopEnd - 1.0;                            // bounce to loop end
            else if (looping.load() || scratching)
            {
                pos = (double) srcSamples + std::fmod (pos, (double) srcSamples);
                if (pos >= (double) srcSamples) pos -= (double) srcSamples;
                if (pos < 0.0)                  pos = (double) srcSamples - 1.0;
            }
            else
                pos = 0.0;
            if (scratching)
            {
                const double shift = pos - oldPos;
                scratchAnchorPos += shift;
                desiredPos       += shift;
            }
        }

        // Loop window wrap (takes precedence over full-sample wrap).
        if (effectiveAnchor >= 0.0)
        {
            if (pos >= loopEnd)
            {
                const double oldPos = pos;
                pos = effectiveAnchor;
                loopWrapped.store (true);
                if (scratching)
                {
                    const double shift = pos - oldPos;
                    scratchAnchorPos += shift;
                    desiredPos       += shift;
                }
            }
        }
        else if (pos >= (double) srcSamples)
        {
            if (looping.load() || scratching)
            {
                const double oldPos = pos;
                pos = std::fmod (pos, (double) srcSamples);
                if (scratching)
                {
                    const double shift = pos - oldPos;
                    scratchAnchorPos += shift;
                    desiredPos       += shift;
                }
            }
            else
            {
                playing.store (false);
                break;
            }
        }

        // ---- Slew transport gain toward target (handles stop/pause fade).
        if (fadeSamples <= 0.0f)
            playGainSmoothed = targetPlayGain;   // brick = instant
        else if (playGainSmoothed < targetPlayGain)
            playGainSmoothed = juce::jmin (targetPlayGain, playGainSmoothed + fadeStep);
        else if (playGainSmoothed > targetPlayGain)
            playGainSmoothed = juce::jmax (targetPlayGain, playGainSmoothed - fadeStep);

        // ---- Loop boundary fade (only when loop is engaged and not brick).
        float loopGain = 1.0f;
        if (effectiveAnchor >= 0.0 && fadeSamples > 0.0f)
        {
            const double distFromAnchor = pos - effectiveAnchor;
            const double distToEnd      = loopEnd - pos;
            if (distFromAnchor < (double) fadeSamples)
                loopGain *= (float) (distFromAnchor / (double) fadeSamples);
            if (distToEnd < (double) fadeSamples)
                loopGain *= (float) (distToEnd / (double) fadeSamples);
            loopGain = juce::jlimit (0.0f, 1.0f, loopGain);
        }

        const float totalGain = playGainSmoothed * loopGain * sampleGain.load();

        // ---- Sample read at fractional source position --------------------
        // Normal playback: linear interp (fast, clean when rate ≈ 1×).
        // While scratching: cubic Hermite (Catmull-Rom) on 4 source samples
        // — gives smoother HF content under heavy rate changes, killing the
        // grainy "stepped" sound linear interp produces below 1× and during
        // direction changes. The extra ~3 multiplies/adds per channel are
        // cheap and the audible improvement is significant.
        const int    i0   = (int) pos;
        const int    i1   = juce::jmin (i0 + 1, srcSamples - 1);
        const float  frac = (float) (pos - (double) i0);
        const int    im1  = juce::jmax (0, i0 - 1);
        const int    i2   = juce::jmin (srcSamples - 1, i1 + 1);

        // Stylus friction noise + direction-change zip — mono so all channels
        // share the same noise burst (reads as a real needle, not stereo hiss).
        float scratchNoise = 0.0f;
        if (scratching)
        {
            const float absRate = (float) std::abs (scratchRateSmoothed);
            const float frictionLevel = juce::jmin (1.0f, absRate * 0.55f) * 0.018f;
            const float zipLevel      = scratchZipEnv * 0.10f;
            const float n             = (scratchNoiseRng.nextFloat() - 0.5f) * 2.0f;
            scratchNoise = n * (frictionLevel + zipLevel);
            // Decay envelopes — zip ~30ms, onset ~80ms.
            scratchZipEnv   *= 0.94f;
            scratchOnsetEnv *= 0.985f;
        }

        // Sample-rate reduction: if we're still holding a previous sample, just
        // re-emit it. Otherwise read fresh, optionally bit-crush, then refresh
        // the hold buffer and reset the counter.
        const bool useHold = doDegrade && srrCounter > 0;
        if (useHold)
        {
            const int chMax = juce::jmin (numOutChannels, kMaxFilterChannels);
            for (int ch = 0; ch < chMax; ++ch)
                buffer.setSample (ch, i, srrHold[ch] * totalGain);
            --srrCounter;
        }
        else
        {
            const int chMax = juce::jmin (numOutChannels, kMaxFilterChannels);
            for (int ch = 0; ch < chMax; ++ch)
            {
                const int srcCh = ch % juce::jmax (1, srcChannels);
                float interp;
                if (scratching)
                {
                    // 4-point Hermite (Catmull-Rom) — smooth HF under rate changes.
                    const float sm1 = src.getSample (srcCh, im1);
                    const float s0  = src.getSample (srcCh, i0);
                    const float s1  = src.getSample (srcCh, i1);
                    const float s2  = src.getSample (srcCh, i2);
                    const float a = 0.5f * (-sm1 + 3.0f * s0 - 3.0f * s1 + s2);
                    const float b = sm1 - 2.5f * s0 + 2.0f * s1 - 0.5f * s2;
                    const float c = 0.5f * (-sm1 + s1);
                    interp = ((a * frac + b) * frac + c) * frac + s0;
                }
                else
                {
                    const float s0 = src.getSample (srcCh, i0);
                    const float s1 = src.getSample (srcCh, i1);
                    interp = s0 + (s1 - s0) * frac;
                }

                if (doDegrade)
                    interp = std::round (interp * bitLevels) / bitLevels; // bit-crush

                // Cartridge LP during scratch — softens aliasing into vinyl
                // warmth and tracks rate magnitude (slow drag = darker).
                if (scratching)
                {
                    interp += scratchNoise;                      // friction + zip
                    scratchLpState[ch] += scratchLpCoef * (interp - scratchLpState[ch]);
                    interp = scratchLpState[ch];
                    // Needle-drop bass thump: subtle low-end boost via DC
                    // bias decaying out over the onset envelope. Sounds like
                    // weight settling on the platter for the first ~80 ms.
                    interp += scratchOnsetEnv * 0.05f;
                }

                srrHold[ch] = interp;
                buffer.setSample (ch, i, interp * totalGain);
            }
            if (doDegrade)
                srrCounter = holdLen;
        }

        if (scratching)
        {
            // SCRUB lerp: pull pos toward desiredPos with a tight one-pole
            // filter. Clamp the per-sample movement so a giant block-
            // boundary jump (huge mouse flick) can't push pos arbitrarily
            // far in one sample. Continuous motion → continuous audio;
            // mouse stops → pos arrives at desired and holds (silence).
            const double rawStep = (desiredPos - pos) * posLerpCoef;
            const double step    = juce::jlimit (-32.0, 32.0, rawStep);
            pos += step;
            // Keep scratchRateSmoothed in sync so the on-release snap-back
            // has a sane starting value (uses recent per-sample motion).
            scratchRateSmoothed = step;
        }
        else
        {
            // Normal playback: smooth toward the target rate so mouse jitter
            // / event quantisation doesn't translate into audible chirps.
            scratchRateSmoothed += (targetRate - scratchRateSmoothed) * rateSmoothCoef;
            pos += scratchRateSmoothed;
        }
    }

    playPosition.store (pos);
    }   // end if (samplePlaying) — sample-read block

    // ---- MIDI sampler voices: sum each active voice on top of whatever
    // the transport produced. Reads the same loaded sample at a per-voice
    // pitch-shifted rate, then feeds the full effect chain below. Coexists
    // with PLAY-button transport — perform live MIDI over a running loop.
    bool anyVoiceActive = false;
    for (const auto& v : midiVoices) if (v.stage != VoiceStage::Off) { anyVoiceActive = true; break; }

    if (anyVoiceActive && haveSample && numOutChannels > 0)
    {
        const auto& src         = local->buffer;
        const int   srcSamples  = src.getNumSamples();
        const int   srcChannels = src.getNumChannels();
        const double srcSr      = local->sourceSampleRate > 0.0 ? local->sourceSampleRate : hostSampleRate;
        const double pitchRatio = (hostSampleRate > 0.0) ? (srcSr / hostSampleRate) : 1.0;
        const float  envAttackInc  = 1.0f / juce::jmax (1.0f, 0.005f * (float) hostSampleRate);   // ~5ms
        const float  envReleaseDec = 1.0f / juce::jmax (1.0f, 0.080f * (float) hostSampleRate);   // ~80ms

        // Pitch bend: convert ±2 semitones to a multiplicative rate factor
        // applied to every active voice. 0 semis = 1.0×, no audible cost.
        const double bendFactor = std::pow (2.0, midiPitchBendSemis.load() / 12.0);

        // Mod wheel → ~5 Hz tremolo on the voice amplitude. When ARP is on
        // the mod wheel is repurposed as the arp rate selector, so disable
        // tremolo in that mode to keep the two roles cleanly separated.
        const float  modAmount    = arpEnabled.load() ? 0.0f : midiModWheel.load();
        const double tremRateHz   = 5.0;
        const double tremPhaseInc = juce::MathConstants<double>::twoPi
                                      * tremRateHz / juce::jmax (1.0, hostSampleRate);

        // Loop region for MIDI voices — respect drag-highlight or knob loop
        // if set, otherwise one-shot through the whole sample.
        double loopStart = 0.0, loopEnd = (double) srcSamples;
        if (hasCustomLoop())
        {
            loopStart = customLoopStart.load();
            loopEnd   = customLoopEnd.load();
        }
        else if (loopLengthMode.load() > 0 && loopAnchorBase >= 0.0)
        {
            const double samplesPerBeat = srcSr * 60.0 / internalBpm.load();
            const double windowSamps    = samplesPerBeat * kLoopFractionOfBeat[loopLengthMode.load()];
            loopStart = juce::jlimit (0.0, (double) srcSamples, loopAnchorBase);
            loopEnd   = juce::jlimit (loopStart + 1.0, (double) srcSamples, loopStart + windowSamps);
        }
        const bool   midiLooping = (loopEnd - loopStart) < (double) srcSamples;
        const double loopLen     = loopEnd - loopStart;

        const int chCount = juce::jmin (numOutChannels, kMaxFilterChannels);

        for (auto& v : midiVoices)
        {
            if (v.stage == VoiceStage::Off) continue;
            // Start voices at loop region start (or sample start if no loop).
            if (v.pos < loopStart) v.pos = loopStart;

            for (int i = 0; i < numSamples; ++i)
            {
                // Envelope update
                if      (v.stage == VoiceStage::Attack)
                {
                    v.env = juce::jmin (1.0f, v.env + envAttackInc);
                    if (v.env >= 1.0f) v.stage = VoiceStage::Sustain;
                }
                else if (v.stage == VoiceStage::Release)
                {
                    v.env -= envReleaseDec;
                    if (v.env <= 0.0f) { v.env = 0.0f; v.stage = VoiceStage::Off; break; }
                }

                // Wrap / end-of-sample handling
                if (v.pos >= loopEnd)
                {
                    if (midiLooping)
                        v.pos = loopStart + std::fmod (v.pos - loopStart, loopLen);
                    else
                    {
                        // One-shot ended — let envelope release out.
                        v.stage = VoiceStage::Off;
                        break;
                    }
                }

                const int    i0 = (int) v.pos;
                const int    i1 = juce::jmin (i0 + 1, srcSamples - 1);
                const float  frac = (float) (v.pos - (double) i0);
                // Mod wheel tremolo: 1.0 = silent at trough, 1.0 = full at peak.
                // Phase advances once per sample regardless of voice count;
                // depth scales with the wheel position (0..1).
                float tremGain = 1.0f;
                if (modAmount > 1e-4f)
                {
                    midiTremoloPhase += tremPhaseInc;
                    if (midiTremoloPhase > juce::MathConstants<double>::twoPi)
                        midiTremoloPhase -= juce::MathConstants<double>::twoPi;
                    const float t = 0.5f * (1.0f + (float) std::sin (midiTremoloPhase));
                    tremGain = juce::jlimit (0.0f, 1.0f, 1.0f - modAmount * (1.0f - t));
                }
                const float  gain = v.env * v.velocity * sampleGain.load() * tremGain;
                const bool   recordThisSample = recordingNow && i < recBlockLen;
                const int    recChCount       = recordThisSample ? recordBuffer.getNumChannels() : 0;

                for (int ch = 0; ch < chCount; ++ch)
                {
                    const int srcCh = ch % srcChannels;
                    const float s0 = src.getSample (srcCh, i0);
                    const float s1 = src.getSample (srcCh, i1);
                    const float voiceSample = (s0 + (s1 - s0) * frac) * gain;
                    buffer.addSample (ch, i, voiceSample);
                    // Mirror into the recording slice so REC captures MIDI
                    // performance. Mono recordings get the voice summed into
                    // both rec channels for a usable stereo file on stop.
                    if (recordThisSample && ch < recChCount)
                        recordBuffer.addSample (ch, recStartPos + i, voiceSample);
                }
                // If the plugin is rendering mono out but the record buffer
                // is stereo, mirror channel 0 into channel 1 so the saved
                // sample isn't half-silent.
                if (recordThisSample && chCount == 1 && recChCount >= 2)
                {
                    const int srcCh = 0;
                    const float s0 = src.getSample (srcCh % srcChannels, i0);
                    const float s1 = src.getSample (srcCh % srcChannels, i1);
                    recordBuffer.addSample (1, recStartPos + i, (s0 + (s1 - s0) * frac) * gain);
                }
                v.pos += v.rate * pitchRatio * bendFactor;
            }
        }

        // Publish the position of the most-recently-triggered active voice
        // so the editor's waveform playhead can follow MIDI playback when
        // the PLAY-button transport isn't running. Picks the voice with the
        // highest ageCounter (= most recently triggered / arp-fired) and
        // grabs its current source-sample read position.
        const SamplerVoice* latest = nullptr;
        uint64_t newestAge = 0;
        for (const auto& v : midiVoices)
        {
            if (v.stage != VoiceStage::Off && v.ageCounter >= newestAge)
            {
                latest    = &v;
                newestAge = v.ageCounter;
            }
        }
        midiPlayheadPos.store (latest != nullptr ? latest->pos : -1.0);
    }
    else
    {
        // No active voices — clear the MIDI playhead so the waveform falls
        // back to its idle state (or transport position, if PLAY is on).
        midiPlayheadPos.store (-1.0);
    }

    // ---- OUTPUT comp chain: apply the SAME preamp to playback so the INPUT
    // knob actually shapes what you HEAR (not just what gets recorded).
    // Uses independent filter/envelope state (out*) so it doesn't conflict.
    {
        const float mix = inputMix.load();
        if (mix > 1e-4f && numOutChannels > 0)
        {
            const auto params = getCompParams (inputCompMode.load());
            const float threshold = 0.18f;
            const float ratio     = 24.0f;
            const float attackC   = std::exp (-1.0f / (float) (hostSampleRate * params.attackMs  * 0.001));
            const float releaseC  = std::exp (-1.0f / (float) (hostSampleRate * params.releaseMs * 0.001));
            const float makeupLin = 3.16f;
            const float lpCoef = (float) (1.0 - std::exp (-juce::MathConstants<double>::twoPi
                                                         * (double) params.lpHz / hostSampleRate));
            const float drive   = params.satDrive;
            const float bias    = 0.10f;
            const float biasOut = std::tanh (bias * drive);

            const int chCount = juce::jmin (numOutChannels, kMaxFilterChannels);
            for (int ch = 0; ch < chCount; ++ch)
            {
                auto* w = buffer.getWritePointer (ch);
                for (int i = 0; i < numSamples; ++i)
                {
                    const float dry = w[i];

                    // Compressor.
                    const float rect = std::abs (dry);
                    const float coef = rect > outCompEnv[ch] ? attackC : releaseC;
                    outCompEnv[ch] = coef * outCompEnv[ch] + (1.0f - coef) * rect;
                    float gr = 1.0f;
                    if (outCompEnv[ch] > threshold)
                    {
                        const float over = outCompEnv[ch] / threshold;
                        gr = std::pow (over, -(1.0f - 1.0f / ratio));
                    }
                    float wet = dry * gr * makeupLin;

                    // Asymmetric saturation.
                    wet = (std::tanh ((wet + bias) * drive) - biasOut) / drive;

                    // Tape HF rolloff.
                    outLpState[ch] += lpCoef * (wet - outLpState[ch]);
                    wet = outLpState[ch];

                    // DC blocker.
                    const float y = wet - outHpPrev[ch] + warmthHpCoeff * outHpState[ch];
                    outHpPrev[ch]  = wet;
                    outHpState[ch] = y;

                    w[i] = dry * (1.0f - mix) + y * mix;
                }
            }
        }
    }

    // ---- JUICE chain: apply effects in the user-specified order ----------
    // Each lambda processes 'buffer' in place. The chain runs in the order
    // configured by signalPathOrder (drag-to-reorder in the UI).
    auto applyFilter = [&] ()
    {
        const float basePos  = filterPos.load();
        const int   lfoRate  = filterLfoRate.load();
        const bool  lfoOn    = lfoRate > 0;
        // Skip entirely when filter is at bypass AND LFO is off — saves CPU.
        if (! lfoOn && std::abs (basePos - 0.5f) < 0.02f) return;
        if (numOutChannels == 0) return;

        // LFO period in beats: 0=OFF, then 1/2..1/128 of a beat (powers of 2).
        static const float kBeatFrac[] = { 0.0f, 2.0f, 1.0f, 0.5f, 0.25f, 0.125f, 0.0625f, 0.03125f };
        const float lfoPeriodBeats = kBeatFrac[juce::jlimit (0, (int) kNumFilterLfoRates - 1, lfoRate)];
        const double samplesPerBeat  = hostSampleRate * 60.0 / internalBpm.load();
        const double samplesPerCycle = juce::jmax (1.0, lfoPeriodBeats * samplesPerBeat);
        const double phaseInc        = lfoOn
            ? (juce::MathConstants<double>::twoPi / samplesPerCycle) : 0.0;
        const float lfoDepth = filterLfoRange.load() == 0 ? 0.10f
                             : filterLfoRange.load() == 1 ? 0.25f : 0.50f;

        const float Q = (filterQMode.load() == 0) ? 0.707f
                      : (filterQMode.load() == 1) ? 2.0f
                                                  : 6.0f;
        const float k = 1.0f / Q;

        const int chCount = juce::jmin (numOutChannels, kMaxFilterChannels);

        // Per-sample: recompute pos (with LFO offset) → cutoff → coefficients.
        // Channel loop is INSIDE so all channels share the same coefficients
        // for this sample. Slight extra CPU vs. block-rate but smooth modulation.
        for (int i = 0; i < numSamples; ++i)
        {
            float pos = basePos;
            if (lfoOn)
            {
                pos = juce::jlimit (0.0f, 1.0f,
                                    basePos + (float) std::sin (filterLfoPhase) * lfoDepth * 0.5f);
                filterLfoPhase += phaseInc;
                if (filterLfoPhase > juce::MathConstants<double>::twoPi)
                    filterLfoPhase -= juce::MathConstants<double>::twoPi;
            }

            const bool  isHp = pos > 0.5f;
            // Clamp cutoff to slightly under Nyquist so tan() never overshoots
            // π/2 (which would yield Inf / NaN and contaminate the SVF state).
            // Important at low sample rates (22.05k / 32k) where the nominal
            // ceiling of 20 kHz is above Nyquist.
            const float nyquistGuard = (float) hostSampleRate * 0.49f;
            const float rawCutoff = isHp
                ? juce::jlimit (20.0f,  4000.0f,  20.0f  + ((pos - 0.5f) * 2.0f) * (4000.0f - 20.0f))
                : juce::jlimit (200.0f, 20000.0f, 200.0f + (pos * 2.0f)          * (20000.0f - 200.0f));
            const float cutoff = juce::jmin (rawCutoff, nyquistGuard);
            const float g  = std::tan (juce::MathConstants<float>::pi * cutoff / (float) hostSampleRate);
            const float a1 = 1.0f / (1.0f + g * (g + k));
            const float a2 = g * a1;

            for (int ch = 0; ch < chCount; ++ch)
            {
                auto* w = buffer.getWritePointer (ch);
                const float v0 = w[i];
                const float v3 = v0 - svfLow[ch];
                const float v1 = a1 * svfBand[ch] + a2 * v3;
                const float v2 = svfLow[ch] + g * v1;
                svfBand[ch] = 2.0f * v1 - svfBand[ch];
                svfLow [ch] = 2.0f * v2 - svfLow [ch];
                w[i] = isHp ? (v0 - v2 - k * v1) : v2;
            }
        }
    };

    auto applyGhost = [&] ()
    {
        const float gh = ghostAmount.load();
        const int   bufLen = ghostDelayBuf.getNumSamples();
        if (! (gh > 1e-4f && numOutChannels > 0 && bufLen > 8)) return;

        const int   tm        = ghostTimeMode.load();
        const float beatFrac  = (tm == 0) ? 0.25f : (tm == 1) ? 0.5f : 1.0f;
        const int   delaySamps = juce::jlimit (4, bufLen - 1,
            (int) ((float) hostSampleRate * (60.0f / (float) internalBpm.load()) * beatFrac));
        const float feedback  = juce::jlimit (0.0f, 0.92f, gh * 0.92f);
        const float lpCutoff  = 8000.0f - gh * 7500.0f;
        const float lpCoef    = (float) (1.0 - std::exp (-juce::MathConstants<double>::twoPi
                                                        * (double) lpCutoff / hostSampleRate));
        const int   chCount   = juce::jmin (numOutChannels,
                                            ghostDelayBuf.getNumChannels(),
                                            kMaxFilterChannels);

        for (int i = 0; i < numSamples; ++i)
        {
            const int writeP = (ghostWritePos + i) % bufLen;
            const int readP  = (writeP - delaySamps + bufLen) % bufLen;
            for (int ch = 0; ch < chCount; ++ch)
            {
                auto* w  = buffer       .getWritePointer (ch);
                auto* d  = ghostDelayBuf.getWritePointer (ch);
                const float in      = w[i];
                const float delayed = d[readP];
                ghostFbLp[ch] += lpCoef * (delayed - ghostFbLp[ch]);
                d[writeP] = in + ghostFbLp[ch] * feedback;
                w[i] = in * (1.0f - gh) + delayed * gh;
            }
        }
        ghostWritePos = (ghostWritePos + numSamples) % bufLen;
    };

    auto applyHaze = [&] ()
    {
        const float haze   = hazeAmount.load();
        const bool  frozen = hazeFrozen.load();
        if (! ((haze > 1e-4f || frozen) && numOutChannels >= 1)) return;

        const int   preset    = hazePreset.load();
        const int   chCount   = juce::jmin (2, numOutChannels);

        // ---- Per-preset reverb parameters (Valhalla/Lexicon-style flavours).
        juce::Reverb::Parameters p;
        p.wetLevel   = juce::jmax (haze, 0.5f * (frozen ? 1.0f : 0.0f));
        p.dryLevel   = 1.0f;
        p.freezeMode = frozen ? 1.0f : 0.0f;
        switch (preset)
        {
            case PresetHall:    p.roomSize = 0.88f; p.damping = 0.28f; p.width = 1.0f; break;
            case PresetPlate:   p.roomSize = 0.62f; p.damping = 0.05f; p.width = 1.0f; break;
            case PresetRoom:    p.roomSize = 0.28f; p.damping = 0.55f; p.width = 0.7f; break;
            case PresetChamber: p.roomSize = 0.55f; p.damping = 0.40f; p.width = 0.9f; break;
            case PresetCave:    p.roomSize = 0.96f; p.damping = 0.75f; p.width = 1.0f; break;
            case PresetShimmer: p.roomSize = 0.92f; p.damping = 0.18f; p.width = 1.0f; break;
            default:            p.roomSize = 0.55f; p.damping = 0.40f; p.width = 0.9f; break;
        }
        hazeReverb.setParameters (p);

        // ---- PRE: PLATE gets a high-pass into the reverb (DC/sub trimmed
        // for that bright metallic plate sound). SHIMMER also benefits.
        if (preset == PresetPlate || preset == PresetShimmer)
        {
            const float hpCutoff = preset == PresetPlate ? 220.0f : 280.0f;
            const float alpha    = (float) std::exp (-juce::MathConstants<double>::twoPi
                                                     * (double) hpCutoff / hostSampleRate);
            for (int ch = 0; ch < chCount; ++ch)
            {
                auto* w = buffer.getWritePointer (ch);
                for (int i = 0; i < numSamples; ++i)
                {
                    const float in = w[i];
                    const float hp = alpha * (hazePreHpState[ch] + in - hazePreHpPrev[ch]);
                    hazePreHpPrev [ch] = in;
                    hazePreHpState[ch] = hp;
                    w[i] = hp;
                }
            }
        }

        // ---- AURA (shimmer): write the dry signal into a delay line and
        // read it back at 2× rate (one-octave-up pitch shift, simple delay-
        // line trick). Crossfade between two read pointers offset half a
        // buffer apart so the lap-around doesn't click. Cascade back into
        // the reverb input through a high-pass + soft clip.
        if (preset == PresetShimmer && shimmerBuf.getNumSamples() > 0)
        {
            const int   bufLen = shimmerBuf.getNumSamples();
            const float fbAmt  = 0.42f;             // octave-up feedback gain
            const float shHpA  = (float) std::exp (-juce::MathConstants<double>::twoPi
                                                   * 220.0 / hostSampleRate);
            const double halfBuf = (double) bufLen * 0.5;

            // IMPORTANT: process all channels per sample so the shared
            // write/read positions stay aligned. Doing the channel loop
            // OUTSIDE drives ch=1's writes into the wrong delay slots.
            for (int i = 0; i < numSamples; ++i)
            {
                // Two read pointers half a buffer apart; crossfade between
                // them by distance to write head (closer = quieter, far = full).
                double rA = shimmerReadPos;
                double rB = shimmerReadPos + halfBuf;
                if (rA >= (double) bufLen) rA -= (double) bufLen;
                if (rB >= (double) bufLen) rB -= (double) bufLen;

                auto distToWrite = [&] (double rp) {
                    double d = (double) shimmerWritePos - rp;
                    if (d < 0.0) d += (double) bufLen;
                    return d;
                };
                const double dA  = distToWrite (rA);
                const double dB  = distToWrite (rB);
                // Window: fade out as the read pointer approaches the writer.
                auto win = [&] (double dist) {
                    const double half = halfBuf;
                    const double k    = juce::jlimit (0.0, 1.0, dist / half);
                    return (float) std::sin (k * juce::MathConstants<double>::pi);
                };
                const float gA = win (dA);
                const float gB = win (dB);
                const float gSum = juce::jmax (1.0e-4f, gA + gB);

                const int rA0 = (int) rA;
                const int rB0 = (int) rB;
                const int rA1 = (rA0 + 1) % bufLen;
                const int rB1 = (rB0 + 1) % bufLen;
                const float frA = (float) (rA - (double) rA0);
                const float frB = (float) (rB - (double) rB0);

                for (int ch = 0; ch < chCount; ++ch)
                {
                    auto* w = buffer.getWritePointer (ch);
                    auto* d = shimmerBuf.getWritePointer (ch);

                    const float sA = d[rA0] + (d[rA1] - d[rA0]) * frA;
                    const float sB = d[rB0] + (d[rB1] - d[rB0]) * frB;
                    float octUp    = (sA * gA + sB * gB) / gSum;

                    // High-pass the cascade so each octave layer trims its
                    // own subs — otherwise the recursion piles up mud.
                    const float in = octUp;
                    const float hp = shHpA * (shimmerHpState[ch] + in - shimmerHpPrev[ch]);
                    shimmerHpPrev [ch] = in;
                    shimmerHpState[ch] = hp;
                    octUp = hp;

                    // Feedback into the reverb input; soft-clip stops runaway.
                    const float mixed = w[i] + std::tanh (octUp * fbAmt);
                    d[shimmerWritePos] = mixed;
                    w[i] = mixed;
                }

                // Advance shared cursors per sample. Write moves 1×; read
                // moves 2× to produce the +1-octave pitch shift.
                shimmerWritePos = (shimmerWritePos + 1) % bufLen;
                shimmerReadPos += 2.0;
                if (shimmerReadPos >= (double) bufLen)
                    shimmerReadPos -= (double) bufLen;
            }
        }

        // ---- Process through the JUCE reverb.
        if (numOutChannels >= 2)
            hazeReverb.processStereo (buffer.getWritePointer (0),
                                      buffer.getWritePointer (1), numSamples);
        else
            hazeReverb.processMono (buffer.getWritePointer (0), numSamples);

        // ---- POST: CAVE rolls the top off so the long tail reads as dark
        // and underground rather than splashy.
        if (preset == PresetCave)
        {
            const float lpCoef = (float) (1.0 - std::exp (-juce::MathConstants<double>::twoPi
                                                          * 2200.0 / hostSampleRate));
            for (int ch = 0; ch < chCount; ++ch)
            {
                auto* w = buffer.getWritePointer (ch);
                for (int i = 0; i < numSamples; ++i)
                {
                    hazePostLpState[ch] += lpCoef * (w[i] - hazePostLpState[ch]);
                    w[i] = hazePostLpState[ch];
                }
            }
        }
    };

    for (int slot = 0; slot < kNumEffects; ++slot)
    {
        switch (signalPathOrder[slot].load())
        {
            case EffectFilter: applyFilter(); break;
            case EffectGhost:  applyGhost();  break;
            case EffectHaze:   applyHaze();   break;
        }
    }

    // ---- TAPE: wet/dry of one of three machine flavours ------------------
    {
        const float tm = tapeMix.load();
        const int   mch = tapeMachine.load();
        const int   bufLen = tapeDelayBuf.getNumSamples();
        if (tm > 1e-4f && numOutChannels > 0 && bufLen > 32)
        {
            // Machine-specific params.
            float satDrive, lpCutoff, wowDepthMs, lfoHz;
            float tapeBitLevels = 0.0f;       // distinct from the SPEED-degrade bitLevels
            switch (mch)
            {
                case 1:  satDrive = 1.4f; lpCutoff = 8000.f; wowDepthMs = 1.8f; lfoHz = 1.4f; break;  // WOW
                case 2:  satDrive = 1.9f; lpCutoff = 5000.f; wowDepthMs = 3.2f; lfoHz = 2.7f; tapeBitLevels = 512.0f; break; // LOFI
                default: satDrive = 1.2f; lpCutoff = 12000.f; wowDepthMs = 0.0f; lfoHz = 0.0f; break; // SAT
            }
            const float lpCoef = (float) (1.0 - std::exp (-juce::MathConstants<double>::twoPi
                                                        * (double) lpCutoff / hostSampleRate));
            const float depthSamples = wowDepthMs * 0.001f * (float) hostSampleRate;
            const float lfoInc = lfoHz / (float) hostSampleRate * juce::MathConstants<float>::twoPi;

            const int chCount = juce::jmin (numOutChannels,
                                            tapeDelayBuf.getNumChannels(),
                                            kMaxFilterChannels);
            for (int i = 0; i < numSamples; ++i)
            {
                tapeLfoPhase += lfoInc;
                if (tapeLfoPhase > juce::MathConstants<float>::twoPi)
                    tapeLfoPhase -= juce::MathConstants<float>::twoPi;
                const float lfoVal = std::sin (tapeLfoPhase);

                const int writeP = (tapeDelayWritePos + i) % bufLen;

                for (int ch = 0; ch < chCount; ++ch)
                {
                    auto* w = buffer       .getWritePointer (ch);
                    auto* d = tapeDelayBuf .getWritePointer (ch);
                    const float dry = w[i];

                    // Write to delay line.
                    d[writeP] = dry;

                    // Wow-modulated read position.
                    float wet = dry;
                    if (depthSamples > 0.5f)
                    {
                        const float readOffset = depthSamples * (1.0f + lfoVal) * 0.5f + 1.0f;
                        const int   r0 = (writeP - (int) readOffset + bufLen) % bufLen;
                        wet = d[r0];
                    }

                    // Soft tape saturation.
                    wet = std::tanh (wet * satDrive) / satDrive;

                    // HF rolloff.
                    tapeLpState[ch] += lpCoef * (wet - tapeLpState[ch]);
                    wet = tapeLpState[ch];

                    // Optional bit-crush for LO-FI.
                    if (tapeBitLevels > 0.0f)
                        wet = std::round (wet * tapeBitLevels) / tapeBitLevels;

                    w[i] = dry * (1.0f - tm) + wet * tm;
                }
            }
            tapeDelayWritePos = (tapeDelayWritePos + numSamples) % bufLen;
        }
    }

    // ---- LO-FI master mode -------------------------------------------------
    // Warm creamy character on the final mix. Tanh saturation gives the
    // rounded analogue-tape body; a gentle 5-bit crush adds the digital
    // grain underneath; a touch of HF rolloff keeps the highs silky.
    // Blended 40 % wet / 60 % dry so the character is unmistakeable but
    // doesn't bulldoze the underlying mix — feels like a vibe, not a wall.
    if (lofiMode.load() && numOutChannels > 0)
    {
        const int   chCount         = juce::jmin (numOutChannels, kMaxFilterChannels);
        // Knob 0..1 → internal wet 0..0.5. Default knob 0.8 keeps the
        // previous 40 % wet feel; max 1.0 lets the user push to 50 % wet
        // for a more aggressive lo-fi mash.
        const float wetMix          = lofiMix.load() * 0.5f;
        const float dryMix          = 1.0f - wetMix;
        const float satDrive        = 2.0f;                                            // creamy warm
        const float satNormalise    = 1.0f / std::tanh (satDrive);                     // unit-gain at full-scale
        const float bitLevels       = 32.0f;                                           // ~5-bit, gentle crunch
        const float lpCoef          = (float) (1.0 - std::exp (-juce::MathConstants<double>::twoPi
                                                               * 7000.0 / juce::jmax (1.0, hostSampleRate)));

        for (int i = 0; i < numSamples; ++i)
        {
            for (int ch = 0; ch < chCount; ++ch)
            {
                auto* w = buffer.getWritePointer (ch);
                const float dry = w[i];
                float       wet = dry;

                // 1) Warm creamy saturation (tape-style tanh).
                wet = std::tanh (wet * satDrive) * satNormalise;

                // 2) Gentle bit-crush — adds quantisation grain without
                //    the harsh stepping of low bit depths.
                wet = std::round (wet * bitLevels) / bitLevels;

                // 3) Soft HF rolloff so the crush doesn't read as piercing.
                lofiLpState[ch] += lpCoef * (wet - lofiLpState[ch]);
                wet = lofiLpState[ch];

                // 4) Parallel blend with the dry signal.
                w[i] = juce::jlimit (-1.0f, 1.0f, dry * dryMix + wet * wetMix);
            }
        }
    }

    // ---- Finalise recording window for this block --------------------------
    // Both the input-capture block (top of processBlock) and the MIDI voice
    // renderer wrote into recordBuffer at [recStartPos, recStartPos + recBlockLen).
    // Advance recordPos here so the next block continues where this one ended,
    // and auto-stop if we hit capacity.
    if (recordingNow && recBlockLen > 0)
    {
        const int newPos = recStartPos + recBlockLen;
        recordPos.store (newPos);
        if (newPos >= recCap)
            recording.store (false);
    }
}

//==============================================================================
bool SpoolAudioProcessor::loadFile (const juce::File& file)
{
    juce::Logger::writeToLog ("loadFile: " + file.getFullPathName());
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader == nullptr)
    {
        juce::Logger::writeToLog ("loadFile: FAILED — no reader for " + file.getFileName());
        return false;
    }
    juce::Logger::writeToLog (juce::String::formatted (
        "loadFile: ch=%d sr=%.0f len=%lld",
        (int) reader->numChannels, reader->sampleRate, (long long) reader->lengthInSamples));

    auto newSample = new Sample();
    newSample->sourceSampleRate = reader->sampleRate;
    newSample->name             = file.getFileNameWithoutExtension();
    newSample->buffer.setSize ((int) reader->numChannels,
                               (int) reader->lengthInSamples,
                               false, true, false);

    reader->read (&newSample->buffer, 0, (int) reader->lengthInSamples, 0, true, true);

    Sample::Ptr keepAlive (newSample);

    // Swap atomically.
    {
        const juce::SpinLock::ScopedLockType lock (sampleLock);
        currentSample = keepAlive;
    }

    playPosition.store (0.0);

    // Refresh thumbnail for the editor's waveform display.
    thumbnail.setSource (new juce::FileInputSource (file));

    return true;
}

void SpoolAudioProcessor::play()         { playing.store (true); }
void SpoolAudioProcessor::stop()         { playing.store (false); playPosition.store (0.0); }
void SpoolAudioProcessor::togglePlay()   { playing.store (! playing.load()); }

void SpoolAudioProcessor::seekNormalised (float norm) noexcept
{
    Sample::Ptr local;
    {
        const juce::SpinLock::ScopedLockType lock (sampleLock);
        local = currentSample;
    }
    if (local == nullptr) return;

    const double clamped = (double) juce::jlimit (0.0f, 1.0f, norm);
    playPosition.store (clamped * (double) local->buffer.getNumSamples());
}

void SpoolAudioProcessor::setScratching (bool on) noexcept
{
    // Zero the angular accumulator at every fresh contact so the audio
    // thread starts from a known baseline (no spurious delta carried over
    // from the previous scratch session).
    if (on)
    {
        const juce::SpinLock::ScopedLockType lock (scratchAccumLock);
        scratchAccumAngleRad = 0.0f;
    }
    scratchActive.store (on);
}

void SpoolAudioProcessor::pushScratchAngleDelta (float deltaRadians) noexcept
{
    if (! std::isfinite (deltaRadians)) return;
    // Clamp per-event delta so a single huge jump can't push the
    // accumulator far enough to overflow per-block rate clamps further
    // down. ±2π per event = up to one full rotation in a single mouse
    // sample, which is already a violent flick.
    const float clamped = juce::jlimit (-juce::MathConstants<float>::twoPi,
                                         juce::MathConstants<float>::twoPi,
                                         deltaRadians);
    const juce::SpinLock::ScopedLockType lock (scratchAccumLock);
    scratchAccumAngleRad += clamped;
}

void SpoolAudioProcessor::setLoopAnchorFromNormalised (float norm) noexcept
{
    Sample::Ptr local;
    {
        const juce::SpinLock::ScopedLockType lock (sampleLock);
        local = currentSample;
    }
    if (local == nullptr) return;
    const double clamped = (double) juce::jlimit (0.0f, 1.0f, norm);
    pendingLoopAnchor.store (clamped * (double) local->buffer.getNumSamples());
}

void SpoolAudioProcessor::setCustomLoopRegionNormalised (float startNorm, float endNorm) noexcept
{
    Sample::Ptr local;
    {
        const juce::SpinLock::ScopedLockType lock (sampleLock);
        local = currentSample;
    }
    if (local == nullptr || local->buffer.getNumSamples() == 0) return;

    const double N = (double) local->buffer.getNumSamples();
    const double s = juce::jlimit (0.0, 1.0, (double) juce::jmin (startNorm, endNorm)) * N;
    const double e = juce::jlimit (0.0, 1.0, (double) juce::jmax (startNorm, endNorm)) * N;
    if (e - s < 1.0) return; // degenerate
    customLoopStart.store (s);
    customLoopEnd  .store (e);
    // Drag-highlighted regions aren't beat-locked, so clear the rescale
    // hook used by the SIZE buttons.
    lastLoopSizeBeats.store (0.0f);
}

void SpoolAudioProcessor::clearCustomLoop() noexcept
{
    customLoopStart.store (-1.0);
    customLoopEnd  .store (-1.0);
    lastLoopSizeBeats.store (0.0f);
}

void SpoolAudioProcessor::setLoopSizeBeats (float beats) noexcept
{
    Sample::Ptr local;
    {
        const juce::SpinLock::ScopedLockType lock (sampleLock);
        local = currentSample;
    }
    if (local == nullptr || local->buffer.getNumSamples() == 0) return;

    const double srcSr        = local->sourceSampleRate > 0.0 ? local->sourceSampleRate : hostSampleRate;
    const double samplesPerBt = srcSr * 60.0 / internalBpm.load();
    const double winLen       = juce::jmax (8.0, samplesPerBt * (double) beats);
    const double total        = (double) local->buffer.getNumSamples();
    const double start        = juce::jlimit (0.0, juce::jmax (0.0, total - winLen),
                                              playPosition.load());

    customLoopStart.store (start);
    customLoopEnd  .store (juce::jmin (total, start + winLen));
    lastLoopSizeBeats.store (beats);          // remember so setBpm can rescale
    loopLengthMode .store (1);                // auto-engage loop
    loopShouldResetNudge.store (true);
    juce::Logger::writeToLog (juce::String::formatted (
        "setLoopSizeBeats %.3f → start=%.0f end=%.0f", beats, start, start + winLen));
}

void SpoolAudioProcessor::setBpm (double bpm) noexcept
{
    const double newBpm = juce::jlimit (40.0, 240.0, bpm);
    internalBpm.store (newBpm);

    // If the user previously locked in a SIZE-button loop, rescale its
    // end-point to keep it the same number of BEATS at the new tempo.
    const float beats = lastLoopSizeBeats.load();
    if (beats <= 0.0f) return;

    Sample::Ptr local;
    {
        const juce::SpinLock::ScopedLockType lock (sampleLock);
        local = currentSample;
    }
    if (local == nullptr || local->buffer.getNumSamples() == 0) return;

    const double srcSr        = local->sourceSampleRate > 0.0 ? local->sourceSampleRate : hostSampleRate;
    const double samplesPerBt = srcSr * 60.0 / newBpm;
    const double winLen       = juce::jmax (8.0, samplesPerBt * (double) beats);
    const double total        = (double) local->buffer.getNumSamples();
    const double s            = customLoopStart.load();
    if (s < 0.0) return;       // no active loop region
    const double newEnd       = juce::jmin (total, s + winLen);
    customLoopEnd.store (newEnd);
    juce::Logger::writeToLog (juce::String::formatted (
        "setBpm %.1f → rescaled loop end=%.0f (beats=%.3f)", newBpm, newEnd, beats));
}

bool SpoolAudioProcessor::hasCustomLoop() const noexcept
{
    const double s = customLoopStart.load();
    const double e = customLoopEnd  .load();
    return s >= 0.0 && e > s;
}

float SpoolAudioProcessor::getLoopRegionStartNormalised() const noexcept
{
    Sample::Ptr local;
    {
        const juce::SpinLock::ScopedLockType lock (sampleLock);
        local = currentSample;
    }
    if (local == nullptr || local->buffer.getNumSamples() == 0) return 0.0f;

    const double N = (double) local->buffer.getNumSamples();
    const double srcSr = local->sourceSampleRate > 0.0 ? local->sourceSampleRate : hostSampleRate;
    const double offsetSamps = (double) loopAnchorOffsetSeconds.load() * srcSr;

    if (hasCustomLoop())
    {
        // Translate the highlighted region by the nudge offset, clamping so
        // it can't slide past either end of the sample.
        const double s0 = customLoopStart.load();
        const double e0 = customLoopEnd.load();
        const double winLen = e0 - s0;
        const double maxStart = juce::jmax (0.0, N - winLen);
        const double s = juce::jlimit (0.0, maxStart, s0 + offsetSamps);
        return (float) juce::jlimit (0.0, 1.0, s / N);
    }

    if (loopLengthMode.load() > 0 && loopAnchorBase >= 0.0)
        return (float) juce::jlimit (0.0, 1.0, (loopAnchorBase + offsetSamps) / N);
    return 0.0f;
}

float SpoolAudioProcessor::getLoopRegionEndNormalised() const noexcept
{
    Sample::Ptr local;
    {
        const juce::SpinLock::ScopedLockType lock (sampleLock);
        local = currentSample;
    }
    if (local == nullptr || local->buffer.getNumSamples() == 0) return 0.0f;

    const double N = (double) local->buffer.getNumSamples();
    const double srcSr = local->sourceSampleRate > 0.0 ? local->sourceSampleRate : hostSampleRate;
    const double offsetSamps = (double) loopAnchorOffsetSeconds.load() * srcSr;

    if (hasCustomLoop())
    {
        const double s0 = customLoopStart.load();
        const double e0 = customLoopEnd.load();
        const double winLen = e0 - s0;
        const double maxStart = juce::jmax (0.0, N - winLen);
        const double s = juce::jlimit (0.0, maxStart, s0 + offsetSamps);
        return (float) juce::jlimit (0.0, 1.0, (s + winLen) / N);
    }

    if (loopLengthMode.load() > 0 && loopAnchorBase >= 0.0)
    {
        const double samplesPerBt = srcSr * 60.0 / internalBpm.load();
        const double windowSamps  = samplesPerBt * kLoopFractionOfBeat[loopLengthMode.load()];
        return (float) juce::jlimit (0.0, 1.0, (loopAnchorBase + offsetSamps + windowSamps) / N);
    }
    return 0.0f;
}

void SpoolAudioProcessor::commitNudgeOffset() noexcept
{
    Sample::Ptr local;
    {
        const juce::SpinLock::ScopedLockType lock (sampleLock);
        local = currentSample;
    }
    const float offSec = loopAnchorOffsetSeconds.load();
    if (std::abs (offSec) < 1.0e-6f) return;
    if (local == nullptr || local->buffer.getNumSamples() == 0) { loopAnchorOffsetSeconds.store (0.0f); return; }

    const double N      = (double) local->buffer.getNumSamples();
    const double srcSr  = local->sourceSampleRate > 0.0 ? local->sourceSampleRate : hostSampleRate;
    const double offS   = (double) offSec * srcSr;

    if (hasCustomLoop())
    {
        const double s0 = customLoopStart.load();
        const double e0 = customLoopEnd.load();
        const double winLen = e0 - s0;
        const double maxStart = juce::jmax (0.0, N - winLen);
        const double s = juce::jlimit (0.0, maxStart, s0 + offS);
        customLoopStart.store (s);
        customLoopEnd  .store (s + winLen);
    }
    else if (loopLengthMode.load() > 0 && loopAnchorBase >= 0.0)
    {
        loopAnchorBase = juce::jlimit (0.0, N, loopAnchorBase + offS);
    }
    loopAnchorOffsetSeconds.store (0.0f);
    juce::Logger::writeToLog (juce::String::formatted ("commitNudge: offset=%.3fs baked into region", offSec));
}

void SpoolAudioProcessor::clearSample()
{
    juce::Logger::writeToLog ("clearSample");
    playing.store (false);
    playPosition.store (0.0);
    {
        const juce::SpinLock::ScopedLockType lock (sampleLock);
        currentSample = nullptr;
    }
    loopLengthMode.store (0);   // also disengage any active loop
    thumbnail.clear();
}

//==============================================================================
// Folder browsing
//
namespace
{
    struct FileNameSorter
    {
        int compareElements (const juce::File& a, const juce::File& b)
        {
            return a.getFileName().compareIgnoreCase (b.getFileName());
        }
    };
}

void SpoolAudioProcessor::setFolder (const juce::File& folder)
{
    juce::Logger::writeToLog ("setFolder: " + folder.getFullPathName());
    folderFiles.clear();
    folderIndex   = -1;
    currentFolder = juce::File{};

    if (! folder.isDirectory())
    {
        juce::Logger::writeToLog ("setFolder: not a directory, abort");
        return;
    }

    currentFolder = folder;
    folder.findChildFiles (folderFiles, juce::File::findFiles, false,
                           "*.wav;*.WAV;*.mp3;*.MP3;*.aif;*.aiff;*.flac;*.FLAC;*.ogg");
    FileNameSorter sorter;
    folderFiles.sort (sorter);

    juce::Logger::writeToLog (juce::String::formatted ("setFolder: found %d files",
                                                       folderFiles.size()));
    if (folderFiles.size() > 0)
    {
        folderIndex = 0;
        loadFile (folderFiles.getReference (0));
    }
}

void SpoolAudioProcessor::loadNextInFolder()
{
    if (folderFiles.size() == 0) return;
    folderIndex = (folderIndex + 1) % folderFiles.size();
    loadFile (folderFiles.getReference (folderIndex));
}

void SpoolAudioProcessor::loadPrevInFolder()
{
    if (folderFiles.size() == 0) return;
    folderIndex = (folderIndex - 1 + folderFiles.size()) % folderFiles.size();
    loadFile (folderFiles.getReference (folderIndex));
}

double SpoolAudioProcessor::getPlayPositionSeconds() const noexcept
{
    Sample::Ptr local;
    {
        const juce::SpinLock::ScopedLockType lock (sampleLock);
        local = currentSample;
    }
    if (local == nullptr || local->buffer.getNumSamples() == 0)
        return 0.0;
    const double sr = local->sourceSampleRate > 0.0 ? local->sourceSampleRate : hostSampleRate;
    if (sr <= 0.0) return 0.0;
    const double midi = midiPlayheadPos.load();
    const double pos  = playing.load()        ? playPosition.load()
                      : (midi >= 0.0          ? midi : playPosition.load());
    return pos / sr;
}

float SpoolAudioProcessor::getNormalisedPosition() const noexcept
{
    Sample::Ptr local;
    {
        const juce::SpinLock::ScopedLockType lock (sampleLock);
        local = currentSample;
    }
    if (local == nullptr || local->buffer.getNumSamples() == 0)
        return 0.0f;

    const auto totalSamples = (double) local->buffer.getNumSamples();

    // If the PLAY-button transport is running, the playhead follows that.
    // Otherwise, if a MIDI voice is currently sounding, surface ITS read
    // position so the waveform marker tracks keyboard / arp playback too.
    // (Without this fallback the playhead just sits frozen even though MIDI
    //  is actively scrubbing through the sample.)
    const bool transportRunning = playing.load();
    const double midiPos = midiPlayheadPos.load();
    double pos = transportRunning ? playPosition.load()
                                  : (midiPos >= 0.0 ? midiPos : playPosition.load());

    return juce::jlimit (0.0f, 1.0f, (float) (pos / totalSamples));
}

double SpoolAudioProcessor::getDurationSeconds() const noexcept
{
    Sample::Ptr local;
    {
        const juce::SpinLock::ScopedLockType lock (sampleLock);
        local = currentSample;
    }
    if (local == nullptr || local->sourceSampleRate <= 0.0)
        return 0.0;

    return (double) local->buffer.getNumSamples() / local->sourceSampleRate;
}

juce::String SpoolAudioProcessor::getLoadedSampleName() const
{
    Sample::Ptr local;
    {
        const juce::SpinLock::ScopedLockType lock (sampleLock);
        local = currentSample;
    }
    return local != nullptr ? local->name : juce::String();
}

//==============================================================================
void SpoolAudioProcessor::startRecording()
{
    juce::Logger::writeToLog ("startRecording");
    recordPos.store (0);
    recording.store (true);
}

void SpoolAudioProcessor::stopRecording()
{
    juce::Logger::writeToLog (juce::String::formatted ("stopRecording: captured=%d",
                                                       (int) recordPos.load()));
    if (! recording.load() && recordPos.load() == 0)
        return;

    recording.store (false);

    const int captured = recordPos.load();
    if (captured <= 0)
        return;

    // Build a Sample from the captured portion and swap it in.
    auto newSample = new Sample();
    newSample->sourceSampleRate = hostSampleRate;
    newSample->name             = "recording_" + juce::Time::getCurrentTime().formatted ("%H%M%S");
    newSample->buffer.setSize (recordBuffer.getNumChannels(), captured, false, true, false);
    for (int ch = 0; ch < recordBuffer.getNumChannels(); ++ch)
        newSample->buffer.copyFrom (ch, 0, recordBuffer, ch, 0, captured);

    Sample::Ptr keepAlive (newSample);
    {
        const juce::SpinLock::ScopedLockType lock (sampleLock);
        currentSample = keepAlive;
    }
    playPosition.store (0.0);

    // Push captured audio into the thumbnail so the editor draws a waveform.
    thumbnail.reset (newSample->buffer.getNumChannels(), hostSampleRate, captured);
    thumbnail.addBlock (0, newSample->buffer, 0, captured);

    // Live-looper feel: as soon as recording stops, start playback with loop on.
    looping.store (true);
    playing.store (true);
}

float SpoolAudioProcessor::getRecordProgress() const noexcept
{
    if (recordBuffer.getNumSamples() == 0) return 0.0f;
    return juce::jlimit (0.0f, 1.0f,
                         (float) recordPos.load() / (float) recordBuffer.getNumSamples());
}

//==============================================================================
juce::File SpoolAudioProcessor::renderLoopToTempWav()
{
    Sample::Ptr local;
    {
        const juce::SpinLock::ScopedLockType lock (sampleLock);
        local = currentSample;
    }
    if (local == nullptr || local->buffer.getNumSamples() == 0)
        return {};

    const double sr      = local->sourceSampleRate > 0.0 ? local->sourceSampleRate : hostSampleRate;
    const int    totalN  = local->buffer.getNumSamples();
    const int    numCh   = local->buffer.getNumChannels();

    // Pick the loop region: custom-highlight first, then knob-loop, then whole.
    int startSample = 0;
    int length      = totalN;
    if (hasCustomLoop())
    {
        const double s = customLoopStart.load();
        const double e = customLoopEnd.load();
        startSample = juce::jlimit (0, totalN - 1, (int) std::round (s));
        length      = juce::jmax (1, juce::jmin ((int) std::round (e - s),
                                                  totalN - startSample));
    }
    else if (loopLengthMode.load() > 0)
    {
        const double samplesPerBt = sr * 60.0 / internalBpm.load();
        const double windowSamps  = samplesPerBt * kLoopFractionOfBeat[loopLengthMode.load()];
        const double offsetSamps  = (double) loopAnchorOffsetSeconds.load() * sr;
        const double anchorBase   = (loopAnchorBase >= 0.0) ? loopAnchorBase : 0.0;
        const double maxAnchor    = juce::jmax (0.0, (double) totalN - windowSamps);
        const double anchor       = juce::jlimit (0.0, maxAnchor, anchorBase + offsetSamps);
        startSample = (int) std::round (anchor);
        length      = juce::jmax (1, juce::jmin ((int) std::round (windowSamps), totalN - startSample));
    }

    // Slice the source buffer to the loop region.
    juce::AudioBuffer<float> slice (numCh, length);
    for (int ch = 0; ch < numCh; ++ch)
        slice.copyFrom (ch, 0, local->buffer, ch, startSample, length);

    // Render through the offline effect chain.
    juce::AudioBuffer<float> out;
    renderProcessed (slice, sr, out);

    // Write to a fresh temp WAV next to the session dir so the host has
    // time to ingest the file before any cleanup.
    const auto dragDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                             .getChildFile ("SPOOL").getChildFile ("drag");
    dragDir.createDirectory();
    const auto baseName = getLoadedSampleName().isEmpty() ? juce::String ("loop")
                                                          : getLoadedSampleName();
    const auto stamp = juce::Time::getCurrentTime().formatted ("%H%M%S");
    const auto file  = dragDir.getChildFile (baseName + "_spool_" + stamp + ".wav");

    file.deleteFile();
    std::unique_ptr<juce::FileOutputStream> stream (file.createOutputStream());
    if (stream == nullptr) return {};

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (
        wav.createWriterFor (stream.get(), sr,
                             (unsigned int) out.getNumChannels(), 24, {}, 0));
    if (writer == nullptr) return {};
    stream.release();
    if (! writer->writeFromAudioSampleBuffer (out, 0, out.getNumSamples()))
        return {};
    writer.reset();    // flush

    juce::Logger::writeToLog (juce::String::formatted (
        "renderLoopToTempWav: %d samples → %s", out.getNumSamples(),
        file.getFullPathName().toRawUTF8()));
    return file;
}

bool SpoolAudioProcessor::exportToWav (const juce::File& destination)
{
    Sample::Ptr local;
    {
        const juce::SpinLock::ScopedLockType lock (sampleLock);
        local = currentSample;
    }
    if (local == nullptr || local->buffer.getNumSamples() == 0)
        return false;

    const double sr = local->sourceSampleRate > 0.0 ? local->sourceSampleRate : hostSampleRate;

    juce::AudioBuffer<float> out;
    renderProcessed (local->buffer, sr, out);
    const int N  = out.getNumSamples();
    const int CH = out.getNumChannels();

    // Write to disk.
    destination.deleteFile();
    std::unique_ptr<juce::FileOutputStream> stream (destination.createOutputStream());
    if (stream == nullptr) return false;

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (
        wav.createWriterFor (stream.get(), sr, (unsigned int) CH, 24, {}, 0));
    if (writer == nullptr) return false;

    stream.release();
    return writer->writeFromAudioSampleBuffer (out, 0, N);
}

//==============================================================================
// Offline render — apply the FULL effect chain (sample gain, INPUT preamp
// wet/dry, GHOST filtered delay, HAZE reverb) to a source buffer using fresh
// per-channel state so live processing isn't disturbed. Used by EXPORT and
// slot SAVE so the resulting WAV / slot already sounds like it does live.
//
void SpoolAudioProcessor::renderProcessed (const juce::AudioBuffer<float>& src,
                                           double srcSr,
                                           juce::AudioBuffer<float>& out)
{
    const int N      = src.getNumSamples();
    const int srcCH  = juce::jmax (1, src.getNumChannels());
    const int CH     = juce::jmax (2, srcCH);          // always at least stereo output
    out.setSize (CH, N, false, true, false);
    out.clear();
    for (int ch = 0; ch < CH; ++ch)
        out.copyFrom (ch, 0, src, ch % srcCH, 0, N);

    // 1) SAMPLE GAIN.
    out.applyGain (sampleGain.load());

    // 2) INPUT preamp wet/dry. (Always before the JUICE chain for offline.)
    {
        const float mix = inputMix.load();
        if (mix > 1e-4f)
        {
            const auto params = getCompParams (inputCompMode.load());
            const float threshold = 0.18f, ratio = 24.0f, makeup = 3.16f;
            const float attackC  = std::exp (-1.0f / (float) (srcSr * params.attackMs  * 0.001));
            const float releaseC = std::exp (-1.0f / (float) (srcSr * params.releaseMs * 0.001));
            const float lpCoef   = (float) (1.0 - std::exp (-juce::MathConstants<double>::twoPi
                                                          * (double) params.lpHz / srcSr));
            const float drive    = params.satDrive;
            const float bias     = 0.10f;
            const float biasOut  = std::tanh (bias * drive);

            for (int ch = 0; ch < CH; ++ch)
            {
                float env = 0.0f, lp = 0.0f, hpState = 0.0f, hpPrev = 0.0f;
                auto* w = out.getWritePointer (ch);
                for (int i = 0; i < N; ++i)
                {
                    const float dry = w[i];
                    const float r   = std::abs (dry);
                    const float c   = r > env ? attackC : releaseC;
                    env = c * env + (1.0f - c) * r;
                    float gr = 1.0f;
                    if (env > threshold) gr = std::pow (env / threshold, -(1.0f - 1.0f / ratio));
                    float wet = dry * gr * makeup;
                    wet = (std::tanh ((wet + bias) * drive) - biasOut) / drive;
                    lp += lpCoef * (wet - lp);
                    wet = lp;
                    const float y = wet - hpPrev + warmthHpCoeff * hpState;
                    hpPrev = wet; hpState = y;
                    w[i] = dry * (1.0f - mix) + y * mix;
                }
            }
        }
    }

    // 3) GHOST (filtered feedback delay).
    {
        const float gh = ghostAmount.load();
        if (gh > 1e-4f)
        {
            const int   tm        = ghostTimeMode.load();
            const float beatFrac  = (tm == 0) ? 0.25f : (tm == 1) ? 0.5f : 1.0f;
            const int   delaySamps = juce::jmax (4, (int) ((float) srcSr * (60.0f / (float) internalBpm.load()) * beatFrac));
            const float feedback  = juce::jlimit (0.0f, 0.92f, gh * 0.92f);
            const float lpCutoff  = 8000.0f - gh * 7500.0f;
            const float lpCoef    = (float) (1.0 - std::exp (-juce::MathConstants<double>::twoPi
                                                            * (double) lpCutoff / srcSr));
            const int   bufLen    = juce::jmax (delaySamps + 1, (int) srcSr);

            juce::AudioBuffer<float> dbuf (CH, bufLen);
            dbuf.clear();
            float fbLp[8] = {};
            int   writeP = 0;

            for (int i = 0; i < N; ++i)
            {
                const int readP = (writeP - delaySamps + bufLen) % bufLen;
                for (int ch = 0; ch < juce::jmin (CH, 8); ++ch)
                {
                    auto* w = out.getWritePointer (ch);
                    auto* d = dbuf.getWritePointer (ch);
                    const float in = w[i];
                    const float delayed = d[readP];
                    fbLp[ch] += lpCoef * (delayed - fbLp[ch]);
                    d[writeP] = in + fbLp[ch] * feedback;
                    w[i] = in * (1.0f - gh) + delayed * gh;
                }
                writeP = (writeP + 1) % bufLen;
            }
        }
    }

    // 4) HAZE reverb (freeze skipped — no infinite tail offline).
    {
        const float haze = hazeAmount.load();
        if (haze > 1e-4f && CH >= 2)
        {
            juce::Reverb rv;
            rv.setSampleRate (srcSr);
            juce::Reverb::Parameters p;
            p.roomSize = 0.35f + haze * 0.6f;
            p.damping  = 0.4f;
            p.wetLevel = haze;
            p.dryLevel = 1.0f;
            p.width    = 1.0f;
            rv.setParameters (p);
            rv.processStereo (out.getWritePointer (0), out.getWritePointer (1), N);
        }
    }
}

//==============================================================================
// Loop slots
//
bool SpoolAudioProcessor::isSlotFilled (int slot) const noexcept
{
    return slot >= 0 && slot < kNumSlots && slots[(size_t) slot] != nullptr;
}

bool SpoolAudioProcessor::saveCurrentLoopToSlot (int slot)
{
    juce::Logger::writeToLog (juce::String::formatted ("saveCurrentLoopToSlot: slot=%d loop=%d",
                                                       slot, loopLengthMode.load()));
    if (slot < 0 || slot >= kNumSlots) return false;

    // Snapshot the current sample buffer. If LOOP is engaged we save just the
    // loop window; otherwise we save the entire sample.
    Sample::Ptr src;
    {
        const juce::SpinLock::ScopedLockType lock (sampleLock);
        src = currentSample;
    }
    if (src == nullptr || src->buffer.getNumSamples() == 0) return false;

    const double srcSr     = src->sourceSampleRate > 0.0 ? src->sourceSampleRate : hostSampleRate;
    const int    totalSrc  = src->buffer.getNumSamples();

    int startSample = 0;
    int length      = totalSrc;
    const char* sourceKind = "whole-sample";
    // Highest priority: custom drag region (the orange highlight on the waveform).
    if (hasCustomLoop())
    {
        const double s = customLoopStart.load();
        const double e = customLoopEnd.load();
        startSample = juce::jlimit (0, totalSrc - 1, (int) std::round (s));
        length      = juce::jmax (0, juce::jmin ((int) std::round (e - s),
                                                 totalSrc - startSample));
        sourceKind  = "custom-drag";
    }
    else if (loopLengthMode.load() > 0)
    {
        const double samplesPerBt = srcSr * 60.0 / internalBpm.load();
        const double windowSamps  = samplesPerBt * kLoopFractionOfBeat[loopLengthMode.load()];
        const double offsetSamps  = (double) loopAnchorOffsetSeconds.load() * srcSr;
        const double anchorBase   = (loopAnchorBase >= 0.0) ? loopAnchorBase : 0.0;
        const double maxAnchor    = juce::jmax (0.0, (double) totalSrc - windowSamps);
        const double anchor       = juce::jlimit (0.0, maxAnchor, anchorBase + offsetSamps);
        startSample = (int) std::round (anchor);
        length      = juce::jmin ((int) std::round (windowSamps), totalSrc - startSample);
        sourceKind  = "knob-loop";
    }
    juce::Logger::writeToLog (juce::String::formatted (
        "saveSlot: src=%s startSample=%d length=%d", sourceKind, startSample, length));
    if (length <= 0) return false;

    // Auto-trim leading + trailing silence so the loop plays tight against
    // its first/last audible sample. Threshold -60 dB (≈ 0.001 amplitude).
    // Without this, a manually highlighted region almost always carries a
    // few ms of dead air on each side, audible as a stutter on loop wrap.
    const int   numCh = src->buffer.getNumChannels();
    const float silenceThresh = 0.001f;
    int trimStart = startSample;
    int trimEnd   = startSample + length;        // exclusive
    {
        auto peakAt = [&] (int idx) {
            float p = 0.0f;
            for (int ch = 0; ch < numCh; ++ch)
                p = juce::jmax (p, std::abs (src->buffer.getSample (ch, idx)));
            return p;
        };
        while (trimStart < trimEnd && peakAt (trimStart) < silenceThresh) ++trimStart;
        while (trimEnd > trimStart && peakAt (trimEnd - 1) < silenceThresh) --trimEnd;
    }
    const int trimmedLen = trimEnd - trimStart;
    juce::Logger::writeToLog (juce::String::formatted (
        "saveSlot: trimmed %d→%d samples (lead=%d, trail=%d)",
        length, trimmedLen, trimStart - startSample,
        (startSample + length) - trimEnd));
    if (trimmedLen <= 0)
    {
        // Selection was entirely silent — keep the original window so we
        // don't end up storing a zero-length slot.
        // (No-op: keep startSample / length as-is.)
    }
    else
    {
        startSample = trimStart;
        length      = trimmedLen;
    }

    // Store RAW loop-window audio. All current effect settings go into the
    // snapshot below — on load we restore the snapshot so the live chain
    // reproduces the same sound (and the same tempo / signal-path order).
    auto* newSlot = new Sample();
    newSlot->sourceSampleRate = srcSr;
    newSlot->name             = "slot_" + juce::String (slot + 1);
    newSlot->buffer.setSize (numCh, length, false, true, false);
    for (int ch = 0; ch < numCh; ++ch)
        newSlot->buffer.copyFrom (ch, 0, src->buffer, ch, startSample, length);

    // Tiny 2 ms equal-power crossfade at the boundary — prevents audible
    // click on loop wrap without eating into the body of the audio.
    {
        const int fadeLen = juce::jlimit (8, length / 8,
                                          (int) (0.002 * srcSr));
        if (fadeLen > 0)
        {
            for (int ch = 0; ch < numCh; ++ch)
            {
                auto* d = newSlot->buffer.getWritePointer (ch);
                for (int i = 0; i < fadeLen; ++i)
                {
                    const float t = (float) i / (float) fadeLen;
                    const float g = std::sin (t * juce::MathConstants<float>::halfPi);
                    d[i]                       *= g;
                    d[length - 1 - i]          *= g;
                }
            }
        }
    }

    slots[(size_t) slot] = Sample::Ptr (newSlot);

    // Snapshot ALL effect/tempo state so the slot reproduces the sound exactly.
    SlotSnapshot snap;
    snap.inputMix       = inputMix.load();
    snap.inputCompMode  = inputCompMode.load();
    snap.sampleGain     = sampleGain.load();
    snap.playbackSpeed  = playbackSpeed.load();
    snap.filterPos      = filterPos.load();
    snap.filterQMode    = filterQMode.load();
    snap.filterLfoRate  = filterLfoRate.load();
    snap.filterLfoRange = filterLfoRange.load();
    snap.bpm            = internalBpm.load();
    snap.ghostAmount    = ghostAmount.load();
    snap.ghostTimeMode  = ghostTimeMode.load();
    snap.hazeAmount     = hazeAmount.load();
    snap.hazeFrozen     = hazeFrozen.load();
    snap.hazePreset     = hazePreset.load();
    snap.tapeMix        = tapeMix.load();
    snap.tapeMachine    = tapeMachine.load();
    snap.loopCutoffMode = loopCutoffMode.load();
    for (int i = 0; i < 3; ++i) snap.signalOrder[i] = signalPathOrder[i].load();
    snap.valid          = true;
    slotSnapshots[(size_t) slot] = snap;
    juce::Logger::writeToLog (juce::String::formatted (
        "saveSlot: snap fp=%.2f gh=%.2f hz=%.2f speed=%.2f order=%d,%d,%d",
        snap.filterPos, snap.ghostAmount, snap.hazeAmount, snap.playbackSpeed,
        snap.signalOrder[0], snap.signalOrder[1], snap.signalOrder[2]));

    // Mirror to disk so the user can find the WAV on their filesystem.
    const auto wavFile = sessionDir.getChildFile ("slot_" + juce::String (slot + 1) + ".wav");
    wavFile.deleteFile();
    std::unique_ptr<juce::FileOutputStream> stream (wavFile.createOutputStream());
    if (stream != nullptr)
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer (
            wav.createWriterFor (stream.get(), srcSr,
                                 (unsigned int) newSlot->buffer.getNumChannels(),
                                 24, {}, 0));
        if (writer != nullptr)
        {
            stream.release(); // writer owns it now
            writer->writeFromAudioSampleBuffer (newSlot->buffer, 0, length);
        }
    }
    return true;
}

bool SpoolAudioProcessor::loadSlot (int slot)
{
    juce::Logger::writeToLog (juce::String::formatted ("loadSlot[%d]: entry", slot));

    if (slot < 0 || slot >= kNumSlots)
    {
        juce::Logger::writeToLog ("loadSlot: bad slot index");
        return false;
    }
    if (! isSlotFilled (slot))
    {
        juce::Logger::writeToLog ("loadSlot: slot empty");
        return false;
    }

    auto sample = slots[(size_t) slot];
    if (sample == nullptr
        || sample->buffer.getNumSamples()  == 0
        || sample->buffer.getNumChannels() == 0
        || sample->sourceSampleRate <= 0.0)
    {
        juce::Logger::writeToLog ("loadSlot: sample invalid (null/empty/zero-sr)");
        return false;
    }

    juce::Logger::writeToLog (juce::String::formatted (
        "loadSlot: ch=%d sr=%.0f samples=%d",
        sample->buffer.getNumChannels(),
        sample->sourceSampleRate,
        sample->buffer.getNumSamples()));

    // Quiesce playback BEFORE the swap so the audio thread can't be mid-read
    // on the old sample while we replace it.
    playing.store (false);
    customLoopStart.store (-1.0);
    customLoopEnd  .store (-1.0);
    loopLengthMode .store (0);             // fresh playback, no loop window
    loopAnchorOffsetSeconds.store (0.0f);
    loopShouldResetNudge.store (true);
    juce::Logger::writeToLog ("loadSlot: quiesced");

    {
        const juce::SpinLock::ScopedLockType lock (sampleLock);
        currentSample = sample;
    }
    playPosition.store (0.0);
    juce::Logger::writeToLog ("loadSlot: sample swapped");

    // Refresh thumbnail — validated above so these args are safe.
    thumbnail.reset (sample->buffer.getNumChannels(),
                     sample->sourceSampleRate,
                     sample->buffer.getNumSamples());
    thumbnail.addBlock (0, sample->buffer, 0, sample->buffer.getNumSamples());
    juce::Logger::writeToLog ("loadSlot: thumbnail updated");

    // Restore the snapshot — exact effect settings + tempo + signal order.
    const auto& snap = slotSnapshots[(size_t) slot];
    if (snap.valid)
    {
        inputMix      .store (snap.inputMix);
        inputCompMode .store (snap.inputCompMode);
        sampleGain    .store (snap.sampleGain);
        playbackSpeed .store (snap.playbackSpeed);
        filterPos     .store (snap.filterPos);
        filterQMode   .store (snap.filterQMode);
        filterLfoRate .store (snap.filterLfoRate);
        filterLfoRange.store (snap.filterLfoRange);
        setBpm (snap.bpm);
        ghostAmount   .store (snap.ghostAmount);
        ghostTimeMode .store (snap.ghostTimeMode);
        hazeAmount    .store (snap.hazeAmount);
        hazeFrozen    .store (snap.hazeFrozen);
        hazePreset    .store (snap.hazePreset);
        tapeMix       .store (snap.tapeMix);
        tapeMachine   .store (snap.tapeMachine);
        loopCutoffMode.store (snap.loopCutoffMode);
        for (int i = 0; i < 3; ++i) signalPathOrder[i].store (snap.signalOrder[i]);

        // Reset per-channel filter / envelope state so playback starts clean.
        for (int ch = 0; ch < kMaxFilterChannels; ++ch)
        {
            warmthHpState[ch] = warmthHpPrev[ch] = warmthLpState[ch] = 0.0f;
            compEnv      [ch] = outCompEnv [ch] = 0.0f;
            outLpState   [ch] = outHpState [ch] = outHpPrev[ch] = 0.0f;
            ghostFbLp    [ch] = 0.0f;
            svfLow       [ch] = svfBand    [ch] = 0.0f;
            srrHold      [ch] = 0.0f;
        }
        juce::Logger::writeToLog (juce::String::formatted (
            "loadSlot: snap restored fp=%.2f gh=%.2f hz=%.2f speed=%.2f order=%d,%d,%d",
            snap.filterPos, snap.ghostAmount, snap.hazeAmount, snap.playbackSpeed,
            snap.signalOrder[0], snap.signalOrder[1], snap.signalOrder[2]));
    }

    // Start playback, but leave looping alone — the user controls that with
    // the LOOP button. (Auto-looping was a hold-over from when slots were
    // assumed to always contain seamless loops; with one-shot samples it
    // turned every slot trigger into an unintended infinite loop.)
    playing.store (true);
    juce::Logger::writeToLog ("loadSlot: playback resumed, done");
    return true;
}

void SpoolAudioProcessor::clearSlot (int slot)
{
    if (slot < 0 || slot >= kNumSlots) return;
    slots[(size_t) slot] = nullptr;
    const auto wavFile = sessionDir.getChildFile ("slot_" + juce::String (slot + 1) + ".wav");
    if (wavFile.exists()) wavFile.deleteFile();
}

//==============================================================================
juce::AudioProcessorEditor* SpoolAudioProcessor::createEditor()
{
    return new SpoolAudioProcessorEditor (*this);
}

//==============================================================================
// State persistence — the DAW calls these when the host project is saved /
// reloaded. We serialize:
//   1. Every atomic knob/button value
//   2. The currently loaded sample's audio + sample-rate + name
//   3. Each of the 8 slots: audio + full SlotSnapshot
//   4. Any active loop region (drag-highlight + knob anchor + nudge offset)
// On load, everything is reconstructed before the next processBlock fires.
//
namespace
{
    constexpr int kStateMagic   = 0x53504F4C; // 'SPOL'
    constexpr int kStateVersion = 3;            // v3: + LO-FI dry/wet knob

    inline void writeAudioBuffer (juce::MemoryOutputStream& s,
                                  const juce::AudioBuffer<float>* buf,
                                  double sampleRate)
    {
        const bool present = (buf != nullptr && buf->getNumSamples() > 0
                              && buf->getNumChannels() > 0);
        s.writeBool (present);
        if (! present) return;
        const int  ch = buf->getNumChannels();
        const int  n  = buf->getNumSamples();
        s.writeInt    (ch);
        s.writeInt    (n);
        s.writeDouble (sampleRate);
        for (int c = 0; c < ch; ++c)
            s.write (buf->getReadPointer (c), (size_t) n * sizeof (float));
    }

    inline bool readAudioBuffer (juce::MemoryInputStream& s,
                                 juce::AudioBuffer<float>& outBuf,
                                 double& outSampleRate)
    {
        const bool present = s.readBool();
        if (! present) { outBuf.setSize (0, 0); outSampleRate = 0.0; return false; }
        const int  ch = s.readInt();
        const int  n  = s.readInt();
        outSampleRate = s.readDouble();
        if (ch <= 0 || ch > 32 || n <= 0 || n > (1 << 28))   // sanity
            return false;
        outBuf.setSize (ch, n, false, true, false);
        for (int c = 0; c < ch; ++c)
            s.read (outBuf.getWritePointer (c), n * sizeof (float));
        return true;
    }
}

void SpoolAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::Logger::writeToLog ("getStateInformation: serializing...");
    juce::MemoryOutputStream out (destData, false);
    out.writeInt (kStateMagic);
    out.writeInt (kStateVersion);

    // ---- Atomic knob/button state -----------------------------------------
    out.writeFloat  (inputMix.load());
    out.writeInt    (inputCompMode.load());
    out.writeFloat  (sampleGain.load());
    out.writeFloat  (playbackSpeed.load());
    out.writeFloat  (filterPos.load());
    out.writeInt    (filterQMode.load());
    out.writeInt    (filterLfoRate.load());
    out.writeInt    (filterLfoRange.load());
    out.writeFloat  (ghostAmount.load());
    out.writeInt    (ghostTimeMode.load());
    out.writeFloat  (hazeAmount.load());
    out.writeBool   (hazeFrozen.load());
    out.writeInt    (hazePreset.load());
    out.writeFloat  (tapeMix.load());
    out.writeInt    (tapeMachine.load());
    out.writeInt    (loopCutoffMode.load());
    out.writeInt    (loopLengthMode.load());
    out.writeFloat  (loopAnchorOffsetSeconds.load());
    out.writeFloat  (lastLoopSizeBeats.load());
    out.writeDouble (internalBpm.load());
    out.writeBool   (overdubEnabled.load());
    out.writeBool   (looping.load());
    for (int i = 0; i < kNumEffects; ++i) out.writeInt (signalPathOrder[i].load());

    // ---- Loop region (drag highlight + knob anchor) -----------------------
    out.writeDouble (customLoopStart.load());
    out.writeDouble (customLoopEnd.load());
    out.writeDouble (loopAnchorBase);

    // ---- Currently loaded sample ------------------------------------------
    Sample::Ptr cur;
    {
        const juce::SpinLock::ScopedLockType lock (sampleLock);
        cur = currentSample;
    }
    if (cur != nullptr)
    {
        out.writeString (cur->name);
        writeAudioBuffer (out, &cur->buffer, cur->sourceSampleRate);
    }
    else
    {
        out.writeString ({});
        writeAudioBuffer (out, nullptr, 0.0);
    }

    // ---- Eight slots: audio + snapshot ------------------------------------
    for (int i = 0; i < kNumSlots; ++i)
    {
        auto& slot = slots[(size_t) i];
        if (slot != nullptr)
        {
            out.writeString (slot->name);
            writeAudioBuffer (out, &slot->buffer, slot->sourceSampleRate);
        }
        else
        {
            out.writeString ({});
            writeAudioBuffer (out, nullptr, 0.0);
        }

        const auto& snap = slotSnapshots[(size_t) i];
        out.writeBool   (snap.valid);
        if (! snap.valid) continue;

        out.writeFloat  (snap.inputMix);
        out.writeInt    (snap.inputCompMode);
        out.writeFloat  (snap.sampleGain);
        out.writeFloat  (snap.playbackSpeed);
        out.writeFloat  (snap.filterPos);
        out.writeInt    (snap.filterQMode);
        out.writeInt    (snap.filterLfoRate);
        out.writeInt    (snap.filterLfoRange);
        out.writeFloat  (snap.ghostAmount);
        out.writeInt    (snap.ghostTimeMode);
        out.writeFloat  (snap.hazeAmount);
        out.writeBool   (snap.hazeFrozen);
        out.writeInt    (snap.hazePreset);
        out.writeFloat  (snap.tapeMix);
        out.writeInt    (snap.tapeMachine);
        out.writeInt    (snap.loopCutoffMode);
        out.writeDouble (snap.bpm);
        for (int k = 0; k < 3; ++k) out.writeInt (snap.signalOrder[k]);
    }

    // v2 trailing fields — kept at the end so v1 readers can stop early.
    out.writeBool (lofiMode.load());
    // v3 — LO-FI dry/wet knob (visible only when LO-FI is engaged).
    out.writeFloat (lofiMix.load());

    juce::Logger::writeToLog (juce::String::formatted (
        "getStateInformation: %d bytes written", (int) destData.getSize()));
}

void SpoolAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    juce::Logger::writeToLog (juce::String::formatted (
        "setStateInformation: %d bytes received", sizeInBytes));
    if (data == nullptr || sizeInBytes < 8) return;

    juce::MemoryInputStream in (data, (size_t) sizeInBytes, false);
    const int magic = in.readInt();
    if (magic != kStateMagic) { juce::Logger::writeToLog ("state: bad magic, abort"); return; }
    const int version = in.readInt();
    if (version > kStateVersion)
    {
        juce::Logger::writeToLog ("state: future version, abort");
        return;
    }

    // ---- Atomic knob/button state -----------------------------------------
    inputMix              .store (in.readFloat());
    inputCompMode         .store (in.readInt());
    sampleGain            .store (in.readFloat());
    playbackSpeed         .store (in.readFloat());
    filterPos             .store (in.readFloat());
    filterQMode           .store (in.readInt());
    filterLfoRate         .store (in.readInt());
    filterLfoRange        .store (in.readInt());
    ghostAmount           .store (in.readFloat());
    ghostTimeMode         .store (in.readInt());
    hazeAmount            .store (in.readFloat());
    hazeFrozen            .store (in.readBool());
    hazePreset            .store (in.readInt());
    tapeMix               .store (in.readFloat());
    tapeMachine           .store (in.readInt());
    loopCutoffMode        .store (in.readInt());
    loopLengthMode        .store (in.readInt());
    loopAnchorOffsetSeconds.store (in.readFloat());
    lastLoopSizeBeats     .store (in.readFloat());
    setBpm                (in.readDouble());     // setter recomputes loop window
    overdubEnabled        .store (in.readBool());
    looping               .store (in.readBool());
    for (int i = 0; i < kNumEffects; ++i) signalPathOrder[i].store (in.readInt());

    // ---- Loop region (drag highlight + knob anchor) -----------------------
    customLoopStart.store (in.readDouble());
    customLoopEnd  .store (in.readDouble());
    loopAnchorBase = in.readDouble();

    // ---- Current sample ----------------------------------------------------
    const juce::String curName = in.readString();
    {
        auto* newSample = new Sample();
        newSample->name = curName;
        if (readAudioBuffer (in, newSample->buffer, newSample->sourceSampleRate))
        {
            Sample::Ptr swapIn (newSample);
            {
                const juce::SpinLock::ScopedLockType lock (sampleLock);
                currentSample = swapIn;
            }
            thumbnail.reset (newSample->buffer.getNumChannels(),
                             newSample->sourceSampleRate,
                             newSample->buffer.getNumSamples());
            thumbnail.addBlock (0, newSample->buffer, 0, newSample->buffer.getNumSamples());
        }
        else
        {
            delete newSample;
            const juce::SpinLock::ScopedLockType lock (sampleLock);
            currentSample = nullptr;
        }
    }

    // ---- Eight slots -------------------------------------------------------
    for (int i = 0; i < kNumSlots; ++i)
    {
        const juce::String slotName = in.readString();
        auto* newSlot = new Sample();
        newSlot->name = slotName;
        if (readAudioBuffer (in, newSlot->buffer, newSlot->sourceSampleRate))
            slots[(size_t) i] = Sample::Ptr (newSlot);
        else
        {
            delete newSlot;
            slots[(size_t) i] = nullptr;
        }

        SlotSnapshot snap;
        snap.valid = in.readBool();
        if (! snap.valid) { slotSnapshots[(size_t) i] = snap; continue; }

        snap.inputMix       = in.readFloat();
        snap.inputCompMode  = in.readInt();
        snap.sampleGain     = in.readFloat();
        snap.playbackSpeed  = in.readFloat();
        snap.filterPos      = in.readFloat();
        snap.filterQMode    = in.readInt();
        snap.filterLfoRate  = in.readInt();
        snap.filterLfoRange = in.readInt();
        snap.ghostAmount    = in.readFloat();
        snap.ghostTimeMode  = in.readInt();
        snap.hazeAmount     = in.readFloat();
        snap.hazeFrozen     = in.readBool();
        snap.hazePreset     = in.readInt();
        snap.tapeMix        = in.readFloat();
        snap.tapeMachine    = in.readInt();
        snap.loopCutoffMode = in.readInt();
        snap.bpm            = in.readDouble();
        for (int k = 0; k < 3; ++k) snap.signalOrder[k] = in.readInt();
        slotSnapshots[(size_t) i] = snap;
    }

    // v2 trailing fields — only present if the file is v2+ AND there's
    // still bytes left to read (defensive against truncated files).
    if (version >= 2 && in.getNumBytesRemaining() >= 1)
        lofiMode.store (in.readBool());
    else
        lofiMode.store (false);
    if (version >= 3 && in.getNumBytesRemaining() >= 4)
        lofiMix.store (juce::jlimit (0.0f, 1.0f, in.readFloat()));
    else
        lofiMix.store (0.8f);

    juce::Logger::writeToLog ("setStateInformation: restored");
}

//==============================================================================
// Set save / load — wraps get/setStateInformation in a single .spoolset file
// so users can move sets between sessions, share them, or auto-restore the
// last standalone session on next launch.
//
juce::File SpoolAudioProcessor::getLastSessionFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("SPOOL").getChildFile ("last-session.spoolset");
}

void SpoolAudioProcessor::resetAllParameters()
{
    juce::Logger::writeToLog ("resetAllParameters: factory defaults");

    // INPUT preamp
    inputMix      .store (0.0f);
    inputCompMode .store (CompVintage);

    // SAMPLE GAIN
    sampleGain.store (1.0f);

    // SPEED
    playbackSpeed.store (1.0f);

    // FILTER + LFO
    filterPos      .store (0.5f);    // 0.5 = bypass
    filterQMode    .store (1);       // MID
    filterLfoRate  .store (0);       // OFF
    filterLfoRange .store (1);       // MD
    filterLfoPhase = 0.0;

    // GHOST
    ghostAmount   .store (0.0f);
    ghostTimeMode .store (1);        // 1/8

    // HAZE
    hazeAmount .store (0.0f);
    hazeFrozen .store (false);
    hazePreset .store (PresetHall);

    // TAPE
    tapeMix     .store (0.0f);
    tapeMachine .store (0);          // SAT

    // LOOP cutoff slope + knob-loop length
    loopCutoffMode.store (0);        // -12
    loopLengthMode.store (0);        // off
    loopAnchorBase = -1.0;
    loopAnchorOffsetSeconds.store (0.0f);
    lastLoopSizeBeats.store (0.0f);

    // Custom drag-highlight loop region
    customLoopStart.store (-1.0);
    customLoopEnd  .store (-1.0);

    // Signal-path order
    signalPathOrder[0].store (EffectFilter);
    signalPathOrder[1].store (EffectGhost);
    signalPathOrder[2].store (EffectHaze);

    // LO-FI master mode off + mix back to default — RESET should always
    // return the device to its clean / hi-fi default character.
    lofiMode.store (false);
    lofiMix.store (0.8f);

    // Tempo back to default 120
    setBpm (120.0);

    // Reset every per-channel DSP state buffer so old reverb tails / delay
    // contents / filter ringing don't bleed through after the reset.
    for (int ch = 0; ch < kMaxFilterChannels; ++ch)
    {
        warmthHpState[ch] = warmthHpPrev[ch] = warmthLpState[ch] = 0.0f;
        compEnv      [ch] = outCompEnv [ch] = 0.0f;
        outLpState   [ch] = outHpState [ch] = outHpPrev[ch] = 0.0f;
        ghostFbLp    [ch] = 0.0f;
        svfLow       [ch] = svfBand   [ch] = 0.0f;
        srrHold      [ch] = 0.0f;
        scratchLpState[ch] = 0.0f;
        shimmerHpState[ch] = shimmerHpPrev[ch] = 0.0f;
        hazePreHpState[ch] = hazePreHpPrev[ch] = 0.0f;
        hazePostLpState[ch] = 0.0f;
    }
    ghostDelayBuf.clear();
    tapeDelayBuf .clear();
    shimmerBuf   .clear();
    hazeReverb.reset();
}

bool SpoolAudioProcessor::saveSetToFile (const juce::File& destination)
{
    juce::Logger::writeToLog ("saveSetToFile: " + destination.getFullPathName());
    juce::MemoryBlock blob;
    getStateInformation (blob);
    destination.getParentDirectory().createDirectory();
    destination.deleteFile();
    return destination.replaceWithData (blob.getData(), blob.getSize());
}

bool SpoolAudioProcessor::loadSetFromFile (const juce::File& source)
{
    juce::Logger::writeToLog ("loadSetFromFile: " + source.getFullPathName());
    if (! source.existsAsFile()) return false;
    juce::MemoryBlock blob;
    if (! source.loadFileAsData (blob)) return false;
    setStateInformation (blob.getData(), (int) blob.getSize());
    return true;
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SpoolAudioProcessor();
}
