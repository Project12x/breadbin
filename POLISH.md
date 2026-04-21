# Improvement & Polish Plan

Phased plan for hardening, cleanup, and UI polish leading to release.
Each phase is independent — complete one before starting the next.

**Legend**: `[x]` done, `[ ]` pending

---

## Phase 1: Critical Bug Fixes

Fixes that cause incorrect behavior or silent failures today.

### 1A. Remove aging factor feature
- **Issue**: `setAgingFactor()` called every processBlock, cascades to all 26 SID engines (2 mono + 24 poly). The feature only offsets filter cutoff by up to -200 — minimal sonic impact for significant per-block CPU cost.
- **Action**: Remove entirely: APVTS param `"aging"`, `agingFactor` field, `setAgingFactor()` in SIDEngine + Processor, `agingCutoffOffset` in SIDEngine, all UI components (slider, labels, attachment), MIDI learn enum entry, Time Machine references in ROADMAP/STATE.
- **Files**: `SIDEngine.h/cpp`, `PluginProcessor.h/cpp`, `PluginEditor.h/cpp`, `ROADMAP.md`, `STATE.md`
- **Risk**: Low. Deprecates a minor feature. Existing presets with `aging` value will silently ignore it on load.
- [x] Remove SIDEngine aging members and `setAgingFactor()`
- [x] Remove aging from `updateFilterRegisters()` offset
- [x] Remove Processor `agingFactor`, `agingPtr`, `setAgingFactor()`, ControlParam::Aging
- [x] Remove Editor UI: `agingSlider`, `agingLabel`, `agingStartLabel`, `agingEndLabel`, `agingAttach`
- [x] Remove from `resized()` layout and `timerCallback()` sync
- [x] Remove APVTS parameter `"aging"` from `createParameterLayout()`
- [x] Remove from preset reset lambda (if present)
- [x] Update integration test (line 287: `check("aging")`)
- [x] Update ROADMAP.md Phase 3 (remove "Time Machine aging slider")
- [x] Update STATE.md (remove "Time Machine aging simulation")
- [x] Build + test

### 1B. Fix snapshotSidPlayerToAPVTS broken parameter IDs
- **Issue**: Lines 3950-3960 write to `"leftCutoff"`, `"leftResonance"`, `"leftLP"`, `"leftBP"`, `"leftHP"` — these APVTS parameter IDs do not exist. The `setParam` helper silently no-ops because `apvts.getParameter()` returns nullptr. Filter state is stored as non-APVTS member variables (`baseFilterCutoffLeft`, `baseFilterResLeft`).
- **Action**: Replace `setParam()` calls with direct writes to the non-APVTS fields via existing setters (`setBaseFilterCutoff()`, `setBaseFilterResonance()`). For filter mode bits (LP/BP/HP), call `sidLeft.setFilterMode()` / `sidRight.setFilterMode()` directly, mirroring both SID engines.
- **Files**: `PluginProcessor.cpp` (lines 3948-3960)
- **Risk**: Low. Currently a no-op; fix makes snapshot actually work.
- [x] Replace leftCutoff/rightCutoff with `setBaseFilterCutoff(true/false, cutoff)`
- [x] Replace leftResonance with `setBaseFilterResonance(true/false, res)`
- [x] Replace filter mode with direct `sidLeft/sidRight.setFilterMode(lp, bp, hp)`
- [x] Mirror to both SID engines (left and right get same snapshot values)
- [x] Test: load a .SID, snapshot, verify filter controls update

### 1C. polyNoteCounter uint32_t wrap safety
- **Issue**: `polyNoteCounter` increments every note-on and is used for voice-stealing age comparison. After ~4 billion notes it wraps to 0, causing youngest voice to appear oldest and get stolen first.
- **Action**: Add a `normalizeNoteCounters()` call when counter exceeds a threshold (e.g., UINT32_MAX / 2). This renumbers all active voices relative to the oldest, resetting the counter.
- **Files**: `PluginProcessor.h/cpp`
- **Risk**: Very low. Theoretical issue at extreme usage, but trivial to prevent.
- [x] Add counter normalization logic
- [x] Test: verify voice stealing order is preserved after normalization

