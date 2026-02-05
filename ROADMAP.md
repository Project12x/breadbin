# Breadbin SID VST - Roadmap

## Phase 1: Core Engine ✅ Complete
- [x] reSIDfp integration with cycle-accurate emulation
- [x] Dual SID architecture (Left/Right)
- [x] Triple SID architecture (Left/Center/Right)
- [x] 9 voices (3 per SID)
- [x] Per-voice ADSR envelope
- [x] Waveforms: Triangle, Saw, Pulse, Noise
- [x] Pulse width control
- [x] SID chip model selection (6581/8580)
- [x] Time Machine aging simulation

## Phase 2: Modes & Routing ✅ Complete
- [x] Stereo/Unison mode
- [x] Multitimbral keyboard split (C4-B4 = center)
- [x] Per-SID panning
- [x] Resonant LP/BP/HP filters per SID
- [x] Note queue system for voice allocation

## Phase 3: Arpeggiator ✅ Complete
- [x] C64-accurate PAL 50Hz / NTSC 60Hz timing
- [x] Patterns: Up, Down, Up/Down, Random
- [x] Octave range (1-3)
- [x] Host sync option

## Phase 4: Advanced SID Modulation 🔜 Next
- [ ] Ring Modulation (Voice 3 × Voice 1)
- [ ] Hard Sync (Voice 1 resets Voice 3)
- [ ] Filter routing per voice
- [ ] Per-SID detune

## Phase 5: LFO & Modulation
- [ ] LFO with rate/waveform (Tri/Saw/Square/S&H)
- [ ] Filter cutoff modulation
- [ ] Pulse width modulation (PWM)
- [ ] Pitch vibrato
- [ ] Portamento/Glide

## Phase 6: MIDI & Expression
- [ ] Pitch bend with range setting
- [ ] Mod wheel routing
- [ ] Velocity sensitivity options

## Phase 7: Arpeggiator Extensions
- [ ] Speed multiplier (1x/2x/3x)
- [ ] Chord memory (4 slots)

## Phase 8: External Audio
- [ ] Sidechain input through SID filter
- [ ] Dry/wet mix control
