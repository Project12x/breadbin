# Breadbin macOS Arm64 Build Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build and locally install native arm64 Breadbin Standalone, VST3, and Audio Unit bundles on the owner's M4 Mac, bringing up Standalone first and preserving timestamped release artifacts.

**Architecture:** Keep one JUCE target and move platform differences into CMake. Use isolated Unix Makefiles preset trees for arm64 and future Universal output, validate each macOS bundle through a focused CMake script, and make plugin installation an explicit `cmake --install` component.

**Tech Stack:** CMake 3.22+, Apple clang/Command Line Tools, JUCE 9.0.0, C++20, libsidplayfp 2.16.0, melatonin_blur v1.4, Ghostmoon OSS headers, AU Validation Tool.

## Global Constraints

- Stage 1 architecture is exactly `arm64`; `arm64;x86_64` is reserved and documented as unverified until Stage 2.
- Build order is Standalone, then VST3, then AU.
- macOS formats are Standalone, VST3, and AU; Windows remains Standalone and VST3 with ASIO.
- Preserve manufacturer code `Estw`, plugin code `Bred`, and bundle ID `com.ericsteenwerth.breadbin`.
- Do not change DSP, UI, parameters, presets, or serialized state.
- Do not copy plugins during compilation.
- Installation writes only `~/Library/Audio/Plug-Ins/VST3/Breadbin.vst3` and `~/Library/Audio/Plug-Ins/Components/Breadbin.component`.
- Preserve complete bundles under `releases/<timestamp>_macos-arm64/` before replacing installed plugins.
- Breadbin installation does not write to `/Library` or `/Applications` and does not require administrator privileges; the separate CMake toolchain prerequisite may live in `/Applications/CMake.app`.
- Treat the existing one-survivor mutation result as documented evidence, not as a silently ignored passing test.
- Use the official CMake distribution and Apple Command Line Tools; do not add a project package-manager dependency.

---

## File Map

- `CMakeLists.txt`: platform formats, portable Ghostmoon paths, platform definitions, bundle-validation targets, and macOS install entry point.
- `CMakePresets.json`: arm64 Standalone, arm64 all-format, and reserved Universal configurations/builds.
- `cmake/ValidateMacOSBundle.cmake`: validate one bundle's existence, architectures, property list, ad-hoc signature, and strict signature verification.
- `cmake/InstallMacOSBundles.cmake`: preflight all artifacts, create a timestamped snapshot, and replace the two user-local plugin bundles.
- `README.md`: concise cross-platform build entry points.
- `HOWTO.md`: exact macOS prerequisite, build, validation, installation, and host-rescan procedure.
- `STATE.md`: measured macOS build/test/host status.
- `ROADMAP.md`: Stage 1 completion state and explicit Universal/signing follow-up.

### Task 1: Portable macOS Configuration and Presets

**Files:**
- Modify: `CMakeLists.txt:48-53`
- Modify: `CMakeLists.txt:139-186`
- Modify: `CMakeLists.txt:257-265`
- Create: `CMakePresets.json`

**Interfaces:**
- Consumes: sibling repositories `../ghostmoongpl` and `../ghostmoon/tools/include`.
- Produces: configure presets `macos-arm64` and `macos-universal`; build presets `macos-arm64-standalone`, `macos-arm64-all`, and `macos-universal-all`.

- [ ] **Step 1: Verify the remaining toolchain failure**

Run:

```bash
xcode-select -p
clang++ --version
git --version
cmake --version
```

Expected: Apple Command Line Tools, arm64 clang, and Git succeed; `cmake --version` fails with `command not found`.

- [ ] **Step 2: Install official CMake and verify the floor**

Install the current stable macOS Universal CMake application from the official CMake distribution into `/Applications/CMake.app`, then expose its command-line tools using CMake's documented command-line installation option.

Run:

```bash
cmake --version
xcrun --show-sdk-path
make --version
```

Expected: CMake reports version 3.22 or newer, `xcrun` prints a macOS SDK path, and GNU Make is available.

- [ ] **Step 3: Capture the current configure failure**