---

## Phase 2: Audio Correctness

Fixes that affect audio output quality or DAW integration.

### 2A. Fix getTailLengthSeconds()
- **Issue**: Returns 0.0, but the plugin has reverb (up to 10s decay), delay (up to 1s), and SID release envelopes (up to 24s). DAWs use this to determine how long to render after stop — returning 0 causes abrupt tail cutoff on bounce/freeze.
- **Action**: Return a conservative fixed value (e.g., 10.0 seconds) or compute dynamically from reverb decay + delay time + max release.
- **Files**: `PluginProcessor.h` or `PluginProcessor.cpp`
- **Risk**: None. Only affects DAW tail rendering behavior (improvement).
- [x] Implement getTailLengthSeconds() with reasonable value
- [x] Test: verify in Reaper that reverb tail is preserved on render

### 2B. Wavetable rate aliasing at block boundaries
- **Issue**: Wavetable step changes happen once per processBlock (~86Hz at 44.1kHz/512 samples). When wavetable rate exceeds ~86Hz (max is 200Hz), multiple steps are skipped per block, causing aliased stepping artifacts.
- **Action**: Move wavetable advancement to per-sample or sub-block granularity. Alternatively, cap effective WT rate to Nyquist of block rate and document the limitation.
- **Files**: `PluginProcessor.cpp` (wavetable processing section in processBlock)
- **Risk**: Medium. Per-sample WT would add branching cost; sub-block (e.g., 32-sample chunks) is a good middle ground.
- [ ] Profile current WT overhead
- [ ] Implement sub-block WT stepping (or per-sample if overhead is negligible)
- [ ] Test: verify smooth stepping at 200Hz WT rate

### 2C. ~~Sustain pedal release uses arp held-note tracking~~ VERIFIED OK

- **Audit result**: `arpHeldNotes` is populated on every note-on/off regardless of arp state. It functions as a general "physically held keys" tracker. Sustain pedal release correctly uses it to identify notes no longer held. Naming is misleading but behavior is correct. No fix needed.

---

## Phase 3: Code Quality & Maintainability

Structural improvements that reduce bug surface area and make future work easier.

### 3A. Extract modulation processing from processBlock
- **Issue**: processBlock is ~770 lines. LFO processing, filter envelope, mod matrix, and wavetable logic are all inline. LFO1 and LFO2 processing is duplicated.
- **Action**: Extract into helper methods:
  - `processLFO(lfoIndex)` — unified LFO1/LFO2 processing
  - `processFilterEnvelope()` — filter ADSR + mod stacking
  - `processModMatrix()` — 4-slot routing
  - `processWavetable()` — step advancement + waveform application
- **Files**: `PluginProcessor.h/cpp`
- **Risk**: Low. Pure refactor, no behavior change. Test suite catches regressions.
- [x] Extract LFO processing (deduplicate LFO1/LFO2)
- [x] Extract filter envelope processing
- [x] Extract mod matrix processing
- [x] Extract wavetable processing
- [x] Verify all 382 integration tests still pass

### 3B. Voice settings dirty flag for applyVoiceSettings
- **Issue**: `applyVoiceSettings()` syncs all 6 voice parameters to both SID engines every processBlock, even when nothing changed. This includes waveform, PW, ADSR, ring mod, sync, filter routing, and mod offset for all voices.
- **Action**: Add a `voiceSettingsDirty` flag, set it when any voice parameter changes, and only call `applyVoiceSettings()` when dirty.
- **Files**: `PluginProcessor.h/cpp`
- **Risk**: Low. Must ensure all parameter change paths set the dirty flag.
- [x] Add dirty flag
- [x] Set flag in all voice parameter change paths
- [x] Guard applyVoiceSettings with dirty check
- [x] Test: verify voice settings still apply correctly

