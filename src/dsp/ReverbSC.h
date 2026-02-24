// ReverbSC - Sean Costello's 8-delay-line stereo FDN reverb
//
// Original algorithm: Sean Costello & Istvan Varga (Csound reverbsc opcode, 1999/2005)
// C extraction: Paul Batchelor (Soundpipe library)
// C++ wrapper: Breadbin project
//
// Soundpipe license (MIT):
//   Copyright (c) 2020 Paul Batchelor
//   Permission is hereby granted, free of charge, to any person obtaining a copy
//   of this software and associated documentation files (the "Software"), to deal
//   in the Software without restriction, including without limitation the rights
//   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//   copies of the Software, and to permit persons to whom the Software is
//   furnished to do so, subject to the following conditions:
//   The above copyright notice and this permission notice shall be included in
//   all copies or substantial portions of the Software.
//   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND.

#pragma once

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

class ReverbSC {
public:
  ReverbSC() = default;
  ~ReverbSC() { destroy(); }

  // Non-copyable (owns heap memory)
  ReverbSC(const ReverbSC &) = delete;
  ReverbSC &operator=(const ReverbSC &) = delete;

  void init(float sampleRate) {
    destroy();
    rev.sampleRate = sampleRate;
    rev.feedback = 0.97f;
    rev.lpfreq = 10000.0f;
    rev.iPitchMod = 1.0f;
    rev.dampFact = 1.0f;
    rev.prv_LPFreq = 0.0f;
    rev.initDone = 1;

    // Calculate total delay buffer size
    int nBytes = 0;
    for (int i = 0; i < 8; i++)
      nBytes += delayLineBytes(sampleRate, 1.0f, i);

    rev.auxBuf = static_cast<float *>(std::malloc(nBytes));
    if (!rev.auxBuf)
      return;
    std::memset(rev.auxBuf, 0, nBytes);

    // Assign each delay line a slice of the contiguous buffer
    int offset = 0;
    for (int i = 0; i < 8; i++) {
      rev.delayLines[i].buf =
          reinterpret_cast<float *>(reinterpret_cast<char *>(rev.auxBuf) +
                                    offset);
      initDelayLine(i);
      offset += delayLineBytes(sampleRate, 1.0f, i);
    }

    initialized = true;
  }

  void destroy() {
    if (rev.auxBuf) {
      std::free(rev.auxBuf);
      rev.auxBuf = nullptr;
    }
    initialized = false;
    rev.initDone = 0;
  }

  void setFeedback(float fb) { rev.feedback = fb; }
  void setLPFreq(float hz) { rev.lpfreq = hz; }

  void compute(float inL, float inR, float &outL, float &outR) {
    if (!initialized) {
      outL = outR = 0.0f;
      return;
    }

    float dampFact = rev.dampFact;

    // Recalculate tone filter coefficient if frequency changed
    if (rev.lpfreq != rev.prv_LPFreq) {
      rev.prv_LPFreq = rev.lpfreq;
      dampFact = 2.0f - std::cos(rev.prv_LPFreq * (2.0f * kPi) /
                                  rev.sampleRate);
      dampFact = rev.dampFact = dampFact - std::sqrt(dampFact * dampFact -
                                                     1.0f);
    }

    // Calculate "resultant junction pressure" and mix to input signals
    float ainL = 0.0f;
    float aoutL = 0.0f, aoutR = 0.0f;
    for (int n = 0; n < 8; n++)
      ainL += rev.delayLines[n].filterState;

    ainL *= kJpScale;
    float ainR = ainL + inR;
    ainL = ainL + inL;

    // Process all 8 delay lines
    for (int n = 0; n < 8; n++) {
      DelayLine &lp = rev.delayLines[n];
      int bufSize = lp.bufferSize;

      // Send input signal and feedback to delay line
      lp.buf[lp.writePos] = ((n & 1) ? ainR : ainL) - lp.filterState;
      if (++lp.writePos >= bufSize)
        lp.writePos -= bufSize;

      // Read from delay line with cubic interpolation
      if (lp.readPosFrac >= kDelayPosScale) {
        lp.readPos += (lp.readPosFrac >> kDelayPosShift);
        lp.readPosFrac &= kDelayPosMask;
      }
      if (lp.readPos >= bufSize)
        lp.readPos -= bufSize;

      int readPos = lp.readPos;
      float frac = static_cast<float>(lp.readPosFrac) *
                   (1.0f / static_cast<float>(kDelayPosScale));

      // Cubic interpolation coefficients
      float a2 = frac * frac;
      a2 -= 1.0f;
      a2 *= (1.0f / 6.0f);
      float a1 = frac;
      a1 += 1.0f;
      a1 *= 0.5f;
      float am1 = a1 - 1.0f;
      float a0 = 3.0f * a2;
      a1 -= a0;
      am1 -= a2;
      a0 -= frac;

      // Read four samples for interpolation
      float vm1, v0, v1, v2;
      if (readPos > 0 && readPos < (bufSize - 2)) {
        vm1 = lp.buf[readPos - 1];
        v0 = lp.buf[readPos];
        v1 = lp.buf[readPos + 1];
        v2 = lp.buf[readPos + 2];
      } else {
        // Buffer wrap-around: check each index
        if (--readPos < 0)
          readPos += bufSize;
        vm1 = lp.buf[readPos];
        if (++readPos >= bufSize)
          readPos -= bufSize;
        v0 = lp.buf[readPos];
        if (++readPos >= bufSize)
          readPos -= bufSize;
        v1 = lp.buf[readPos];
        if (++readPos >= bufSize)
          readPos -= bufSize;
        v2 = lp.buf[readPos];
      }
      v0 = (am1 * vm1 + a0 * v0 + a1 * v1 + a2 * v2) * frac + v0;

      // Update buffer read position
      lp.readPosFrac += lp.readPosFrac_inc;

      // Apply feedback gain and lowpass filter
      v0 *= rev.feedback;
      v0 = (lp.filterState - v0) * dampFact + v0;
      lp.filterState = v0;

      // Mix to output (even lines → left, odd lines → right)
      if (n & 1)
        aoutR += v0;
      else
        aoutL += v0;

      // Start next random line segment if current one reached endpoint
      if (--(lp.randLine_cnt) <= 0)
        nextRandomLineSeg(n);
    }

    outL = aoutL * kOutputGain;
    outR = aoutR * kOutputGain;
  }

private:
  static constexpr float kPi = 3.14159265358979323846f;
  static constexpr float kOutputGain = 0.35f;
  static constexpr float kJpScale = 0.25f;
  static constexpr float kDefaultSRate = 44100.0f;
  static constexpr int kDelayPosShift = 28;
  static constexpr int kDelayPosScale = 0x10000000;
  static constexpr int kDelayPosMask = 0x0FFFFFFF;