Run:

```bash
cmake -S . -B build/macos-arm64-preflight -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64
```

Expected: configuration fails because the committed build points `GHOSTMOON_OSS_DIR` at the old Windows path. Preserve the exact error in the task notes.

- [ ] **Step 4: Make dependency paths portable and fail early**

Replace the hard-coded Ghostmoon block with:

```cmake
get_filename_component(BREADBIN_DEFAULT_GHOSTMOON_OSS_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../ghostmoongpl" ABSOLUTE)
set(GHOSTMOON_OSS_DIR "${BREADBIN_DEFAULT_GHOSTMOON_OSS_DIR}"
    CACHE PATH "Path to the ghostmoon-oss repo")
if(NOT EXISTS "${GHOSTMOON_OSS_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "GHOSTMOON_OSS_DIR must contain ghostmoon-oss CMakeLists.txt: ${GHOSTMOON_OSS_DIR}")
endif()
add_subdirectory("${GHOSTMOON_OSS_DIR}" "${CMAKE_BINARY_DIR}/ghostmoon_oss-build")

get_filename_component(BREADBIN_DEFAULT_GHOSTMOON_TOOLS_INCLUDE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../ghostmoon/tools/include" ABSOLUTE)
set(GHOSTMOON_TOOLS_INCLUDE_DIR
    "${BREADBIN_DEFAULT_GHOSTMOON_TOOLS_INCLUDE_DIR}"
    CACHE PATH "Path to ghostmoon profiling/test tool headers")
if(NOT EXISTS
   "${GHOSTMOON_TOOLS_INCLUDE_DIR}/ghostmoon_tools/CpuProfileHarness.h")
    message(FATAL_ERROR
        "GHOSTMOON_TOOLS_INCLUDE_DIR must contain ghostmoon_tools/CpuProfileHarness.h: ${GHOSTMOON_TOOLS_INCLUDE_DIR}")
endif()
```

- [ ] **Step 5: Select formats by platform without changing plugin identity**

Immediately before `juce_add_plugin`, add:

```cmake
set(BREADBIN_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND BREADBIN_FORMATS AU)
endif()
```

Change only the format line to `FORMATS ${BREADBIN_FORMATS}`. Leave `PLUGIN_MANUFACTURER_CODE Estw`, `PLUGIN_CODE Bred`, and `BUNDLE_ID "com.ericsteenwerth.breadbin"` unchanged.

- [ ] **Step 6: Scope Windows definitions to Windows**

Remove `WIN32` from both unconditional definition lists. Keep the common definitions on all platforms, then use:

```cmake
if(WIN32)
    target_compile_definitions(Breadbin PUBLIC
        JUCE_ASIO=1
        WIN32
    )
endif()
```

After the existing unconditional `BreadbinIntegrationTests` definitions, add:

```cmake
if(WIN32)
    target_compile_definitions(BreadbinIntegrationTests PRIVATE WIN32)
endif()
```

Retain the existing `/FS` options under `if(MSVC)`. This placement matters because `BreadbinIntegrationTests` does not exist at the earlier Breadbin definition block.

- [ ] **Step 7: Add deterministic macOS presets**

Create `CMakePresets.json`:

