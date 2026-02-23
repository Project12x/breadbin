# Breadbin SID VST - Handoff Document

## Project Overview
Breadbin is a VST3/Standalone synthesizer emulating the Commodore 64's SID chip using the cycle-accurate reSIDfp library. It features dual SID engines (6 voices total) with 8 chip model variants, comprehensive modulation, and an authentic digi sampler.

## Architecture

### Core Files
| File | Purpose |
|------|---------|
| `SIDEngine.h/cpp` | reSIDfp wrapper, per-chip emulation, 8 chip model profiles |
| `PluginProcessor.h/cpp` | Audio processing, MIDI handling, voice allocation, digi mixing |
| `PluginEditor.h/cpp` | JUCE GUI implementation, popup panels |
| `DigiSampler.h` | WAV sample loading, 4-bit/8-bit playback, pitch tracking |
| `SidFilePlayer.h/cpp` | .SID file playback via full libsidplayfp engine |

### Key Classes
- **SIDEngine**: Wraps reSIDfp, exposes `noteOn/noteOff`, filter, waveform, ADSR, chip model profiles
- **BreadbinProcessor**: 2 SIDEngine instances, note queues, arpeggiator, LFO, mod matrix, digi mixing
- **BreadbinEditor**: UI components, voice selection, parameter controls, popup panels
- **DigiSampler**: Loads WAV files, stores both 4-bit packed and 8-bit unpacked sample data

### Voice Routing
```
Voice 0-2 -> sidLeft  -> leftNoteQueue
Voice 3-5 -> sidRight -> rightNoteQueue
```

### Dual SID Modes
- **Stereo Split**: Left SID -> Left channel, Right SID -> Right channel
- **Unison**: Both SIDs summed to stereo with detune
- **Multitimbral**: Voice-level stereo routing

### Audio Signal Path
```
SID Voices (vol register = 15) -> Per-SID Pan (equal-power) -> Voice Gain Compensation
  + Digi 4-bit ($D418 volume writes) OR Digi 8-bit (direct float mix)
  -> Master Volume (output gain) -> DC Blocker (20Hz HP) -> Ultrasonic Filter -> Safety Limiter -> Noise Gate
```

### Chip Model Variants (8 profiles)
| ID | Name | Base | Character |
|----|------|------|-----------|
| 0 | MOS 6581 | 6581 | Standard early 6581 |
| 1 | MOS 6581 R2 | 6581 | Early revision, bright, weak combined waveforms |
| 2 | MOS 6581 R3 | 6581 | Most common 6581, slightly dark |
| 3 | MOS 6581 R4 | 6581 | Late revision, bright filter, strong combined waveforms |
| 4 | MOS 8580 | 8580 | Standard 8580, clean |
| 5 | MOS 8580 R5 | 8580 | Late 8580, darker, stronger combined waveforms |
| 6 | CSG 9580 | 8580 | Final production run, bright and clean |
| 7 | MOS 8580D | 8580 | Digiboost era, mellow |

### Arpeggiator
- `arpHeldNotes`: Captured from MIDI note-on
- `arpSequence`: Built by `rebuildArpSequence()` with octave expansion
- `processArpeggiator()`: Called per buffer, triggers at PAL/NTSC frame rate

### Digi Sampler
- **4-bit mode**: Authentic $D418 volume register writes (0-15), crunchy/lo-fi
- **8-bit mode**: Direct float mix into output bus (0-255 -> normalized), cleaner retro sound
- Both modes support pitch tracking (MIDI note -> playback rate) and looping
- WAV files quantized to both formats at load time for instant switching

## Build Instructions
```bash
cmake -B build
cmake --build build --config Release
```

### Test
```bash
ctest --test-dir build -C Release
```

Output: `build/Breadbin_artefacts/Release/VST3/Breadbin.vst3`
Standalone: `build/Breadbin_artefacts/Release/Standalone/Breadbin.exe`

## Dependencies
- JUCE 8.0.4 (via CPM)
- libsidplayfp 2.16.0 (release tarball, not git)
- MSVC on Windows 10, CMake

## Key Design Decisions
- **Master volume as output gain**: SID volume register always at 15. Master volume is applied
  as a multiplier on the final output so that digi playback (which overwrites $D418) is also
  affected by the gain knob.
- **DC blocker at 20Hz**: The MOS6581 has significant idle DC offset. A 20Hz 2nd-order
  Butterworth highpass removes it without affecting audible content.
- **Dual SID only**: 3-SID expansion was attempted and cancelled. Dual SID (6 voices) is the
  final architecture for breadbin.

## Remaining Work (See ROADMAP.md)
- DAW compatibility testing (Reaper, Ableton, FL Studio, Bitwig)
- macOS build (AU format)
- Linux build (LV2 format)
- Hard sync / ring mod audibility studies
- User documentation / manual
- Marketing assets

## Known Issues
- MutationTests has pre-existing 17/18 kill rate (triangle boundary test)
- Deprecated Font constructor warnings (JUCE 8)