### 3C. Filter envelope code deduplication (mono vs poly paths)
- **Issue**: The filter envelope ADSR logic is computed once for the mono path and then repeated with slight variation in the poly voice loop. Both paths compute attack/decay/sustain/release rates and advance the envelope state.
- **Action**: Extract filter envelope tick into a reusable struct/function that both paths call.
- **Files**: `PluginProcessor.cpp`
- **Risk**: Low. Pure refactor.
- [x] Extract filter envelope into shared function
- [x] Verify mono and poly paths produce identical results

---

## Phase 4: UI Polish

Visual improvements and usability enhancements.

### 4A. Timer repaint optimization
- **Issue**: `timerCallback()` at 30Hz unconditionally repaints 6 mod meters + 2 filter response displays every tick, even when values haven't changed.
- **Action**: Cache previous values and only `repaint()` when they differ (threshold comparison for float values).
- **Files**: `PluginEditor.cpp` (timerCallback)
- **Risk**: Very low. Reduces unnecessary UI work.
- [x] Add cached previous values for all metered displays
- [x] Conditional repaint on value change
- [x] Verify visual update still works

### 4B. Para mode voice editor clarity
- **Issue**: In Paraphonic mode, all voices share the same filter and ADSR settings inherited from voice 0. The voice editor still shows individual voice tabs, which is misleading — edits to voices 1-5 are overridden.
- **Action**: When in Para mode, either (a) disable/grey out voice 1-5 tabs with a "Shared from V0" label, or (b) auto-sync edits across all voices with visual indication.
- **Files**: `PluginEditor.cpp` (voice editor section)
- **Risk**: Low. UI-only change.
- [x] Detect para mode in voice editor
- [x] Grey out or annotate inherited voices
- [x] Test: switch between modes, verify UI updates

### 4C. FX bypass visual feedback
- **Issue**: FX enable/disable toggles exist but aren't visually prominent. When an effect is bypassed, there's no strong visual cue (e.g., dimmed section, strikethrough label).
- **Action**: When FX is disabled, reduce opacity of the entire effect's control group or add a "BYPASSED" overlay.
- **Files**: `PluginEditor.cpp` (FX section)
- **Risk**: Very low. Visual-only.
- [x] Add visual dimming/overlay for bypassed FX sections
- [x] Test: toggle each FX on/off, verify visual feedback

### 4D. Mod matrix activity indicators on main panel
- **Issue**: When mod matrix slots are active, there's no indication on the main panel — user must open the Modulation popup to see routing status.
- **Action**: Add small LED-style indicators next to the Modulation button showing how many slots are active (e.g., 4 dots, lit when slot is enabled).
- **Files**: `PluginEditor.h/cpp`
- **Risk**: Low. Additive UI feature.
- [x] Add mod slot indicator component
- [x] Update in timerCallback when slot enable states change
- [x] Position near Modulation popup button

---

## Phase 5: Nice-to-Have (Post-Release)

Lower priority improvements that can wait for a future release.

### 5A. DPI-aware resizable GUI
- **Issue**: Fixed 1000x800 window with narrow resize limits (900-1200 x 750-1000). Not truly DPI-aware or freely resizable.
- **Effort**: High. Requires converting all hardcoded pixel positions to proportional layout.
- [ ] Convert resized() to proportional/relative layout
- [ ] Test at multiple DPI settings (100%, 125%, 150%, 200%)

### 5B. Visual voice allocation display
- **Issue**: No way to see which SID voices are active in real-time per mode.
- **Effort**: Medium. Needs real-time voice state export from audio thread.
- [ ] Design voice allocation indicator (e.g., 6 LED dots per SID)
- [ ] Export voice state from audio thread (lock-free)
- [ ] Render in UI