```json
{
  "version": 3,
  "cmakeMinimumRequired": {
    "major": 3,
    "minor": 22,
    "patch": 0
  },
  "configurePresets": [
    {
      "name": "macos-base",
      "hidden": true,
      "generator": "Unix Makefiles",
      "binaryDir": "${sourceDir}/build/${presetName}",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Release",
        "CMAKE_EXPORT_COMPILE_COMMANDS": "ON"
      },
      "condition": {
        "type": "equals",
        "lhs": "${hostSystemName}",
        "rhs": "Darwin"
      }
    },
    {
      "name": "macos-arm64",
      "displayName": "macOS arm64 Release",
      "inherits": "macos-base",
      "cacheVariables": {
        "CMAKE_OSX_ARCHITECTURES": "arm64"
      }
    },
    {
      "name": "macos-universal",
      "displayName": "macOS Universal Release (Stage 2)",
      "inherits": "macos-base",
      "cacheVariables": {
        "CMAKE_OSX_ARCHITECTURES": "arm64;x86_64"
      }
    }
  ],
  "buildPresets": [
    {
      "name": "macos-arm64-standalone",
      "configurePreset": "macos-arm64",
      "targets": [
        "BreadbinLFOTests",
        "BreadbinMutationTests",
        "BreadbinIntegrationTests",
        "Breadbin_Standalone"
      ]
    },
    {
      "name": "macos-arm64-all",
      "configurePreset": "macos-arm64",
      "targets": [
        "BreadbinLFOTests",
        "BreadbinMutationTests",
        "BreadbinIntegrationTests",
        "Breadbin_All"
      ]
    },
    {
      "name": "macos-universal-all",
      "configurePreset": "macos-universal",
      "targets": ["Breadbin_All"]
    }
  ]
}
```

- [ ] **Step 8: Verify configuration and cache values**

Run:

```bash
cmake --list-presets
cmake --preset macos-arm64
rg 'CMAKE_OSX_ARCHITECTURES:STRING=arm64|GHOSTMOON_OSS_DIR:PATH=.*/ghostmoongpl|GHOSTMOON_TOOLS_INCLUDE_DIR:PATH=.*/ghostmoon/tools/include' build/macos-arm64/CMakeCache.txt
```

Expected: both macOS configure presets are listed, configuration completes, and all three cache entries match the arm64/sibling-repository design.

- [ ] **Step 9: Commit the portable configuration**

```bash
git add CMakeLists.txt CMakePresets.json
git commit -m "build: add portable macOS arm64 configuration"
```

### Task 2: Standalone-First Build and Bundle Validation

**Files:**
- Create: `cmake/ValidateMacOSBundle.cmake`
- Modify: `CMakeLists.txt:272-275`

**Interfaces:**
- Consumes: CMake variables `BUNDLE_PATH`, `BINARY_PATH`, and comma-separated `EXPECTED_ARCHS`.
- Produces: target `Breadbin_MacValidateStandalone`; exits nonzero on a missing bundle, missing architecture, invalid property list, or invalid signature.

- [ ] **Step 1: Build the Standalone checkpoint before adding validation**

Run:

```bash
cmake --build --preset macos-arm64-standalone --parallel
```

Expected: the build creates `build/macos-arm64/Breadbin_artefacts/Release/Standalone/Breadbin.app`. A source-level compiler failure at this checkpoint triggers `superpowers:systematic-debugging` before proceeding.

- [ ] **Step 2: Run the automated suites and record the mutation baseline**

Run:

```bash
ctest --test-dir build/macos-arm64 -R '^(LFOTests|IntegrationTests)$' --output-on-failure
build/macos-arm64/BreadbinMutationTests
```

Expected: LFO and integration tests pass. MutationTests exits nonzero only for the documented triangle-boundary survivor and reports a survival rate below 20 percent; any additional survivor is a regression.

- [ ] **Step 3: Write the failing Standalone validation call**

Append this Apple-only target to `CMakeLists.txt`:

```cmake
if(APPLE)
    string(REPLACE ";" "," BREADBIN_EXPECTED_ARCHS_CSV
        "${CMAKE_OSX_ARCHITECTURES}")
    add_custom_target(Breadbin_MacValidateStandalone
        COMMAND ${CMAKE_COMMAND}
            -DBUNDLE_PATH=$<TARGET_BUNDLE_DIR:Breadbin_Standalone>
            -DBINARY_PATH=$<TARGET_FILE:Breadbin_Standalone>
            -DEXPECTED_ARCHS=${BREADBIN_EXPECTED_ARCHS_CSV}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/ValidateMacOSBundle.cmake
        DEPENDS Breadbin_Standalone
        VERBATIM
    )
endif()
```

Run:

```bash
cmake --preset macos-arm64
cmake --build build/macos-arm64 --target Breadbin_MacValidateStandalone --parallel
```

