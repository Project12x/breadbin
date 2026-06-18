# Breadbin UI Chrome Repair Design

## Goal

Restore the refactored UI's missing high-level chrome: visible preset access, visible logo/brand presence, and a slim footer that also hosts the poly/voicing controls.

## Approved Direction

Use the balanced repair layout:

- Keep the global patch selector in the top bar, but reserve enough width for it to remain usable.
- Restore the embedded Breadbin logo on the far-left side of the top bar.
- Move the poly/voicing cluster out of the crowded top bar and into the new footer.
- Add a slim always-visible footer below the keyboard for brand, status/help text, and poly controls.

## Top Bar

The top bar should prioritize global actions:

- Logo area at the far left.
- Engine selector near the logo.
- Master and gate controls.
- Protected global preset browser with previous/next, dirty indicator, save, and load controls.
- CPU and UI scale controls at the far right.

The preset selector must not be starved by lower-priority controls. If space is tight, the footer-owned poly controls must not remain in the top bar.

## Footer

The footer should be slim and persistent. It should sit below the keyboard and use the same glass/chip-console visual language as the rest of the C64 reskin.

Footer content:

- Left: `ANTIGRAVITY · BREADBIN` brand text.
- Center: status/help text. Initial content can report the current preset surface, for example `PATCH READY`.
- Right: poly/voicing cluster:
  - Voicing mode.
  - Poly max notes.
  - Active voice count.
  - Para spread.
  - Retrig.

The footer must remain usable at the existing scaled editor sizes.

## Implementation Notes

- Reuse `globalPresetSelector`, `presetPrevButton`, `presetNextButton`, `savePatchButton`, `loadPatchButton`, and `presetDirtyLabel`.
- Reuse `BinaryData::logo_png`.
- Reuse the existing `voiceModeSelector`, `polyMaxNotesSelector`, `polyVoiceCountLabel`, `paraSpreadSlider`, and `paraRetrigButton`; only relocate them.
- Do not rewrite preset loading/saving behavior.
- Keep the change localized to `src/PluginEditor.h` and `src/PluginEditor.cpp` unless build metadata unexpectedly requires an asset reference update.

## Verification

- Build `Breadbin_All` in Release.
- Launch the Release standalone and visually confirm:
  - Logo is visible in the top-left top bar.
  - Global preset selector is visible and wide enough to select factory/user presets.
  - Footer is visible below the keyboard.
  - Poly/voicing controls are in the footer and not duplicated in the top bar.
  - No controls overlap at 100% scale.
