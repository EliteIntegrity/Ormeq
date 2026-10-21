# Developer tools

Run `python tools/check_public.py` for the public data checks. It requires only
Python's standard library and can run before compiling the game.

| Tool | Checks or purpose |
|---|---|
| `check_refs.py` | Monster, artefact, dungeon and quest-file references |
| `check_identity_rules.py` | Race/class identities in preferences and help |
| `check_objects.py` | Object references in rewards and layouts |
| `check_options.py` | Option descriptions |
| `check_encoding.py` | Source and data text encoding |
| `check_layout.py` | Splash-screen sidebar alignment |
| `check_links.py` | Help link files and anchors |
| `check_release_metadata.py` | Public version in README and release notes |
| `test_game.py` | Gameplay regressions using the headless executable |
| `test_quest_ui.py` | Headless SDL quest layouts, portraits and first-click navigation |
| `test_map_ui.py` | Headless SDL map, Look cards and renderer-reset regressions |
| `test_menu_ui.py` | First wait, hover, click and submenu-return regressions |
| `game_runtime.py` | Temporary runtime copies for automated tests |
| `soak.py` | Longer seeded gameplay runs; see `--help` |
| `make_icon.py` | Rebuild the application icon |

Build the headless executable before `test_game.py` or `soak.py`, or use
`./build.ps1 -Test` to build and run regressions together.

`test_quest_ui.py --clicks-only` checks the first mouse input after opening the
journal in every map mode at wide and compact sizes. SDL capture scripts use
`%!` for one normal idle event pass without a key, allowing the same repaint
that happens while waiting for a player. Click results must match keyboard
results and preserve gameplay state and random rolls.

`test_menu_ui.py` covers title and character creation, settings, inventory,
knowledge, messages, Past Reckonings, buildings and quest offers. It also checks
that keyboard-only pages and the map do not retain stale clickable controls.
Item, spell and power choices must cover the map and HUD completely. The
Reckoner checks compare actual Details-button pixels between command and
direction prompts, exercise its input guard and compare movement/RNG with the
live display off, static and animated.
`--case birth-navigation` checks selection resets across birth pages and nested
returns, race/class previews, arrow/Right input, and conditional Quick Start
and patron hotspots. `test_game.py` also checks that Return sounds occur only
when birth accepts the key, including blocked stats and split personalities.
Five specialist casting implementations are also exercised using characters
made through normal birth, checking cancellation and a clean map return.
The default matrix uses ASCII, Hybrid and compact Full Tile displays. Use
`--case NAME` to select a route and `--keep DIRECTORY` to retain screenshots
and click geometry. `%&x,y;` replays pointer motion through the normal SDL
handler, just as `%@x,y;` replays a click. Coordinates are grid-relative pixels.
Each capture restores all mutable test files, including load counters. The
animated title compares text/colours and click bounds; other clickable screens
also compare pixels. These tests use private runtime copies and dummy drivers.

Checks should report what they examined and identify failures precisely. A
passing automated check establishes only the conditions it tests.
