#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

// Digi sample player for C64-style playback with two modes:
//   4-bit: Authentic $D418 volume register playback (0-15), packed 2 per byte
//   8-bit: Higher quality direct mix (0-255), 1 byte per sample
// Pitch-tracks to MIDI notes via a configurable root note.
//
// Usage:
//   GUI thread:  loadFromFile(path)
//   Audio thread: noteOn(midiNote, hostSR), getNextSample(), noteOff()
//
// In 4-bit mode, the caller writes the returned value to SID register $D418.
// In 8-bit mode, the caller mixes the returned value directly into output.
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

    // Store both 4-bit packed and 8-bit unpacked representations
    int packedSize = (totalSamples + 1) / 2;
    std::vector<uint8_t> newData4(static_cast<size_t>(packedSize), 0);
    std::vector<uint8_t> newData8(static_cast<size_t>(totalSamples), 0);

    for (int i = 0; i < totalSamples; ++i) {
      float normalized = mono[i] * scale;

      // 8-bit: [-1,+1] -> [0,255]
      int q8 = static_cast<int>((normalized + 1.0f) * 127.5f + 0.5f);
      q8 = std::clamp(q8, 0, 255);
      newData8[static_cast<size_t>(i)] = static_cast<uint8_t>(q8);

      // 4-bit: [-1,+1] -> [0,15], packed 2 per byte
      int q4 = static_cast<int>((normalized + 1.0f) * 7.5f + 0.5f);
      q4 = std::clamp(q4, 0, 15);
      int byteIdx = i / 2;
      if (i % 2 == 0)
        newData4[static_cast<size_t>(byteIdx)] |=
            static_cast<uint8_t>(q4 << 4);
      else
        newData4[static_cast<size_t>(byteIdx)] |=
            static_cast<uint8_t>(q4);
    }

    // Commit atomically (audio thread reads only when loaded && playing)
    sampleData = std::move(newData4);
    sampleData8bit = std::move(newData8);
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
    sampleData8bit.clear();
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

  // Returns next sample value, or -1 if not playing.
  // 4-bit mode: returns 0-15, 8-bit mode: returns 0-255.
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

    double frac = position - static_cast<double>(idx);
    int interpolated;

    if (bitDepth == 8) {
      // 8-bit: interpolate from unpacked buffer
      uint8_t s0 = getSample8bit(idx);
      uint8_t s1 = getSample8bit(std::min(idx + 1, numSamples - 1));
      interpolated = static_cast<int>(
          s0 + frac * (static_cast<double>(s1) - static_cast<double>(s0)) + 0.5);
      interpolated = std::clamp(interpolated, 0, 255);
    } else {
      // 4-bit: interpolate from packed nibble buffer
      uint8_t s0 = getSample4bit(idx);
      uint8_t s1 = getSample4bit(std::min(idx + 1, numSamples - 1));
      interpolated = static_cast<int>(
          s0 + frac * (static_cast<double>(s1) - static_cast<double>(s0)) + 0.5);
      interpolated = std::clamp(interpolated, 0, 15);
    }

    position += increment;
    return interpolated;
  }

  bool isPlaying() const { return playing; }

  // ---- Configuration ----

  void setRootNote(int note) { rootNote = std::clamp(note, 0, 127); }
  int getRootNote() const { return rootNote; }
  void setLooping(bool loop) { looping = loop; }
  bool isLooping() const { return looping; }
  void setBitDepth(int depth) { bitDepth = (depth == 8) ? 8 : 4; }
  int getBitDepth() const { return bitDepth; }

  // ---- State serialization ----

  const std::vector<uint8_t> &getPackedData() const { return sampleData; }
  const std::vector<uint8_t> &getData8bit() const { return sampleData8bit; }

  void setPackedData(const std::vector<uint8_t> &data, int count, double sr,
                     const std::string &path) {
    sampleData = data;
    numSamples = count;
    sourceSampleRate = sr;
    filePath = path;
    loaded = (numSamples > 0);
    playing = false;
    // Regenerate 8-bit from 4-bit if not provided separately
    sampleData8bit.resize(static_cast<size_t>(numSamples));
    for (int i = 0; i < numSamples; ++i)
      sampleData8bit[static_cast<size_t>(i)] =
          static_cast<uint8_t>(getSample4bit(i) * 17); // 0-15 -> 0-255
  }

  void setData8bit(const std::vector<uint8_t> &data, int count, double sr,
                   const std::string &path) {
    sampleData8bit = data;
    numSamples = count;
    sourceSampleRate = sr;
    filePath = path;
    loaded = (numSamples > 0);
    playing = false;
    // Regenerate 4-bit packed from 8-bit
    int packedSize = (numSamples + 1) / 2;
    sampleData.assign(static_cast<size_t>(packedSize), 0);
    for (int i = 0; i < numSamples; ++i) {
      int q4 = data[static_cast<size_t>(i)] / 17; // 0-255 -> 0-15
      int byteIdx = i / 2;
      if (i % 2 == 0)
        sampleData[static_cast<size_t>(byteIdx)] |=
            static_cast<uint8_t>(q4 << 4);
      else
        sampleData[static_cast<size_t>(byteIdx)] |=
            static_cast<uint8_t>(q4);
    }
  }

private:
  // Packed 4-bit samples: high nibble = even index, low nibble = odd index
  std::vector<uint8_t> sampleData;
  // Unpacked 8-bit samples: 1 byte per sample (0-255)
  std::vector<uint8_t> sampleData8bit;
  int numSamples = 0;
  double sourceSampleRate = 44100.0;
  int rootNote = 60; // C4
  int bitDepth = 4;  // 4 or 8
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

  uint8_t getSample8bit(int index) const {
    if (index < 0 || index >= numSamples)
      return 128; // DC midpoint
    return sampleData8bit[static_cast<size_t>(index)];
  }
};
