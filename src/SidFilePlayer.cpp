#include "SidFilePlayer.h"

#include "sidplayfp/sidplayfp.h"
#include "sidplayfp/SidTune.h"
#include "sidplayfp/SidTuneInfo.h"
#include "sidplayfp/SidConfig.h"
#include "residfp.h"

#include <algorithm>
#include <cstring>
#include <cmath>

// Number of stereo frames to produce per player thread iteration
static constexpr int CHUNK_FRAMES = 1024;

SidFilePlayer::SidFilePlayer()
{
    engine = std::make_unique<sidplayfp>();
    builder = std::make_unique<ReSIDfpBuilder>("Breadbin Player");

    // Create SID emulations (up to 3 for multi-SID tunes)
    builder->create(3);
    builder->filter(true);
}

SidFilePlayer::~SidFilePlayer()
{
    stopThread();
    tune.reset();
    builder.reset();
    engine.reset();
}

bool SidFilePlayer::loadFile(const std::string& filePath)
{
    // Stop any current playback
    stop();

    tune = std::make_unique<SidTune>(filePath.c_str());
    if (!tune->getStatus())
    {
        tune.reset();
        loaded.store(false);
        return false;
    }

    // Select default sub-tune
    tune->selectSong(0);

    // Configure engine at fixed internal rate (resampling to host rate done externally)
    SidConfig cfg;
    cfg.frequency = static_cast<uint_least32_t>(ENGINE_SAMPLE_RATE);
    cfg.playback = SidConfig::STEREO;
    cfg.sidEmulation = builder.get();
    cfg.defaultSidModel = SidConfig::MOS6581;
    cfg.defaultC64Model = SidConfig::PAL;
    cfg.samplingMethod = SidConfig::INTERPOLATE;

    if (!engine->load(tune.get()))
    {
        tune.reset();
        loaded.store(false);
        return false;
    }

    if (!engine->config(cfg))
    {
        tune.reset();
        loaded.store(false);
        return false;
    }

    loaded.store(true);
    return true;
}

void SidFilePlayer::unload()
{
    stop();
    if (engine)
        engine->load(nullptr);
    tune.reset();
    loaded.store(false);
}

bool SidFilePlayer::isLoaded() const
{
    return loaded.load();
}

void SidFilePlayer::prepare(double sampleRate)
{
    currentSampleRate = sampleRate;
    // Engine always runs at ENGINE_SAMPLE_RATE; resampling done by caller
}

void SidFilePlayer::play()
{
    if (!loaded.load())
        return;

    if (paused.load())
    {
        paused.store(false);
        return;
    }

    // Stop any existing thread before starting a new one
    stopThread();

    // Reset ring buffer positions
    writePos.store(0);
    readPos.store(0);

    playing.store(true);
    paused.store(false);
    threadShouldExit.store(false);

    playerThread = std::thread(&SidFilePlayer::playerThreadFunc, this);
}

void SidFilePlayer::stop()
{
    stopThread();
    playing.store(false);
    paused.store(false);
}

void SidFilePlayer::pause()
{
    if (playing.load())
        paused.store(true);
}

bool SidFilePlayer::isPlaying() const
{
    return playing.load() && !paused.load();
}

bool SidFilePlayer::isPaused() const
{
    return playing.load() && paused.load();
}

int SidFilePlayer::getNumSubtunes() const
{
    if (!tune)
        return 0;
    const SidTuneInfo* info = tune->getInfo();
    return info ? static_cast<int>(info->songs()) : 0;
}

int SidFilePlayer::getCurrentSubtune() const
{
    if (!tune)
        return 0;
    const SidTuneInfo* info = tune->getInfo();
    return info ? static_cast<int>(info->currentSong()) : 0;
}

void SidFilePlayer::selectSubtune(int subtune)
{
    if (!tune)
        return;

    bool wasPlaying = isPlaying();
    if (wasPlaying)
        stop();

    tune->selectSong(static_cast<unsigned int>(subtune));

    // Reload and reconfigure at fixed internal rate
    if (engine)
    {
        engine->load(tune.get());
        SidConfig cfg = engine->config();
        cfg.frequency = static_cast<uint_least32_t>(ENGINE_SAMPLE_RATE);
        cfg.playback = SidConfig::STEREO;
        cfg.sidEmulation = builder.get();
        engine->config(cfg);
    }

    if (wasPlaying)
        play();
}

std::string SidFilePlayer::getTitle() const
{
    if (!tune) return {};
    const SidTuneInfo* info = tune->getInfo();
    if (!info || info->numberOfInfoStrings() < 1) return {};
    return info->infoString(0);
}

