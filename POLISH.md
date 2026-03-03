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
- [ ] Remove SIDEngine aging members and `setAgingFactor()`
- [ ] Remove aging from `updateFilterRegisters()` offset
- [ ] Remove Processor `agingFactor`, `agingPtr`, `setAgingFactor()`, ControlParam::Aging
- [ ] Remove Editor UI: `agingSlider`, `agingLabel`, `agingStartLabel`, `agingEndLabel`, `agingAttach`
- [ ] Remove from `resized()` layout and `timerCallback()` sync
- [ ] Remove APVTS parameter `"aging"` from `createParameterLayout()`
- [ ] Remove from preset reset lambda (if present)
- [ ] Update integration test (line 287: `check("aging")`)
- [ ] Update ROADMAP.md Phase 3 (remove "Time Machine aging slider")
- [ ] Update STATE.md (remove "Time Machine aging simulation")
- [ ] Build + test

### 1B. Fix snapshotSidPlayerToAPVTS broken parameter IDs
- **Issue**: Lines 3950-3960 write to `"leftCutoff"`, `"leftResonance"`, `"leftLP"`, `"leftBP"`, `"leftHP"` — these APVTS parameter IDs do not exist. The `setParam` helper silently no-ops because `apvts.getParameter()` returns nullptr. Filter state is stored as non-APVTS member variables (`baseFilterCutoffLeft`, `baseFilterResLeft`).
- **Action**: Replace `setParam()` calls with direct writes to the non-APVTS fields via existing setters (`setBaseFilterCutoff()`, `setBaseFilterResonance()`). For filter mode bits (LP/BP/HP), call `sidLeft.setFilterMode()` / `sidRight.setFilterMode()` directly, mirroring both SID engines.
- **Files**: `PluginProcessor.cpp` (lines 3948-3960)
- **Risk**: Low. Currently a no-op; fix makes snapshot actually work.
- [ ] Replace leftCutoff/rightCutoff with `setBaseFilterCutoff(true/false, cutoff)`
- [ ] Replace leftResonance with `setBaseFilterResonance(true/false, res)`
- [ ] Replace filter mode with direct `sidLeft/sidRight.setFilterMode(lp, bp, hp)`
- [ ] Mirror to both SID engines (left and right get same snapshot values)
- [ ] Test: load a .SID, snapshot, verify filter controls update

### 1C. polyNoteCounter uint32_t wrap safety
- **Issue**: `polyNoteCounter` increments every note-on and is used for voice-stealing age comparison. After ~4 billion notes it wraps to 0, causing youngest voice to appear oldest and get stolen first.
- **Action**: Add a `normalizeNoteCounters()` call when counter exceeds a threshold (e.g., UINT32_MAX / 2). This renumbers all active voices relative to the oldest, resetting the counter.
- **Files**: `PluginProcessor.h/cpp`
- **Risk**: Very low. Theoretical issue at extreme usage, but trivial to prevent.
- [ ] Add counter normalization logic
- [ ] Test: verify voice stealing order is preserved after normalization

---

## Phase 2: Audio Correctness

Fixes that affect audio output quality or DAW integration.

### 2A. Fix getTailLengthSeconds()
- **Issue**: Returns 0.0, but the plugin has reverb (up to 10s decay), delay (up to 1s), and SID release envelopes (up to 24s). DAWs use this to determine how long to render after stop — returning 0 causes abrupt tail cutoff on bounce/freeze.
- **Action**: Return a conservative fixed value (e.g., 10.0 seconds) or compute dynamically from reverb decay + delay time + max release.
- **Files**: `PluginProcessor.h` or `PluginProcessor.cpp`
- **Risk**: None. Only affects DAW tail rendering behavior (improvement).
- [ ] Implement getTailLengthSeconds() with reasonable value
- [ ] Test: verify in Reaper that reverb tail is preserved on render

### 2B. Wavetable rate aliasing at block boundaries
- **Issue**: Wavetable step changes happen once per processBlock (~86Hz at 44.1kHz/512 samples). When wavetable rate exceeds ~86Hz (max is 200Hz), multiple steps are skipped per block, causing aliased stepping artifacts.
- **Action**: Move wavetable advancement to per-sample or sub-block granularity. Alternatively, cap effective WT rate to Nyquist of block rate and document the limitation.
- **Files**: `PluginProcessor.cpp` (wavetable processing section in processBlock)
- **Risk**: Medium. Per-sample WT would add branching cost; sub-block (e.g., 32-sample chunks) is a good middle ground.
- [ ] Profile current WT overhead
- [ ] Implement sub-block WT stepping (or per-sample if overhead is negligible)
- [ ] Test: verify smooth stepping at 200Hz WT rate