Expected: FAIL because `cmake/ValidateMacOSBundle.cmake` does not exist.

- [ ] **Step 4: Implement focused bundle validation**

Create `cmake/ValidateMacOSBundle.cmake`:

```cmake
foreach(required_var BUNDLE_PATH BINARY_PATH EXPECTED_ARCHS)
    if(NOT DEFINED ${required_var} OR "${${required_var}}" STREQUAL "")
        message(FATAL_ERROR "${required_var} is required")
    endif()
endforeach()

if(NOT IS_DIRECTORY "${BUNDLE_PATH}")
    message(FATAL_ERROR "Bundle does not exist: ${BUNDLE_PATH}")
endif()
if(NOT EXISTS "${BINARY_PATH}")
    message(FATAL_ERROR "Bundle executable does not exist: ${BINARY_PATH}")
endif()

execute_process(
    COMMAND /usr/bin/lipo -archs "${BINARY_PATH}"
    RESULT_VARIABLE lipo_result
    OUTPUT_VARIABLE actual_archs
    ERROR_VARIABLE lipo_error
    OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT lipo_result EQUAL 0)
    message(FATAL_ERROR "lipo failed: ${lipo_error}")
endif()
string(REPLACE " " ";" actual_arch_list "${actual_archs}")
string(REPLACE "," ";" expected_arch_list "${EXPECTED_ARCHS}")
foreach(expected_arch IN LISTS expected_arch_list)
    list(FIND actual_arch_list "${expected_arch}" arch_index)
    if(arch_index EQUAL -1)
        message(FATAL_ERROR
            "Missing ${expected_arch} in ${BINARY_PATH}; found: ${actual_archs}")
    endif()
endforeach()

execute_process(
    COMMAND /usr/bin/plutil -lint "${BUNDLE_PATH}/Contents/Info.plist"
    RESULT_VARIABLE plutil_result
    OUTPUT_VARIABLE plutil_output
    ERROR_VARIABLE plutil_error)
if(NOT plutil_result EQUAL 0)
    message(FATAL_ERROR "Invalid Info.plist: ${plutil_output}${plutil_error}")
endif()

execute_process(
    COMMAND /usr/bin/codesign --force --deep --sign - "${BUNDLE_PATH}"
    RESULT_VARIABLE sign_result
    ERROR_VARIABLE sign_error)
if(NOT sign_result EQUAL 0)
    message(FATAL_ERROR "Ad-hoc signing failed: ${sign_error}")
endif()
execute_process(
    COMMAND /usr/bin/codesign --verify --deep --strict --verbose=2
            "${BUNDLE_PATH}"
    RESULT_VARIABLE verify_result
    ERROR_VARIABLE verify_error)
if(NOT verify_result EQUAL 0)
    message(FATAL_ERROR "Signature verification failed: ${verify_error}")
endif()

message(STATUS
    "Validated ${BUNDLE_PATH} with architectures: ${actual_archs}")
```

- [ ] **Step 5: Verify the native Standalone bundle**

Run:

```bash
cmake --preset macos-arm64
cmake --build build/macos-arm64 --target Breadbin_MacValidateStandalone --parallel
```

Expected: PASS and print `Validated ... Breadbin.app with architectures: arm64`.

- [ ] **Step 6: Launch and manually accept the Standalone checkpoint**

Run:

```bash
open build/macos-arm64/Breadbin_artefacts/Release/Standalone/Breadbin.app
```

Verify MIDI input, audio output, Retina UI scaling, preset save/load, one `.sid` load, and one `.wav` load. Stop for user confirmation if any check requires interactive hardware or listening judgment.

- [ ] **Step 7: Commit the Standalone checkpoint**

```bash
git add CMakeLists.txt cmake/ValidateMacOSBundle.cmake
git commit -m "build: validate native macOS standalone"
```

### Task 3: VST3 and Audio Unit Bring-Up

**Files:**
- Modify: `CMakeLists.txt` in the Apple-only validation block created by Task 2.

