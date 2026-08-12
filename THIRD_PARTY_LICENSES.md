# Third-party licence record

Breadbin is distributed under `AGPL-3.0-only`. This record identifies the
third-party software combined with the project and the attribution material
that must accompany source and binary distributions.

| Dependency | Upstream and revision | Licence | Reuse mode | Files inspected |
|---|---|---|---|---|
| JUCE | [`juce-framework/JUCE`](https://github.com/juce-framework/JUCE) [`f8f8864172464b9adf9eba6101e1f784838d1597`](https://github.com/juce-framework/JUCE/tree/f8f8864172464b9adf9eba6101e1f784838d1597) | `AGPL-3.0-only` (the option selected by Breadbin) | Dependency, via CPM | `LICENSE.md`, `CMakeLists.txt`, `BREAKING_CHANGES.md`, and JUCE's ASIO module headers |
| libsidplayfp / reSIDfp | [`libsidplayfp/libsidplayfp`](https://github.com/libsidplayfp/libsidplayfp) [`v2.16.0` / `3fe864b4bfbfd5c6fb947d9685e2346ea719878d`](https://github.com/libsidplayfp/libsidplayfp/tree/3fe864b4bfbfd5c6fb947d9685e2346ea719878d) | `GPL-2.0-or-later`; Breadbin uses the GPLv3-or-later option | Directly compiled source, via the release tarball | `COPYING`, `src/builders/residfp-builder/residfp/SID.cpp`, `src/player.cpp`, `src/sidplayfp/sidplayfp.cpp`, and the CMake source list |
| melatonin_blur | [`sudara/melatonin_blur`](https://github.com/sudara/melatonin_blur) [`v1.4` / `374d39bf79ae2833af18c89b310760c3e5ee0903`](https://github.com/sudara/melatonin_blur/tree/374d39bf79ae2833af18c89b310760c3e5ee0903) | `MIT` | Dependency, via FetchContent | `LICENSE` and the CMake integration |
| ghostmoon-oss | External source configured by `GHOSTMOON_OSS_DIR` | `LGPL-2.1-or-later` (`dsp`), `MIT` (`core`, `ui_synthwave`) | Dependency, via `add_subdirectory` | `README.md`, `dsp/include/ghostmoon/ReverbSC.h`, and the CMake target use |

## Distribution requirements

- Provide the complete corresponding source for the exact Breadbin release,
  including build scripts and all local modifications to the dependencies.
- Include the GNU AGPLv3 text, libsidplayfp's GPL notice, and the MIT/LGPL
  notices and licence texts required by the dependencies in every source and
  binary distribution.
- Do not use the JUCE commercial EULA for this combined work. Breadbin selects
  JUCE's AGPLv3 option so that its terms are compatible with the directly
  compiled GPL-2.0-or-later SID engine.