std::string SidFilePlayer::getAuthor() const
{
    if (!tune) return {};
    const SidTuneInfo* info = tune->getInfo();
    if (!info || info->numberOfInfoStrings() < 2) return {};
    return info->infoString(1);
}

std::string SidFilePlayer::getReleased() const
{
    if (!tune) return {};
    const SidTuneInfo* info = tune->getInfo();
    if (!info || info->numberOfInfoStrings() < 3) return {};
    return info->infoString(2);
}

uint32_t SidFilePlayer::getPlayTimeMs() const
{
    if (!engine) return 0;
    return engine->timeMs();
}

void SidFilePlayer::setVolume(float vol)
{
    volume.store(std::clamp(vol, 0.0f, 1.0f));
}

float SidFilePlayer::getVolume() const
{
    return volume.load();
}

int SidFilePlayer::readSamples(float* leftOut, float* rightOut, int numSamples)
{
    int rp = readPos.load(std::memory_order_relaxed);
    int wp = writePos.load(std::memory_order_acquire);

    int available = (wp - rp + RING_BUFFER_SIZE) % RING_BUFFER_SIZE;
    int toRead = std::min(numSamples, available);

    float vol = volume.load(std::memory_order_relaxed);

    for (int i = 0; i < toRead; ++i)
    {
        int idx = (rp + i) % RING_BUFFER_SIZE;
        leftOut[i] = ringBufferL[idx] * vol;
        rightOut[i] = ringBufferR[idx] * vol;
    }

    // Zero-fill remainder if ring buffer didn't have enough
    for (int i = toRead; i < numSamples; ++i)
    {
        leftOut[i] = 0.0f;
        rightOut[i] = 0.0f;
    }

    readPos.store((rp + toRead) % RING_BUFFER_SIZE, std::memory_order_release);
    return toRead;
}

SidFilePlayer::RegisterSnapshot SidFilePlayer::getRegisterSnapshot() const
{
    int ri = snapshotReadIndex.load(std::memory_order_acquire);
    return snapshots[ri];
}

void SidFilePlayer::playerThreadFunc()
{
    // Stereo interleaved buffer: L, R, L, R, ...
    std::vector<short> rawBuffer(static_cast<size_t>(CHUNK_FRAMES) * 2);

    while (!threadShouldExit.load(std::memory_order_relaxed))
    {
        if (paused.load(std::memory_order_relaxed))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        // Check buffer fill level before producing
        int wp = writePos.load(std::memory_order_relaxed);
        int rp = readPos.load(std::memory_order_acquire);
        int avail = (wp - rp + RING_BUFFER_SIZE) % RING_BUFFER_SIZE;

        // If buffer is more than 75% full, sleep and re-check
        if (avail > (RING_BUFFER_SIZE * 3) / 4)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        // Produce audio: count = total 16-bit samples (stereo interleaved)
        uint_least32_t produced = engine->play(rawBuffer.data(),
                                               static_cast<uint_least32_t>(rawBuffer.size()));

        if (produced == 0)
        {
            // Tune ended or error
            playing.store(false);
            break;
        }

        // Convert interleaved 16-bit stereo to float and write to ring buffer
        int stereoFrames = static_cast<int>(produced) / 2;
        wp = writePos.load(std::memory_order_relaxed);
        rp = readPos.load(std::memory_order_acquire);

        for (int i = 0; i < stereoFrames; ++i)
        {
            int nextWp = (wp + 1) % RING_BUFFER_SIZE;
            if (nextWp == rp)
                break;

            ringBufferL[wp] = static_cast<float>(rawBuffer[i * 2]) / 32768.0f;
            ringBufferR[wp] = static_cast<float>(rawBuffer[i * 2 + 1]) / 32768.0f;
            wp = nextWp;
        }
        writePos.store(wp, std::memory_order_release);

        // Update register snapshot (double-buffered)
        int wi = 1 - snapshotReadIndex.load(std::memory_order_relaxed);
        if (engine->getSidStatus(0, snapshots[wi].regs))
            snapshots[wi].valid = true;
        else
            snapshots[wi].valid = false;
        snapshotWriteIndex.store(wi, std::memory_order_relaxed);
        snapshotReadIndex.store(wi, std::memory_order_release);

        // Brief yield to avoid CPU spinning
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void SidFilePlayer::stopThread()
{
    threadShouldExit.store(true);
    if (playerThread.joinable())
        playerThread.join();
}