**Interfaces:**
- Consumes: `cmake/ValidateMacOSBundle.cmake` and JUCE targets `Breadbin_VST3` and `Breadbin_AU`.
- Produces: targets `Breadbin_MacValidateVST3`, `Breadbin_MacValidateAU`, and `Breadbin_MacValidateAll`.

- [ ] **Step 1: Add reusable validation-target creation**

Replace the single Standalone target with:

```cmake
if(APPLE)
    string(REPLACE ";" "," BREADBIN_EXPECTED_ARCHS_CSV
        "${CMAKE_OSX_ARCHITECTURES}")

    function(breadbin_add_macos_bundle_validation validation_target bundle_target)
        add_custom_target(${validation_target}
            COMMAND ${CMAKE_COMMAND}
                -DBUNDLE_PATH=$<TARGET_BUNDLE_DIR:${bundle_target}>
                -DBINARY_PATH=$<TARGET_FILE:${bundle_target}>
                -DEXPECTED_ARCHS=${BREADBIN_EXPECTED_ARCHS_CSV}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/ValidateMacOSBundle.cmake
            DEPENDS ${bundle_target}
            VERBATIM
        )
    endfunction()

    breadbin_add_macos_bundle_validation(
        Breadbin_MacValidateStandalone Breadbin_Standalone)
    breadbin_add_macos_bundle_validation(
        Breadbin_MacValidateVST3 Breadbin_VST3)
    breadbin_add_macos_bundle_validation(
        Breadbin_MacValidateAU Breadbin_AU)
    add_custom_target(Breadbin_MacValidateAll
        DEPENDS
            Breadbin_MacValidateStandalone
            Breadbin_MacValidateVST3
            Breadbin_MacValidateAU)
endif()
```

- [ ] **Step 2: Build and validate VST3 separately**

```bash
cmake --preset macos-arm64
cmake --build build/macos-arm64 --target Breadbin_MacValidateVST3 --parallel
```

Expected: `Breadbin.vst3` builds and validates as arm64 before AU compilation begins.

- [ ] **Step 3: Build and validate AU separately**

```bash
cmake --build build/macos-arm64 --target Breadbin_MacValidateAU --parallel
```

Expected: `Breadbin.component` builds and validates as arm64.

- [ ] **Step 4: Re-run aggregate build and automated tests**

```bash
cmake --build --preset macos-arm64-all --parallel
cmake --build build/macos-arm64 --target Breadbin_MacValidateAll --parallel
ctest --test-dir build/macos-arm64 -R '^(LFOTests|IntegrationTests)$' --output-on-failure
build/macos-arm64/BreadbinMutationTests
```

Expected: all bundles validate, LFO and integration suites pass, and mutation output matches the accepted one-survivor baseline.

- [ ] **Step 5: Commit the plugin-format checkpoint**

```bash
git add CMakeLists.txt
git commit -m "build: add macOS VST3 and Audio Unit validation"
```

### Task 4: Explicit Local Installation and Release Snapshots

**Files:**
- Create: `cmake/InstallMacOSBundles.cmake`
- Modify: `CMakeLists.txt` after the Apple validation block.

**Interfaces:**
- Consumes: `BREADBIN_STANDALONE_BUNDLE`, `BREADBIN_VST3_BUNDLE`, `BREADBIN_AU_BUNDLE`, `BREADBIN_RELEASES_DIR`, and `BREADBIN_USER_PLUGIN_ROOT`.
- Produces: install component `BreadbinMacLocal`, a `releases/<timestamp>_macos-arm64/` snapshot, and the two user-local installed plugin bundles.

- [ ] **Step 1: Add an install call before its script exists**

Add to the Apple-only CMake configuration:

```cmake
install(CODE "
    set(BREADBIN_STANDALONE_BUNDLE
        \"$<TARGET_BUNDLE_DIR:Breadbin_Standalone>\")
    set(BREADBIN_VST3_BUNDLE
        \"$<TARGET_BUNDLE_DIR:Breadbin_VST3>\")
    set(BREADBIN_AU_BUNDLE
        \"$<TARGET_BUNDLE_DIR:Breadbin_AU>\")
    set(BREADBIN_RELEASES_DIR \"${CMAKE_CURRENT_SOURCE_DIR}/releases\")
    set(BREADBIN_USER_PLUGIN_ROOT
        \"$ENV{HOME}/Library/Audio/Plug-Ins\")
    include(\"${CMAKE_CURRENT_SOURCE_DIR}/cmake/InstallMacOSBundles.cmake\")
" COMPONENT BreadbinMacLocal)
```

