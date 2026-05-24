// =============================================================================
// DriftLink — lock-free shared-memory audio IPC for connecting standalones.
//
// The first standalone to open() creates a named Windows file mapping; the
// second sees the existing one and attaches. A single-producer / single-
// consumer ring buffer in that mapping carries stereo float audio between
// them with no kernel-mode driver and effectively zero added latency (~one
// audio block at the host's block size).
//
// Layout:
//   [Header (atomic counters + sample-rate negotiation)]
//   [Interleaved float ring — kRingFrames * kMaxChannels samples]
//
// Producer (SP·L): at the end of processBlock, push the just-rendered output
// buffer into the ring. Heartbeat ticks each block; consumer detects "dead
// producer" when heartbeat ages past kStaleMs and falls back to silence.
//
// Consumer (DF-T): at the start of processBlock, if LINK is on and the
// producer is alive, replace the input buffer with audio pulled from the ring.
//
// Cross-process safety: both processes are MSVC + C++17, so std::atomic<T>
// on shared memory is well-defined in practice. We use release/acquire
// ordering for the index updates. Sample-rate mismatch is surfaced via
// Consumer::getProducerSampleRate() so the UI can warn.
//
// Windows-only for v1. macOS/Linux ports use shm_open + mmap (drop-in).
// =============================================================================
#pragma once

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <cstdint>
#include <cstring>

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#endif

namespace DriftLink
{
    constexpr uint32_t kMagic        = 0x4B4E4C44;       // 'DLNK' little-endian
    constexpr uint32_t kVersion      = 1;
    constexpr int      kMaxChannels  = 2;
    constexpr int      kRingFrames   = 8192;             // ~170 ms at 48k
    constexpr uint64_t kStaleMs      = 250;              // producer is "dead" past this
    constexpr const char* kSegmentName = "Local\\DriftLink_v1";

    struct alignas (64) Header
    {
        uint32_t magic;
        uint32_t version;
        uint32_t numChannels;
        uint32_t reserved0;
        double   sampleRate;

        // SPSC counters (total frames; ring index = counter % kRingFrames).
        std::atomic<uint64_t> writeFrame;
        std::atomic<uint64_t> readFrame;
        std::atomic<uint64_t> producerHeartbeatMs;
        std::atomic<uint64_t> consumerHeartbeatMs;
        std::atomic<uint32_t> producerActive;
        std::atomic<uint32_t> consumerActive;
    };

    struct Segment
    {
        Header header;
        float  ring [kRingFrames * kMaxChannels];
    };

    inline uint64_t nowMs()
    {
        return (uint64_t) juce::Time::getMillisecondCounterHiRes();
    }

    //==========================================================================
    // Connection — open / map / close the named shared segment. Both Producer
    // and Consumer wrap a Connection.
    class Connection
    {
    public:
        Connection() = default;
        ~Connection() { close(); }

        bool open()
        {
           #if JUCE_WINDOWS
            handle = CreateFileMappingA (INVALID_HANDLE_VALUE, nullptr,
                                         PAGE_READWRITE, 0,
                                         (DWORD) sizeof (Segment), kSegmentName);
            if (handle == nullptr) return false;
            mapped = (Segment*) MapViewOfFile (handle, FILE_MAP_ALL_ACCESS, 0, 0,
                                               sizeof (Segment));
            if (mapped == nullptr)
            {
                CloseHandle (handle);
                handle = nullptr;
                return false;
            }
            return true;
           #else
            return false;
           #endif
        }

        void close()
        {
           #if JUCE_WINDOWS
            if (mapped != nullptr)
            {
                UnmapViewOfFile (mapped);
                mapped = nullptr;
            }
            if (handle != nullptr)
            {
                CloseHandle (handle);
                handle = nullptr;
            }
           #endif
        }

        bool isOpen() const { return mapped != nullptr; }
        Segment*       getSegment()       { return mapped; }
        const Segment* getSegment() const { return mapped; }

    private:
       #if JUCE_WINDOWS
        HANDLE handle = nullptr;
       #endif
        Segment* mapped = nullptr;

        JUCE_DECLARE_NON_COPYABLE (Connection)
    };

    //==========================================================================
    // Producer — writes audio into the ring (call from processBlock's tail).
    class Producer
    {
    public:
        bool open()  { return conn.open(); }
        void close() { conn.close(); }
        bool isOpen() const { return conn.isOpen(); }

        // Stamp our sample rate + channel count and reset the ring. Call from
        // prepareToPlay or whenever the LINK toggle is flipped on.
        void prepare (double sampleRate, int channels)
        {
            if (auto* seg = conn.getSegment())
            {
                seg->header.magic       = kMagic;
                seg->header.version     = kVersion;
                seg->header.numChannels = (uint32_t) juce::jlimit (1, kMaxChannels, channels);
                seg->header.sampleRate  = sampleRate;
                seg->header.writeFrame.store (0, std::memory_order_relaxed);
                seg->header.readFrame .store (0, std::memory_order_relaxed);
                seg->header.producerActive.store (1, std::memory_order_relaxed);
            }
        }

        void shutdown()
        {
            if (auto* seg = conn.getSegment())
                seg->header.producerActive.store (0, std::memory_order_relaxed);
        }

