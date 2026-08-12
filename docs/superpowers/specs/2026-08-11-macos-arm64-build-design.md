# Breadbin macOS Arm64 Build Design

**Date:** 2026-08-11
**Status:** Approved

## Objective

Add a reproducible native Apple Silicon build of Breadbin for the owner's M4
Mac. Bring up the Standalone application first, then the VST3 and Audio Unit
instrument formats. The first milestone is a local development release; a
later milestone will produce Universal binaries and public-release signing,
notarization, and packaging.

## Scope

Stage 1 produces three arm64 Release bundles:

- `Breadbin.app`
- `Breadbin.vst3`
- `Breadbin.component`

The Standalone target is the first checkpoint. VST3 is enabled and validated
only after the Standalone build and core tests pass. AU follows VST3. This
ordering isolates general macOS compiler and runtime problems from
format-specific host and metadata problems.

Stage 1 includes user-local installation of VST3 and AU, timestamped release
snapshots, local ad-hoc signing where macOS requires a valid signature, test
execution, and validation in REAPER and FL Studio. It excludes Developer ID
signing, notarization, a public installer, and CI/CD.

Stage 2 produces Universal `arm64;x86_64` bundles from the same source and
plugin identity. Public-release signing, notarization, stapling, and installer
packaging are separate release-engineering work after Universal validation.

## Build Architecture

Breadbin remains one JUCE target whose processor, editor, assets, and DSP code
are shared by all formats. Platform differences remain in CMake:

- Windows builds VST3 and Standalone and retains Windows/ASIO definitions.
- macOS builds Standalone, VST3, and AU without Windows definitions.
- Plugin manufacturer code `Estw`, plugin code `Bred`, and bundle ID
  `com.ericsteenwerth.breadbin` remain unchanged to preserve host project and
  state compatibility.
- No DSP, UI, preset, parameter, or serialized-state behavior changes are part
  of this work.

The unconditional `WIN32` compile definitions in the current build are moved
behind actual Windows conditions. Apple-specific options and formats are
enabled only when `APPLE` is true.

The existing `GHOSTMOON_OSS_DIR` and `GHOSTMOON_TOOLS_INCLUDE_DIR` cache paths
become portable. On this workstation their defaults resolve to the sibling
repositories `/Volumes/Code/ghostmoongpl` and
`/Volumes/Code/ghostmoon/tools/include`; callers may override either path.
Configuration fails early with an actionable error if a required directory or
header is absent.

## Presets and Build Trees

`CMakePresets.json` provides reproducible, isolated configurations:

- `macos-arm64` uses Release mode and `CMAKE_OSX_ARCHITECTURES=arm64` under
  `build/macos-arm64`.
- `macos-universal` uses Release mode and
  `CMAKE_OSX_ARCHITECTURES=arm64;x86_64` under `build/macos-universal`. It is
  reserved for Stage 2 and must not be treated as verified during Stage 1.

The presets keep architecture and build-type choices out of ad hoc shell
commands. Existing Windows build behavior remains available and unchanged.

## Toolchain Prerequisites

The Mac currently does not expose Apple developer tools or CMake to the
terminal. Before compilation, the implementation verifies and documents:

- Apple Command Line Tools with a usable macOS SDK and clang toolchain.
- CMake 3.22 or newer.
- Access to the pinned JUCE, libsidplayfp, and melatonin_blur sources through
  the existing CMake dependency flow.
- The local `ghostmoongpl` and Ghostmoon profiling-header directories.

A full Xcode IDE project is not required for the first command-line build.

## Standalone-First Bring-Up

The first implementation checkpoint configures the macOS arm64 tree and builds
only Breadbin Standalone plus the standalone LFO and mutation tests. It then
builds and runs the JUCE-linked integration test target. This checkpoint fixes
only cross-platform build issues needed by shared code.

Standalone acceptance requires that the application bundle:

- contains native arm64 executable code;
- has valid bundle metadata and a locally valid signature;
- launches on the M4 Mac without Rosetta;
- opens its audio and MIDI device path;
- produces audio from MIDI input;
- loads and saves Breadbin presets; and
- opens SID and WAV files through macOS file dialogs.

Only after this checkpoint passes does implementation proceed to plugin
formats.

## Plugin Bring-Up

VST3 is the second checkpoint because both installed DAWs can exercise it.
Breadbin VST3 is copied to:

`~/Library/Audio/Plug-Ins/VST3/Breadbin.vst3`

REAPER and FL Studio must both discover it after an explicit plugin rescan.
Each host must receive MIDI, produce audio, display the editor correctly on the
Retina display, and restore plugin state after saving, closing, and reopening a
project.

AU is the third checkpoint. Breadbin AU is copied to:

`~/Library/Audio/Plug-Ins/Components/Breadbin.component`

The component is an Audio Unit instrument and is validated with
`auval -v aumu Bred Estw`. REAPER may also be used as a live AU host. FL Studio
validation remains focused on VST3.

## Installation and Artifact Preservation

Compilation never installs plugins as a side effect. An explicit
`cmake --install` action performs local installation after successful build and
verification.

Before replacing installed Breadbin bundles, the install flow preserves the
newly built Standalone, VST3, and AU bundles under:

`releases/<timestamp>_macos-arm64/`

This mirrors the existing Windows convention of keeping generated build output
under `build/Breadbin_artefacts/Release` and preserving milestone binaries in
timestamped `releases` directories. Complete macOS bundles are copied rather
than extracting only their inner executables.

Installation writes only Breadbin's user-local plugin bundles. It does not
write to system-wide `/Library`, install the Standalone app into `/Applications`,
touch unrelated plugins, or require administrator privileges. Missing source
artifacts cause installation to stop before replacing installed bundles.

## Validation

Automated validation includes:

- CMake configure and Release compilation for the active checkpoint.
- Breadbin LFO, mutation, and integration test execution.
- Explicit documentation of any pre-existing accepted mutation-test survivor;
  it must not be silently ignored.
- `file` or `lipo` verification of the architecture inside every bundle.
- Property-list and bundle metadata validation.
- Strict local signature verification.
- AU validation with `auval -v aumu Bred Estw`.

Manual Stage 1 validation includes:

- Standalone MIDI input, audio output, preset persistence, SID loading, and WAV
  loading.
- REAPER VST3 discovery, AU discovery where supported, audio/MIDI operation,
  Retina UI behavior, and project state restoration.
- FL Studio VST3 discovery, audio/MIDI operation, Retina UI behavior, and
  project state restoration.

Stage 1 is complete only when all three arm64 formats pass their applicable
checks. Documentation records any host-specific rescan step or cache behavior
encountered during validation.

## Universal and Public Release Follow-Up

Stage 2 builds the same three formats with both `arm64` and `x86_64` slices.
`lipo` must report both architectures for each bundle. Native arm64 behavior is
retested on the M4; x86_64 behavior is tested under Rosetta or on an Intel Mac
before the result is described as verified Universal output.

Public distribution then adds Developer ID signing, hardened runtime settings,
Apple notarization and stapling, and installer packaging. Those steps require
the appropriate Apple developer credentials and are not prerequisites for the
local Stage 1 milestone.

## Documentation Deliverables

The implementation updates `README.md`, `HOWTO.md`, `STATE.md`, and
`ROADMAP.md` with the exact prerequisites, preset commands, artifact locations,
installation paths, architecture status, test results, and host-validation
results. Documentation must distinguish arm64-tested output from planned or
unverified Universal output.