Run:

```bash
cmake --preset macos-arm64
cmake --install build/macos-arm64 --component BreadbinMacLocal
```

Expected: FAIL because `cmake/InstallMacOSBundles.cmake` does not exist.

- [ ] **Step 2: Implement preflight, snapshot, and user-local copying**

Create `cmake/InstallMacOSBundles.cmake`:

```cmake
foreach(bundle_var
        BREADBIN_STANDALONE_BUNDLE
        BREADBIN_VST3_BUNDLE
        BREADBIN_AU_BUNDLE)
    if(NOT DEFINED ${bundle_var} OR NOT IS_DIRECTORY "${${bundle_var}}")
        message(FATAL_ERROR
            "Cannot install: ${bundle_var} is missing: ${${bundle_var}}")
    endif()
endforeach()
foreach(path_var BREADBIN_RELEASES_DIR BREADBIN_USER_PLUGIN_ROOT)
    if(NOT DEFINED ${path_var} OR "${${path_var}}" STREQUAL "")
        message(FATAL_ERROR "Cannot install: ${path_var} is required")
    endif()
endforeach()

string(TIMESTAMP snapshot_stamp "%Y-%m-%d_%H%M%S")
set(snapshot_dir
    "${BREADBIN_RELEASES_DIR}/${snapshot_stamp}_macos-arm64")
file(MAKE_DIRECTORY "${snapshot_dir}")
file(COPY
    "${BREADBIN_STANDALONE_BUNDLE}"
    "${BREADBIN_VST3_BUNDLE}"
    "${BREADBIN_AU_BUNDLE}"
    DESTINATION "${snapshot_dir}")

set(vst3_dir "${BREADBIN_USER_PLUGIN_ROOT}/VST3")
set(au_dir "${BREADBIN_USER_PLUGIN_ROOT}/Components")
set(vst3_destination "${vst3_dir}/Breadbin.vst3")
set(au_destination "${au_dir}/Breadbin.component")
file(MAKE_DIRECTORY "${vst3_dir}" "${au_dir}")
if(EXISTS "${vst3_destination}")
    file(REMOVE_RECURSE "${vst3_destination}")
endif()
if(EXISTS "${au_destination}")
    file(REMOVE_RECURSE "${au_destination}")
endif()
file(COPY "${BREADBIN_VST3_BUNDLE}" DESTINATION "${vst3_dir}")
file(COPY "${BREADBIN_AU_BUNDLE}" DESTINATION "${au_dir}")

message(STATUS "Preserved Breadbin bundles in ${snapshot_dir}")
message(STATUS "Installed ${vst3_destination}")
message(STATUS "Installed ${au_destination}")
```

- [ ] **Step 3: Prove missing artifacts cannot replace installed plugins**

Run:

```bash
cmake -DBREADBIN_STANDALONE_BUNDLE=/private/tmp/missing-breadbin.app -DBREADBIN_VST3_BUNDLE=/private/tmp/missing-breadbin.vst3 -DBREADBIN_AU_BUNDLE=/private/tmp/missing-breadbin.component -DBREADBIN_RELEASES_DIR=/private/tmp/breadbin-release-test -DBREADBIN_USER_PLUGIN_ROOT=/private/tmp/breadbin-plugin-test -P cmake/InstallMacOSBundles.cmake
```

Expected: FAIL at preflight, with neither temporary destination created.

- [ ] **Step 4: Install after aggregate validation**

Run:

```bash
cmake --build build/macos-arm64 --target Breadbin_MacValidateAll --parallel
cmake --install build/macos-arm64 --component BreadbinMacLocal
```