### 5C. Keyboard navigation for popup panels
- **Issue**: Mod Matrix, Chord Memory, Wavetable, SID Player popups require mouse interaction only.
- **Effort**: Medium. JUCE keyboard focus management.
- [ ] Add keyboard focus traversal to popup panels
- [ ] Escape to close, Tab to navigate

### 5D. ReverbSC malloc/free cleanup
- **Issue**: ReverbSC uses raw `malloc`/`free` for delay line buffers (inherited from Soundpipe C port).
- **Action**: Replace with `std::vector<float>` or `std::unique_ptr<float[]>`. Not RT-critical since allocation only happens in `init()`, not on audio thread.
- **Effort**: Low. Straightforward modernization.
- [ ] Replace malloc/free with RAII container
- [ ] Verify reverb still sounds identical

---

## Phase 6: Code Refactoring

Structural refactoring to reduce file sizes, eliminate duplication, and improve navigability. Pure refactors — no behavior changes.

### 6A. Split processBlock into subsystems

- **Issue**: `processBlock()` is 762 lines in a single function. Mixes parameter sync, MIDI handling, modulation, sample generation, SID file player mixing, FX chain, and safety processing.
- **Action**: Extract into named helpers:
  - `generateAudio(buffer, numSamples)` — poly + mono/para sample loops (~280 lines)
  - `mixSidFilePlayer(buffer, numSamples)` — resampler + channel mixing (~43 lines)
  - `processFXChain(buffer, numSamples)` — chorus, delay, reverb (~64 lines)
  - `applySafetyChain(buffer, numSamples)` — limiter, noise gate, DC blocker (~66 lines)
- **Files**: `PluginProcessor.h/cpp`
- **Risk**: Low. Pure extraction, test suite catches regressions.
- [x] Extract generateAudio()
- [x] Extract mixSidFilePlayer()
- [x] Extract processFXChain()
- [x] Extract applySafetyChain()
- [x] Verify all integration tests pass

### 6B. Split handleMidiEvent into logical handlers

- **Issue**: `handleMidiEvent()` is 287 lines with nested switch statements for note-on, note-off, all-notes-off, pitch bend, CC, and sustain pedal logic.
- **Action**: Extract into:
  - `handleNoteOn(note, channel)`
  - `handleNoteOff(note, channel)`
  - `handleAllNotesOff()`
  - `handleSustainPedal(value)`
- **Files**: `PluginProcessor.h/cpp`
- **Risk**: Low. Pure extraction.
- [x] Extract handleNoteOn()
- [x] Extract handleNoteOff()
- [x] Extract handleAllNotesOff()
- [x] Extract handleSustainPedal()
- [x] Verify all integration tests pass

### 6C. Deduplicate mono/poly glide and filter modulation

- **Issue**: Glide processing (lines 300-333) has nearly identical mono and poly paths. Filter modulation offset calculation (lines 402-407 vs 1436-1462) is computed twice with slight variation. Pan/gain mixing formula is duplicated across poly (571-575) and mono (671-674) paths.
- **Action**: Extract shared helpers:
  - `processGlide()` — unified mono/poly glide with voice mode branch
  - `computeFilterModOffset()` — returns combined LFO + mod wheel + envelope offset
  - `mixWithPanLaw()` — shared stereo output mixing
- **Files**: `PluginProcessor.h/cpp`
- **Risk**: Low. Must verify audio output is bit-identical.
- [x] Extract computeFilterModOffset() helper
- [x] Extract updatePanCache() helper
- [x] Verify integration tests pass

### 6D. Merge setupLeftSID/setupRightSID

- **Issue**: `setupLeftSID()` and `setupRightSID()` are 153 lines each, ~80% identical. Both set up chip selector, 3 voice buttons, 3 voice enables, filter cutoff/resonance sliders + meters, filter mode buttons, pan slider, detune slider.
- **Action**: Extract `setupSidPanel(bool isLeft)` that parameterizes control references.
- **Files**: `PluginEditor.h/cpp`
- **Risk**: Low. UI-only refactor.
- [x] Create setupSidPanel(bool isLeft) helper
- [x] Remove setupLeftSID/setupRightSID
- [x] Verify UI renders correctly

