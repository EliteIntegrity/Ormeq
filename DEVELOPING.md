# Developing Ormeq

## Supported builds

Use Windows x64, Visual Studio's C++ desktop workload and Windows SDK, CMake
3.20 or newer, and Python 3. The build script finds CMake on PATH or in Visual
Studio. SDL3 packages are vendored; no sibling project directory is required.

| Frontend | Command | Output |
|---|---|---|
| SDL3, primary | `./build.ps1 -Frontend sdl3` | `ormeq-sdl3.exe` |
| Headless tests | `./build.ps1 -Frontend none` | `ormeq-none.exe` |
| Legacy Win32 | `./build.ps1 -Frontend win32` | `ormeq.exe` |

The legacy Win32 port is retained for comparison. Its inherited bitmap fonts
are not supplied in the source candidate; use SDL3 for normal play.

`-Fresh` recreates the selected build directory. Moving the project also
invalidates old CMake caches; the script detects this. `-Wizard` enables debug
commands, which are off by default. `-NoCheck` skips data checks explicitly.
Python is optional for compiling, but required for `-Test`.

## Tests

```powershell
python tools/check_public.py
./build.ps1 -Frontend sdl3 -Test
python tools/soak.py --help
```

`check_public.py` runs the public data checks and fails if any fails.
`test_game.py` contains the gameplay regression suite. `game_runtime.py`
provides disposable copies for tests, including generated help and cache files.
The soak runner supports broader seeded runs; consult its help for the current
options. No third-party Python packages are needed for these tests.

The quest fixture (`-K0/1` in the headless executable) compares the actual
accept/complete/reward/fail lifecycle without and with repeated journal reads.
It checks hidden entries, dialogue stages, town-mode contacts, state and RNG.
`tools/test_quest_ui.py` uses a disposable `-K2` save and SDL's dummy video
driver for journal/card layouts, navigation and renderer reset readbacks.
Use `--keep DIRECTORY` to retain captures. No live saves are loaded or changed.
`-K3` creates a disposable receipt save; `-K4` inspects it and `-K5` resaves it.
`-K6` inspects each session while exercising Continue/New in one process.
The runtime suite checks exact reward/response retention, missing historical
receipts, the old save layout (including early Ormeq version stamps), damaged
receipt data, and new-character isolation. SDL readbacks also reopen a receipt
after a real town turn-in, including the small-screen layout.

The SDL3 build also creates `ormeq-sound-tests`; `-Test` runs it through
`tools/test_sound.py`. Generated PCM fixtures exercise the actual decoder,
mixing arithmetic, volume, fallback, cache eviction and game RNG isolation
using SDL's dummy device. No optional audio or speakers are needed. If the
pack is installed, every mapped variant is decoded as an additional check.

The same command runs `ormeq-music-tests` through `tools/test_music.py`.
Generated 44.1/48 kHz mono/stereo PCM checks sample arithmetic, loop edges,
fades, independent music/effects mute, focus pause, shutdown/reopen, invalid
and truncated files, callback stress and unchanged game RNG state.

`test_audio_events.c` drives the real status setters, projectile paths and
interaction commands in the headless build. Its `-A2` (muted) and `-A3`
(events enabled) runs must produce identical gameplay and RNG traces.
`-A0/1` selects mute/events without the fixture, for other scripted tests.
The gameplay suite also compares 800 specialised ranged attacks per seed
with sounds enabled and disabled. These tests require no audio assets.

`src/ui_sound_map.h` maps game events to optional WAVs under
`lib/xtra/sound/pack/`. The public `pack-manifest.json` alongside that folder
records their hashes. `python tools/check_audio.py --require-pack` verifies a
complete playable pack. Source builds work with no pack. The WAVs and their
production tools must stay outside public source/history. To listen to all
events, run `ormeq-sdl3.exe -S`; use `-S0` for the original synth. Paths resolve
beside the executable, regardless of the working directory.

