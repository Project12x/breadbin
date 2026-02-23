#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

// 4-bit digi sample player for authentic C64 $D418 volume register playback.
// Samples are quantized to 16 levels (0-15), packed 2 per byte.
// Pitch-tracks to MIDI notes via a configurable root note.
//
// Usage:
//   GUI thread:  loadFromFile(path)
//   Audio thread: noteOn(midiNote, hostSR), getNextSample(), noteOff()
//
// The caller writes the returned 4-bit value to SID register $D418 via
// SIDEngine::writeVolumeRegister() before each clock() call.
class DigiSampler {
public:
  DigiSampler() = default;

  // ---- GUI thread only ----

  bool loadFromFile(const std::string &path) {
    juce::File file{juce::String{path}};
    if (!file.existsAsFile())
      return false;

    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader(
        formatManager.createReaderFor(file));
    if (!reader)
      return false;

    // Cap at 5 seconds to prevent excessive memory use
    int totalSamples = static_cast<int>(reader->lengthInSamples);
    int maxSamples = static_cast<int>(reader->sampleRate * 5.0);
    totalSamples = std::min(totalSamples, maxSamples);

    // Read to mono float buffer (sums L+R when both flags are true)
    juce::AudioBuffer<float> buffer(1, totalSamples);
    reader->read(&buffer, 0, totalSamples, 0, true, true);

    const float *mono = buffer.getReadPointer(0);

    // Normalize to peak
    float peak = 0.0f;
    for (int i = 0; i < totalSamples; ++i)
      peak = std::max(peak, std::abs(mono[i]));
    float scale = (peak > 0.0001f) ? (1.0f / peak) : 1.0f;

    // Quantize to 4-bit (0-15) and pack: high nibble = even, low nibble = odd
    int packedSize = (totalSamples + 1) / 2;
    std::vector<uint8_t> newData(static_cast<size_t>(packedSize), 0);

    for (int i = 0; i < totalSamples; ++i) {
      float normalized = mono[i] * scale;
      int quantized =
          static_cast<int>((normalized + 1.0f) * 7.5f + 0.5f); // [-1,+1]->[0,15]
      quantized = std::clamp(quantized, 0, 15);

      int byteIdx = i / 2;
      if (i % 2 == 0)
        newData[static_cast<size_t>(byteIdx)] |=
            static_cast<uint8_t>(quantized << 4);
      else
        newData[static_cast<size_t>(byteIdx)] |=
            static_cast<uint8_t>(quantized);
    }

    // Commit atomically (audio thread reads only when loaded && playing)
    sampleData = std::move(newData);
    numSamples = totalSamples;
    sourceSampleRate = reader->sampleRate;
    filePath = path;
    loaded = true;
    playing = false;
    return true;
  }

  void unload() {
    playing = false;
    loaded = false;
    sampleData.clear();
    numSamples = 0;
    sourceSampleRate = 44100.0;
    filePath.clear();
  }

  bool isLoaded() const { return loaded; }
  int getNumSamples() const { return numSamples; }
  double getSourceSampleRate() const { return sourceSampleRate; }
  const std::string &getFilePath() const { return filePath; }

  // ---- Audio thread ----

  void noteOn(int midiNote, double hostSR) {
    if (!loaded || numSamples == 0)
      return;
    double pitchRatio = std::pow(2.0, (midiNote - rootNote) / 12.0);
    increment = (sourceSampleRate / hostSR) * pitchRatio;
    position = 0.0;
    playing = true;
  }

  void noteOff() { playing = false; }

  // Returns next 4-bit sample (0-15), or -1 if not playing.
  int getNextSample() {
    if (!playing || numSamples == 0)
      return -1;

    int idx = static_cast<int>(position);
    if (idx >= numSamples) {
      if (looping) {
        position = std::fmod(position, static_cast<double>(numSamples));
        idx = static_cast<int>(position);
      } else {
        playing = false;
        return -1;
      }
    }

    // Linear interpolation between adjacent 4-bit samples
    uint8_t s0 = getSample4bit(idx);
    uint8_t s1 = getSample4bit(std::min(idx + 1, numSamples - 1));
    double frac = position - static_cast<double>(idx);
    int interpolated = static_cast<int>(
        s0 + frac * (static_cast<double>(s1) - static_cast<double>(s0)) + 0.5);
    interpolated = std::clamp(interpolated, 0, 15);

    position += increment;
    return interpolated;
  }

  bool isPlaying() const { return playing; }

  // ---- Configuration ----

  void setRootNote(int note) { rootNote = std::clamp(note, 0, 127); }
  int getRootNote() const { return rootNote; }
  void setLooping(bool loop) { looping = loop; }
  bool isLooping() const { return looping; }

  // ---- State serialization ----

  const std::vector<uint8_t> &getPackedData() const { return sampleData; }

  void setPackedData(const std::vector<uint8_t> &data, int count, double sr,
                     const std::string &path) {
    sampleData = data;
    numSamples = count;
    sourceSampleRate = sr;
    filePath = path;
    loaded = (numSamples > 0);
    playing = false;
  }

private:
  // Packed 4-bit samples: high nibble = even index, low nibble = odd index
  std::vector<uint8_t> sampleData;
  int numSamples = 0;
  double sourceSampleRate = 44100.0;
  int rootNote = 60; // C4
  std::string filePath;

  // Playback state (audio thread only)
  double position = 0.0;
  double increment = 1.0;
  bool playing = false;
  bool looping = false;
  bool loaded = false;

  uint8_t getSample4bit(int index) const {
    if (index < 0 || index >= numSamples)
      return 8; // DC midpoint
    int byteIdx = index / 2;
    if (index % 2 == 0)
      return (sampleData[static_cast<size_t>(byteIdx)] >> 4) & 0x0F;
    else
      return sampleData[static_cast<size_t>(byteIdx)] & 0x0F;
  }
};
