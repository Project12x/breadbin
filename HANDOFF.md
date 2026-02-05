# Breadbin SID VST - Handoff Document

## Project Overview
Breadbin is a VST3 synthesizer emulating the Commodore 64's SID chip using the cycle-accurate reSIDfp library. It features 3 independent SID chips for a total of 9 voices.

## Architecture

### Core Files
| File | Purpose |
|------|---------|
| `SIDEngine.h/cpp` | reSIDfp wrapper, per-chip emulation |
| `PluginProcessor.h/cpp` | Audio processing, MIDI handling, voice allocation |
| `PluginEditor.h/cpp` | JUCE GUI implementation |

### Key Classes
- **SIDEngine**: Wraps reSIDfp, exposes `noteOn/noteOff`, filter, waveform, ADSR
- **BreadbinProcessor**: 3 SIDEngine instances, note queues, arpeggiator logic
- **BreadbinEditor**: UI components, voice selection, parameter controls

### Voice Routing
```
Voice 0-2 → sidLeft   → leftNoteQueue
Voice 3-5 → sidCenter → centerNoteQueue  
Voice 6-8 → sidRight  → rightNoteQueue
```

### Arpeggiator
- `arpHeldNotes`: Captured from MIDI note-on
- `arpSequence`: Built by `rebuildArpSequence()` with octave expansion
- `processArpeggiator()`: Called per buffer, triggers at PAL/NTSC frame rate

## Build Instructions
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Output: `build/Breadbin_artefacts/Release/VST3/Breadbin.vst3`

## Next Steps (See ROADMAP.md)
1. Ring Modulation + Hard Sync
2. Per-voice filter routing
3. LFO system
4. Portamento
5. External audio input

## Known Issues
- `samplesPerStep` warning in arpeggiator (missing default case)
- Deprecated Font constructor warnings (JUCE 8)