Title music is optional SDL3 presentation code in `src/ui_music.c`. Its
separate stream reads `lib/xtra/music/pack/title.wav`: PCM16 WAV, mono/stereo,
44.1 or 48 kHz, at most 10 minutes and 128 MiB. No decoder dependency is added.
The optional track is Ormeq - Home Theme (Dark); its SHA-256 and size are in
`lib/xtra/music/pack-manifest.json`. Keep the master and production files
outside source. Track replacements need a new hash/size and playable-inventory
review. The public tests generate their own audio; if the track is installed,
they also decode and mix the complete delivered WAV through the real SDL code.
See `lib/xtra/music/README.txt`.

Run the checks after data edits. Run the gameplay tests after changes to game
state, save handling or input. Exercise visible SDL3 changes in the actual
window: headless tests cannot verify rendering or mouse behaviour.

## Source map

| Path | Responsibility |
|---|---|
| `src/` | Game rules, save format, terminal layer and frontends |
| `src/main-sdl3.c`, `src/sdl3_*.c` | SDL3 window, input and rendering |
| `src/main-none.c` | Scriptable headless frontend |
| `src/ui_*.c` | Shared display models and frontend event interfaces |
| `src/z-term.c`, `src/z-doc.c` | Terminal cells and formatted documents |
| `lib/edit/` | Monsters, objects, quests, dungeons and body templates |
| `lib/help/` | Help markup and generated text copies |
| `lib/pref/`, `lib/theme/` | Preferences, identity rules and colour themes |
| `font/`, `third-party/`, `licenses/` | Bundled dependencies and their notices |

More detail: [architecture](docs/architecture.md) and
[data formats](docs/data-formats.md).

Race/class numbers are persistent identities, used by rules and saves. Display
names are also used in some data references. Preference expressions replace
spaces with hyphens (`Dusk-Elf`); the UI name remains `Dusk Elf`. Use the
identity checker and tests when changing names or availability.

Generated help must agree with its source. Tests generate it in their temporary
copies. After changing character descriptions, review the regenerated help
before including it in a change.

## Release metadata and CI

`ORMEQ_VER` in `src/defines.h` is the public version. CMake reads it directly,
and the public checks verify README and release notes against it. The inherited
`VER_*` fields identify the save format and must not be changed for marketing.
`VERSION_IS_DEVELOPMENT` remains true until the release is approved.

`.github/workflows/build.yml` runs the public checks, SDL3/headless regression
build and legacy Win32 build on pushes and pull requests. It uses pinned actions
and read-only repository permissions; it does not publish packages.
It also runs Debug gameplay tests and checks MSVC assembly stack frames:

```powershell
cmake -S . -B out-none -DFROG_FRONTEND=none -DORMEQ_STACK_AUDIT=ON
./build.ps1 -Frontend none -Config Debug -NoCheck -Test
python tools/check_stack_frames.py out-none/frames
```

The guard rejects frames above 64 KiB, with a narrow allowance for the inherited
save decoder's fixed buffer. Clang/GCC builds also warn and fail on oversized
frames outside that decoder. Debug tests list all magic-realm spell menus,
exercise lore-off saves and private HP outcomes, and test provenance expiry.
Test entry points in game modules are compiled only with `RECKONER_TEST`.

## Contributions

Explain the observed problem, intended behaviour and relevant checks. Keep
changes focused, preserve upstream copyright notices, and state the origin and
terms of contributed code or assets. Existing code and artwork have different
terms; consult LICENSE before reusing either. Do not copy material from another
project merely because its terminology fits the setting.

Report reproducible bugs with the version, frontend, Windows version and steps.
Share save files only after checking their character names and other contents.

Optional map sprites use `tools/build_sprite_pack.py`; see
[map presentation](docs/map-presentation.md) for recipes, fallbacks and tests.
The SDL3 `-Test` build runs the synthetic tile-loader and hidden map-rendering
checks. Converter tests additionally require Pillow and NumPy.
