# How to Use Breadbin SID VST

## Installation
1. Build the project or copy `Breadbin.vst3` to your VST3 folder
2. Scan for plugins in your DAW
3. Load Breadbin on a MIDI track

## Quick Start

### Play Notes
- Use your MIDI keyboard or the on-screen virtual keyboard
- All 3 SIDs play in unison by default (Stereo mode)

### Change the Sound
1. Click a voice button (1-9) to select it
2. Adjust waveform: Triangle, Saw, Pulse, Noise
3. Tweak Pulse Width for PWM sounds
4. Shape the envelope with Attack, Decay, Sustain, Release

### Use the Filters
Each SID has independent LP/BP/HP filter:
- **Cut**: Filter cutoff frequency
- **Res**: Resonance emphasis
- **LP/BP/HP**: Enable filter modes (can combine)

### Multitimbral Mode
Switch Mode to "Multitimbral" for keyboard split:
- Below C4 → Left SID
- C4 to B4 → Center SID  
- C5 and above → Right SID

### Arpeggiator
1. Enable with the **ON** toggle
2. Hold multiple notes
3. Choose pattern: Up, Down, Up/Down, Random
4. Select rate: PAL 50Hz (authentic), NTSC 60Hz, or Sync
5. Expand with Octaves knob (1-3)

### Chip Models
Select per SID:
- **6581**: Dark, gritty, more distortion (early C64)
- **8580**: Cleaner, brighter (later C64)

### Time Machine
The Aging slider simulates component wear:
- 0% = Factory new
- 100% = Well-worn vintage character

## Tips
- Layer 3 slightly detuned pulse waves for thick pads
- Use 6581 for bass, 8580 for leads
- Multitimbral mode: bass on left, lead on right, arp in center
- PAL 50Hz arp rate is authentic C64 speed
