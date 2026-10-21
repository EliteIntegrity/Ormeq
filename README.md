# Ormeq

Ormeq is a single-player roguelike based on
[FrogComposband](https://github.com/sulkasormi/frogcomposband), with an SDL3
frontend and its own setting. It shares a world with Ammerow: Lands Beyond.

The frontend adds resizable windows, themes, mouse navigation, map zoom,
animated effects and optional portrait and tile artwork. The game retains
FrogComposband's extensive character choices and dungeon systems.

**Version 0.1.0 is the first Windows x64 release.**

## Build and play

The maintained build targets Windows x64. Install Visual Studio with the C++
desktop workload, a Windows SDK and CMake. Install Python 3 for the checks and
tests. The SDL3 development libraries are included in `third-party/`.

```powershell
./build.ps1 -Frontend sdl3
./ormeq-sdl3.exe
```

`build.bat` also builds and launches the game. Keep the executable beside
`lib/`, `font/` and the three SDL DLLs. Run `ormeq-sdl3.exe -h` for options.

The source distribution can run without `lib/art/` and `lib/tiles/`; it uses
glyphs when artwork is absent. Artwork supplied with a playable package has
separate terms in [LICENSE](LICENSE).

Playable packages include optional sound effects shared with Ammerow and
**Ormeq - Home Theme (Dark)**, which plays on the title screen. The WAV files
are excluded from public source; missing effects use Ormeq's original synth
and missing music stays silent. Effects and music have separate mute/volume
controls under **Settings > Sound**, also available from the title screen.
See [audio terms](licenses/Ormeq-Audio.txt).

Save files live in `lib/save/`, preferences in `lib/user/` and scores in
`lib/apex/`. Back these up before replacing an installation. The game needs a
writable directory; do not install it under Program Files.

Use **l** (Look) to browse nearby creatures: their known stat card opens
immediately. In SDL3 it stays at the left while the camera keeps the selected
creature visible beside it. **R** switches between the map card and a larger
fullscreen card. Direction keys and **+ / -** browse; **[ / ]** or the mouse
wheel scroll. The card also has clickable controls. **Esc** returns to play. Combat targeting (**\***) still
uses **r** for recall.

The **@** turns yellow at 50% health, orange at 25%, and red at 10%.
SDL3 also offers a static warning border for low health and harmful statuses.
Toggle it under **Esc > Settings > Warning border** (**b**); the choice is
saved. Red means low health, green poison, red-orange bleeding, violet
blindness/confusion/stun/paralysis/fear, and amber severe hunger. Low health takes
priority. The usual status labels remain available.

The map camera keeps the player and nearby squares clear of the fixed HUD,
including near level edges and when zoomed out. The HUD does not move aside.

**The Reckoner** records combat calculations: weapon and unarmed attacks,
spells, healing, and incoming damage. Open **Ctrl+P > Past Reckonings** for
history and exact working. The optional bottom-left panel has an
**Details** button; choose **Off**, **Static** or **Animated** under
**Esc > Settings > The Reckoner**. History works with the panel off. Spell damage
can show its original dice, bonuses, spell power, distance falloff and each
target's defences as one calculation, including actual resistance dice. Recording
uses the game's actual rolls; unknown inputs stay concealed and incomplete
formulas are labelled partial. Specialist spells and borrowed monster magic
are traced too; exact working includes saves and status changes. Press **?**
in Past Reckonings for a guide with worked examples and explanations of missing data.

Glyphs identify the source, weapon or spell and target. A ribbon follows the
recorded amounts through bonuses and defences, with optional sparks. History
offers a recent list and scrollable accounts with named results and explanations;
small screens open one view at a time. The visuals never roll or delay combat.

## Develop and test

```powershell
python tools/check_public.py
./build.ps1 -Test
```

The public data checks cover references, race and class rules, option help,
encoding, splash-screen alignment and help links. Gameplay tests cover seeded
play, save/load, menus, death handling and repeated sessions. Tests create
separate temporary installations and do not use your live saves.

See [DEVELOPING.md](DEVELOPING.md) for the source layout and further commands.
Only the Windows CMake build is maintained. The source distribution includes
the SDL development packages, fonts, tests and notices needed for that build.

See [RELEASE_NOTES.md](RELEASE_NOTES.md) for the first-release notes.

## Credits and terms

Forked from FrogComposband `v7.1.salmiak-63-g3d28f6b1` in August 2026.
Its ancestry includes Moria, Umoria, Angband, ZAngband, Hengband, Chengband,
PosChengband and ComPosband, with contributions from Posband and the Japanese
Angband line. The original authors' credits are retained in
[lib/file/credits.txt](lib/file/credits.txt) and the source headers.

Ormeq development is by EliteIntegrity with AI coding assistance. Portrait source
images were generated using AI image tools and converted to character-art
plates. See LICENSE for the artwork terms and production description.

Ormeq retains the inherited Angband not-for-profit licence; it is not offered
under the modern Angband GPL licence. See [LICENSE](LICENSE) and
[licenses/](licenses/) for the applicable notices and component terms.