        // Push a stereo (or mono) block into the ring. On overrun (consumer
        // hasn't drained fast enough) we advance readFrame ourselves to keep
        // streaming — a brief glitch beats permanent jam.
        void write (const juce::AudioBuffer<float>& buffer, int numChannelsToWrite)
        {
            auto* seg = conn.getSegment();
            if (seg == nullptr) return;

            const int numFrames = buffer.getNumSamples();
            const int chans = juce::jmin (numChannelsToWrite,
                                          (int) seg->header.numChannels,
                                          kMaxChannels);

            const auto wf = seg->header.writeFrame.load (std::memory_order_relaxed);
            const auto rf = seg->header.readFrame .load (std::memory_order_acquire);

            const uint64_t used  = wf - rf;
            const uint64_t cap   = (uint64_t) kRingFrames;
            if (used + (uint64_t) numFrames > cap)
            {
                // Overrun — bump readFrame so writeFrame stays cap-1 ahead.
                const uint64_t newRf = (wf + (uint64_t) numFrames) > cap
                                     ? (wf + (uint64_t) numFrames) - cap
                                     : 0;
                seg->header.readFrame.store (newRf, std::memory_order_release);
            }

            for (int i = 0; i < numFrames; ++i)
            {
                const uint64_t idx = ((wf + (uint64_t) i) % cap) * (uint64_t) kMaxChannels;
                for (int c = 0; c < chans; ++c)
                    seg->ring [idx + (uint64_t) c] = buffer.getSample (c, i);
                for (int c = chans; c < kMaxChannels; ++c)
                    seg->ring [idx + (uint64_t) c] = 0.0f;
            }

            seg->header.writeFrame.store (wf + (uint64_t) numFrames,
                                          std::memory_order_release);
            seg->header.producerHeartbeatMs.store (nowMs(), std::memory_order_relaxed);
        }

        // Liveness check for UI ("LINKED" vs "WAITING").
        bool isConsumerAlive() const
        {
            if (auto* seg = conn.getSegment())
            {
                const auto hb = seg->header.consumerHeartbeatMs.load (std::memory_order_relaxed);
                return seg->header.consumerActive.load (std::memory_order_relaxed) != 0
                    && nowMs() < hb + kStaleMs;
            }
            return false;
        }

    private:
        Connection conn;
    };

    //==========================================================================
    // Consumer — pulls audio out of the ring (call from processBlock's head).
    class Consumer
    {
    public:
        bool open()  { return conn.open(); }
        void close() { conn.close(); }
        bool isOpen() const { return conn.isOpen(); }

        // Sync to the producer's current write position so we don't replay
        // whatever audio is sitting in the ring from before we came online.
        void prepare()
        {
            if (auto* seg = conn.getSegment())
            {
                const auto wf = seg->header.writeFrame.load (std::memory_order_acquire);
                seg->header.readFrame.store (wf, std::memory_order_release);
                seg->header.consumerActive.store (1, std::memory_order_relaxed);
            }
        }

        void shutdown()
        {
            if (auto* seg = conn.getSegment())
                seg->header.consumerActive.store (0, std::memory_order_relaxed);
        }

        // Try to fill `numChannelsToFill` channels of `buffer` from the ring.
        // Returns the number of frames actually filled — any tail past that
        // is left untouched (caller may zero it). When the producer's heart-
        // beat is stale, we resync readFrame to writeFrame so we don't burst
        // 170ms of old audio when the producer comes back online.
        int read (juce::AudioBuffer<float>& buffer, int numChannelsToFill)
        {
            auto* seg = conn.getSegment();
            if (seg == nullptr || seg->header.magic != kMagic) return 0;

            const auto now = nowMs();
            const auto producerHb = seg->header.producerHeartbeatMs.load (std::memory_order_relaxed);
            seg->header.consumerHeartbeatMs.store (now, std::memory_order_relaxed);

            const bool producerAlive = seg->header.producerActive.load (std::memory_order_relaxed) != 0
                                    && now < producerHb + kStaleMs;
            if (! producerAlive)
            {
                // Producer's gone quiet — fast-forward read pointer so we don't
                // play 170ms of stale samples when they come back online.
                const auto wf = seg->header.writeFrame.load (std::memory_order_acquire);
                seg->header.readFrame.store (wf, std::memory_order_release);
                return 0;
            }

            const int numFrames = buffer.getNumSamples();
            const int chans     = juce::jmin (numChannelsToFill,
                                              (int) seg->header.numChannels,
                                              kMaxChannels);

            const auto rf = seg->header.readFrame .load (std::memory_order_relaxed);
            const auto wf = seg->header.writeFrame.load (std::memory_order_acquire);
            const uint64_t avail  = wf - rf;
            const uint64_t toRead = juce::jmin ((uint64_t) numFrames, avail);
            const uint64_t cap    = (uint64_t) kRingFrames;

            for (uint64_t i = 0; i < toRead; ++i)
            {
                const uint64_t idx = ((rf + i) % cap) * (uint64_t) kMaxChannels;
                for (int c = 0; c < chans; ++c)
                    buffer.setSample (c, (int) i, seg->ring [idx + (uint64_t) c]);
                for (int c = chans; c < numChannelsToFill; ++c)
                    buffer.setSample (c, (int) i, 0.0f);
            }

            seg->header.readFrame.store (rf + toRead, std::memory_order_release);
            return (int) toRead;
        }

        // Status helpers for the UI status LED.
        bool isProducerAlive() const
        {
            if (auto* seg = conn.getSegment())
            {
                const auto hb = seg->header.producerHeartbeatMs.load (std::memory_order_relaxed);
                return seg->header.producerActive.load (std::memory_order_relaxed) != 0
                    && nowMs() < hb + kStaleMs;
            }
            return false;
        }

        double getProducerSampleRate() const
        {
            if (auto* seg = conn.getSegment())
                return seg->header.sampleRate;
            return 0.0;
        }

    private:
        Connection conn;
    };
}