Expected: one timestamped snapshot contains all three complete bundles; user VST3 and Components directories contain Breadbin only at their exact destination names.

- [ ] **Step 5: Validate installed AU metadata**

```bash
/usr/bin/auval -v aumu Bred Estw
```

Expected: AU validation completes successfully for Breadbin.

- [ ] **Step 6: Validate installed plugins in the two hosts**

In REAPER, rescan plugins, load both Breadbin VST3 and AU on separate instrument tracks, send MIDI, save the project, close it, reopen it, and confirm state restoration.

In FL Studio, run Plugin Manager's rescan, load Breadbin VST3, send MIDI, save the project, close it, reopen it, and confirm state restoration.

Record host versions and results in `STATE.md` during Task 5.

- [ ] **Step 7: Commit installation support**

```bash
git add CMakeLists.txt cmake/InstallMacOSBundles.cmake
git commit -m "build: install local macOS plugin bundles"
```

### Task 5: Documentation and Final Stage 1 Verification

**Files:**
- Modify: `README.md:57-74`
- Modify: `HOWTO.md:3-46`
- Modify: `HOWTO.md:77-82`
- Modify: `STATE.md:134-154`
- Modify: `ROADMAP.md:129-141`

**Interfaces:**
- Consumes: exact commands, artifact paths, test results, and host results from Tasks 1-4.
- Produces: a durable macOS arm64 build/install guide and an honest separation between verified Stage 1 and unverified Universal/public-release work.

- [ ] **Step 1: Update concise README entry points**

Document these macOS commands exactly:

```bash
cmake --preset macos-arm64
cmake --build --preset macos-arm64-standalone --parallel
cmake --build --preset macos-arm64-all --parallel
cmake --build build/macos-arm64 --target Breadbin_MacValidateAll --parallel
cmake --install build/macos-arm64 --component BreadbinMacLocal
```

Retain the existing Windows commands and licensing warning.

- [ ] **Step 2: Expand HOWTO with platform-specific procedures**

Add separate Windows and macOS prerequisites, macOS output paths, Standalone-first commands, automated-test commands, mutation-baseline interpretation, explicit install behavior, AU validation, and REAPER/FL Studio rescan steps. Describe the macOS user-preset location produced by JUCE's `userApplicationDataDirectory` rather than using the Windows `%APPDATA%` notation.

- [ ] **Step 3: Record measured status without overstating Universal support**

In `STATE.md`, replace `macOS: Not tested` with the exact successful arm64 targets and test counts from this run. Record Standalone, REAPER, and FL Studio manual results individually. If a manual check remains unconfirmed, label that exact check `Pending manual confirmation`; do not mark Stage 1 complete.

In `ROADMAP.md`, mark the macOS arm64 formats complete only if all applicable automated and manual checks passed. Keep Universal build, Developer ID signing, notarization, and installer work unchecked.

- [ ] **Step 4: Run final clean verification from the documented commands**

Run:

```bash
cmake --preset macos-arm64
cmake --build --preset macos-arm64-all --clean-first --parallel
cmake --build build/macos-arm64 --target Breadbin_MacValidateAll --parallel
ctest --test-dir build/macos-arm64 -R '^(LFOTests|IntegrationTests)$' --output-on-failure
build/macos-arm64/BreadbinMutationTests
/usr/bin/auval -v aumu Bred Estw
```

Expected: clean compilation; all three bundles validate as arm64; LFO and integration suites pass; mutation output matches the accepted baseline; AU validation passes.

- [ ] **Step 5: Review the final diff for scope and Windows preservation**

Run:

```bash
git diff --check
git diff --stat
git diff -- CMakeLists.txt CMakePresets.json cmake README.md HOWTO.md STATE.md ROADMAP.md
```

Confirm no source under `src/` changed, Windows retains VST3/Standalone and `JUCE_ASIO=1`, and no generated build or release bundle is tracked.

- [ ] **Step 6: Commit documentation and final status**

```bash
git add README.md HOWTO.md STATE.md ROADMAP.md
git commit -m "docs: document macOS arm64 build and validation"
```
