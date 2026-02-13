#pragma once

#include <string>
#include <memory>
#include <atomic>
#include <thread>
#include <array>
#include <cstdint>

class sidplayfp;
class SidTune;
class ReSIDfpBuilder;

/**
 * SidFilePlayer - Wrapper around libsidplayfp for playing .SID files.
 *
 * Architecture:
 *   - Background thread calls engine->play() in a loop, writing 16-bit samples
 *     into a lock-free ring buffer.
 *   - Audio thread calls readSamples() to drain the ring buffer into processBlock output.
 *   - Register snapshots are double-buffered for lock-free UI reads.
 */
class SidFilePlayer
{
public:
    SidFilePlayer();
    ~SidFilePlayer();

    // File management
    bool loadFile(const std::string& filePath);
    void unload();
    bool isLoaded() const;

    // Prepare for playback at given host sample rate
    void prepare(double sampleRate);

    // Internal engine rate (fixed for stable playback)
    static constexpr double ENGINE_SAMPLE_RATE = 44100.0;
    double getEngineSampleRate() const { return ENGINE_SAMPLE_RATE; }

    // Transport
    void play();
    void stop();
    void pause();
    bool isPlaying() const;
    bool isPaused() const;

    // Tune info
    int getNumSubtunes() const;
    int getCurrentSubtune() const;
    void selectSubtune(int subtune);
    std::string getTitle() const;
    std::string getAuthor() const;
    std::string getReleased() const;
    uint32_t getPlayTimeMs() const;

    // Volume (0-1, for mixing with synth output)
    void setVolume(float vol);
    float getVolume() const;

    // Audio: pull samples into buffer (called from audio thread)
    // Returns number of samples actually read
    int readSamples(float* leftOut, float* rightOut, int numSamples);

    // Register snapshot for UI visualization
    struct RegisterSnapshot
    {
        uint8_t regs[32] = {};
        bool valid = false;
    };
    RegisterSnapshot getRegisterSnapshot() const;

private:
    void playerThreadFunc();
    void stopThread();

    // Engine objects
    std::unique_ptr<sidplayfp> engine;
    std::unique_ptr<SidTune> tune;
    std::unique_ptr<ReSIDfpBuilder> builder;

    // State
    double currentSampleRate = 44100.0;
    std::atomic<float> volume{0.7f};
    std::atomic<bool> loaded{false};
    std::atomic<bool> playing{false};
    std::atomic<bool> paused{false};
    std::atomic<bool> threadShouldExit{false};

    // Background player thread
    std::thread playerThread;

    // Lock-free ring buffer for audio transfer
    static constexpr int RING_BUFFER_SIZE = 16384;
    std::array<float, RING_BUFFER_SIZE> ringBufferL{};
    std::array<float, RING_BUFFER_SIZE> ringBufferR{};
    std::atomic<int> writePos{0};
    std::atomic<int> readPos{0};

    // Double-buffered register snapshots
    std::array<RegisterSnapshot, 2> snapshots{};
    std::atomic<int> snapshotWriteIndex{0};
    std::atomic<int> snapshotReadIndex{0};
};