### 6E. Split resized() into region helpers

- **Issue**: `resized()` is 399 lines of sequential `.setBounds()` calls with hardcoded pixel values. FX rows (chorus/delay/reverb) follow identical layout patterns but are copy-pasted.
- **Action**: Extract region helpers:
  - `layoutTopRow(bounds)` — mode selectors, preset nav, master volume
  - `layoutSidPanels(bounds)` — left/right SID controls
  - `layoutVoiceEditor(bounds)` — waveform, ADSR, modulation toggles
  - `layoutFXChain(bounds)` — chorus/delay/reverb (deduplicate row pattern)
  - `layoutFilterEnvelope(bounds)` — filter env ADSR + amount
  - `layoutPopupButtons(bounds)` — bottom popup button row
- **Files**: `PluginEditor.h/cpp`
- **Risk**: Low. Layout-only refactor.
- [x] Extract 4 layout region helpers (layoutTopRow, layoutSidPanels, layoutVoiceEditor, layoutBottomControls)
- [x] Verify UI layout unchanged

### 6F. Split setupControls() and reduce boilerplate

- **Issue**: `setupControls()` is 721 lines — the largest function in the editor. Contains duplicated preset navigation lambdas (prev/next share `buildNavIds` logic), verbose popup menu construction, and repeated slider/label setup patterns (~50 occurrences across the file).
- **Action**:
  - Extract `buildPresetNavigationList()` to deduplicate prev/next lambdas
  - Extract `setupLabeledSlider(label, slider, name, range, ...)` helper
  - Split setupControls into thematic sections (presets, arp, FX, global)
- **Files**: `PluginEditor.h/cpp`
- **Risk**: Low. Pure refactor.
- [x] Split setupControls() into setupGlobalControls(), setupFXControls(), setupPopupButtons()
- [x] Verify UI functions correctly

### 6G. Split timerCallback into update functions

- **Issue**: `timerCallback()` is 254 lines mixing meter updates, voice count display, para mode visibility, FX bypass visuals, mod matrix indicators, and SID player register overlay.
- **Action**: Extract:
  - `updateModulationMeters()`
  - `updateVoiceCountDisplay()`
  - `updateFxBypassVisuals()`
  - `updateSidPlayerOverlay()`
- **Files**: `PluginEditor.cpp`
- **Risk**: Very low. Pure extraction.
- [x] Extract 4 timer update helpers
- [x] Verify all timer-driven visuals still work

### 6H. Remove dead per-voice pan code

- **Issue**: Per-voice pan was replaced by per-SID pan but remnants remain: `VoicePan` MIDI mapping (line 2881-2884, no-op with comment), state load comment (line 2056). No UI or preset writes to this path.
- **Action**: Remove dead MIDI mapping case and leftover comments.
- **Files**: `PluginProcessor.cpp`
- **Risk**: Very low. Already confirmed unused.
- [x] Remove VoicePan MIDI mapping case
- [x] Remove leftover per-voice pan comments
- [x] Verify build + tests pass

---

## Priority Summary

| Phase | Items | Effort | Impact |
|-------|-------|--------|--------|
| 1: Critical Fixes | 3 | Low | High — eliminates bugs and dead code |
| 2: Audio Correctness | 3 | Low-Medium | High — improves DAW compat and sound quality |
| 3: Code Quality | 3 | Medium | Medium — reduces maintenance burden |
| 4: UI Polish | 4 | Low-Medium | Medium — improves user experience |
| 5: Nice-to-Have | 4 | Medium-High | Low — deferred post-release |
| 6: Code Refactoring | 8 | Medium | Medium — reduces file sizes and duplication |

**Recommended execution order**: Phase 1 -> Phase 2 -> Phase 3 -> Phase 4 -> Phase 6 -> Phase 5