### 2C. Sustain pedal release uses arp held-note tracking
- **Issue**: When sustain pedal is released, the code iterates `arpHeldNotes` to find notes to release — but this container is also used by the arpeggiator. When arp is disabled, `arpHeldNotes` may not accurately reflect held keys.
- **Action**: Audit the note-on/off path. If `arpHeldNotes` is always populated regardless of arp state, document this. Otherwise, use a separate `sustainedNotes` set.
- **Files**: `PluginProcessor.cpp` (handleMidiEvent sustain pedal section)
- **Risk**: Low. Needs careful audit of note tracking across all voice modes.
- [ ] Audit arpHeldNotes population in all voice modes
- [ ] Fix or document behavior
- [ ] Test: sustain pedal in mono/poly/para modes with arp disabled

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
- [ ] Extract LFO processing (deduplicate LFO1/LFO2)
- [ ] Extract filter envelope processing
- [ ] Extract mod matrix processing
- [ ] Extract wavetable processing
- [ ] Verify all 382 integration tests still pass

### 3B. Voice settings dirty flag for applyVoiceSettings
- **Issue**: `applyVoiceSettings()` syncs all 6 voice parameters to both SID engines every processBlock, even when nothing changed. This includes waveform, PW, ADSR, ring mod, sync, filter routing, and mod offset for all voices.
- **Action**: Add a `voiceSettingsDirty` flag, set it when any voice parameter changes, and only call `applyVoiceSettings()` when dirty.
- **Files**: `PluginProcessor.h/cpp`
- **Risk**: Low. Must ensure all parameter change paths set the dirty flag.
- [ ] Add dirty flag
- [ ] Set flag in all voice parameter change paths
- [ ] Guard applyVoiceSettings with dirty check
- [ ] Test: verify voice settings still apply correctly

### 3C. Filter envelope code deduplication (mono vs poly paths)
- **Issue**: The filter envelope ADSR logic is computed once for the mono path and then repeated with slight variation in the poly voice loop. Both paths compute attack/decay/sustain/release rates and advance the envelope state.
- **Action**: Extract filter envelope tick into a reusable struct/function that both paths call.
- **Files**: `PluginProcessor.cpp`
- **Risk**: Low. Pure refactor.
- [ ] Extract filter envelope into shared function
- [ ] Verify mono and poly paths produce identical results

---

## Phase 4: UI Polish

Visual improvements and usability enhancements.

### 4A. Timer repaint optimization
- **Issue**: `timerCallback()` at 30Hz unconditionally repaints 6 mod meters + 2 filter response displays every tick, even when values haven't changed.
- **Action**: Cache previous values and only `repaint()` when they differ (threshold comparison for float values).
- **Files**: `PluginEditor.cpp` (timerCallback)
- **Risk**: Very low. Reduces unnecessary UI work.
- [ ] Add cached previous values for all metered displays
- [ ] Conditional repaint on value change
- [ ] Verify visual update still works

### 4B. Para mode voice editor clarity
- **Issue**: In Paraphonic mode, all voices share the same filter and ADSR settings inherited from voice 0. The voice editor still shows individual voice tabs, which is misleading — edits to voices 1-5 are overridden.
- **Action**: When in Para mode, either (a) disable/grey out voice 1-5 tabs with a "Shared from V0" label, or (b) auto-sync edits across all voices with visual indication.
- **Files**: `PluginEditor.cpp` (voice editor section)
- **Risk**: Low. UI-only change.
- [ ] Detect para mode in voice editor
- [ ] Grey out or annotate inherited voices
- [ ] Test: switch between modes, verify UI updates

### 4C. FX bypass visual feedback
- **Issue**: FX enable/disable toggles exist but aren't visually prominent. When an effect is bypassed, there's no strong visual cue (e.g., dimmed section, strikethrough label).
- **Action**: When FX is disabled, reduce opacity of the entire effect's control group or add a "BYPASSED" overlay.
- **Files**: `PluginEditor.cpp` (FX section)
- **Risk**: Very low. Visual-only.
- [ ] Add visual dimming/overlay for bypassed FX sections
- [ ] Test: toggle each FX on/off, verify visual feedback

### 4D. Mod matrix activity indicators on main panel
- **Issue**: When mod matrix slots are active, there's no indication on the main panel — user must open the Modulation popup to see routing status.
- **Action**: Add small LED-style indicators next to the Modulation button showing how many slots are active (e.g., 4 dots, lit when slot is enabled).
- **Files**: `PluginEditor.h/cpp`
- **Risk**: Low. Additive UI feature.
- [ ] Add mod slot indicator component
- [ ] Update in timerCallback when slot enable states change
- [ ] Position near Modulation popup button

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

## Priority Summary

| Phase | Items | Effort | Impact |
|-------|-------|--------|--------|
| 1: Critical Fixes | 3 | Low | High — eliminates bugs and dead code |
| 2: Audio Correctness | 3 | Low-Medium | High — improves DAW compat and sound quality |
| 3: Code Quality | 3 | Medium | Medium — reduces maintenance burden |
| 4: UI Polish | 4 | Low-Medium | Medium — improves user experience |
| 5: Nice-to-Have | 4 | Medium-High | Low — deferred post-release |

**Recommended execution order**: Phase 1 -> Phase 2 -> Phase 3 -> Phase 4 -> Phase 5