  // Delay time / random variation / random frequency / random seed
  // per delay line — Sean Costello's original tuning
  static constexpr float kParams[8][4] = {
      {2473.0f / kDefaultSRate, 0.0010f, 3.100f, 1966.0f},
      {2767.0f / kDefaultSRate, 0.0011f, 3.500f, 29491.0f},
      {3217.0f / kDefaultSRate, 0.0017f, 1.110f, 22937.0f},
      {3557.0f / kDefaultSRate, 0.0006f, 3.973f, 9830.0f},
      {3907.0f / kDefaultSRate, 0.0010f, 2.341f, 20643.0f},
      {4127.0f / kDefaultSRate, 0.0011f, 1.897f, 22937.0f},
      {2143.0f / kDefaultSRate, 0.0017f, 0.891f, 29491.0f},
      {1933.0f / kDefaultSRate, 0.0006f, 3.221f, 14417.0f}};

  struct DelayLine {
    int writePos = 0;
    int bufferSize = 0;
    int readPos = 0;
    int readPosFrac = 0;
    int readPosFrac_inc = 0;
    int seedVal = 0;
    int randLine_cnt = 0;
    float filterState = 0.0f;
    float *buf = nullptr;
  };

  struct RevState {
    float feedback = 0.97f;
    float lpfreq = 10000.0f;
    float sampleRate = 44100.0f;
    float iPitchMod = 1.0f;
    float dampFact = 1.0f;
    float prv_LPFreq = 0.0f;
    int initDone = 0;
    DelayLine delayLines[8] = {};
    float *auxBuf = nullptr;
  };

  RevState rev;
  bool initialized = false;

  static int delayLineMaxSamples(float sr, float iPitchMod, int n) {
    float maxDel = kParams[n][0] + (kParams[n][1] * iPitchMod * 1.125f);
    return static_cast<int>(maxDel * sr + 16.5f);
  }

  static int delayLineBytes(float sr, float iPitchMod, int n) {
    return delayLineMaxSamples(sr, iPitchMod, n) *
           static_cast<int>(sizeof(float));
  }

  void nextRandomLineSeg(int n) {
    DelayLine &lp = rev.delayLines[n];

    // Update random seed
    if (lp.seedVal < 0)
      lp.seedVal += 0x10000;
    lp.seedVal = (lp.seedVal * 15625 + 1) & 0xFFFF;
    if (lp.seedVal >= 0x8000)
      lp.seedVal -= 0x10000;

    // Length of next segment in samples
    lp.randLine_cnt =
        static_cast<int>((rev.sampleRate / kParams[n][2]) + 0.5f);

    float prvDel = static_cast<float>(lp.writePos);
    prvDel -= (static_cast<float>(lp.readPos) +
               (static_cast<float>(lp.readPosFrac) /
                static_cast<float>(kDelayPosScale)));
    while (prvDel < 0.0f)
      prvDel += lp.bufferSize;
    prvDel = prvDel / rev.sampleRate;

    float nxtDel =
        static_cast<float>(lp.seedVal) * kParams[n][1] / 32768.0f;
    nxtDel = kParams[n][0] + (nxtDel * rev.iPitchMod);

    float phs_incVal = (prvDel - nxtDel) / static_cast<float>(lp.randLine_cnt);
    phs_incVal = phs_incVal * rev.sampleRate + 1.0f;
    lp.readPosFrac_inc =
        static_cast<int>(phs_incVal * kDelayPosScale + 0.5f);
  }

  void initDelayLine(int n) {
    DelayLine &lp = rev.delayLines[n];
    lp.bufferSize = delayLineMaxSamples(rev.sampleRate, 1.0f, n);
    lp.writePos = 0;
    lp.seedVal = static_cast<int>(kParams[n][3] + 0.5f);

    float readPos =
        static_cast<float>(lp.seedVal) * kParams[n][1] / 32768.0f;
    readPos = kParams[n][0] + (readPos * rev.iPitchMod);
    readPos =
        static_cast<float>(lp.bufferSize) - (readPos * rev.sampleRate);
    lp.readPos = static_cast<int>(readPos);
    float fracPart =
        (readPos - static_cast<float>(lp.readPos)) *
        static_cast<float>(kDelayPosScale);
    lp.readPosFrac = static_cast<int>(fracPart + 0.5f);

    nextRandomLineSeg(n);
    lp.filterState = 0.0f;
    std::memset(lp.buf, 0, sizeof(float) * lp.bufferSize);
  }
};
