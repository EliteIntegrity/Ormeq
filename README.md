# Ormeq

**A free, single-player roguelike in a country built from stolen worlds.**

Nobody built this place. It was collected. Beneath its towns lie places taken
from elsewhere: courts, drowned cities, mountains and worse. Going deeper
means travelling further into what each place once was.

Choose a character, prepare in town, and venture into dangerous dungeons.
Find strange equipment, learn your enemies, and decide when to press on or
get out alive. Ormeq combines FrogComposband's deep character and combat
systems with a modern SDL3 interface and its own setting, shared with
**Ammerow: Lands Beyond**.

**[Download Ormeq for Windows x64](https://github.com/EliteIntegrity/Ormeq/releases/download/v0.1.3/Ormeq-0.1.3-windows-x64.zip)**
&nbsp; · &nbsp; [Release notes](RELEASE_NOTES.md)
&nbsp; · &nbsp; [Playing guide](PLAYING.md)

**Version 0.1.3 is the latest Windows x64 release.** Free to play, offline,
with no installer. Extract the whole ZIP to a writable folder and run
`ormeq-sdl3.exe`.

[![Outpost in Full Tile mode, with the player outside the castle and shops around the town square.](https://github.com/EliteIntegrity/Ormeq/releases/download/v0.1.0/ormeq-01-outpost-tiles.png)](https://github.com/EliteIntegrity/Ormeq/releases/download/v0.1.0/ormeq-01-outpost-tiles.png)

*Outpost: a place to prepare, find work and return with whatever you survived.*

## Take your time. Make it count.

- **Build a character worth risking.** Explore a wide range of races, classes,
  magic and equipment combinations inherited from FrogComposband.
- **Think through each turn.** Positioning, supplies, resistances and knowing
  when to retreat matter. Traditional roguelike permadeath makes survival count.
- **Explore beyond the next room.** Travel between towns, take on quests and
  descend through procedurally generated dungeons.
- **Make the view your own.** Choose ASCII, Hybrid or Full Tile mode, zoom the
  map, and adjust the theme. Sound effects, ambient soundscapes and title music
  are included in the playable download, with separate audio controls.

## Tiles or characters: your choice

Switch to Enhanced Square ASCII for crisp, readable layouts, or combine
character glyphs with artwork. Full Tile mode also offers **Keep ASCII floors
only**. Missing artwork always falls back to a glyph.

| Outpost in ASCII | A first encounter in The Sink |
| --- | --- |
| [![The castle, moat and town square in Enhanced Square ASCII mode.](https://github.com/EliteIntegrity/Ormeq/releases/download/v0.1.0/ormeq-02-outpost-ascii.png)](https://github.com/EliteIntegrity/Ormeq/releases/download/v0.1.0/ormeq-02-outpost-ascii.png) | [![An ASCII dungeon room with the player, a snake, a centipede and a floating eye.](https://github.com/EliteIntegrity/Ormeq/releases/download/v0.1.0/ormeq-03-dungeon-ascii.png)](https://github.com/EliteIntegrity/Ormeq/releases/download/v0.1.0/ormeq-03-dungeon-ascii.png) |

## Understand the encounter

Press **l** to look at a creature without losing sight of its position on the
map. Cards bring together its description, portrait and what your character
has learned. Unknown statistics stay unknown; **R** opens a larger card.

[![A metallic green centipede's character-art portrait and known statistics beside its position in the tiled dungeon.](https://github.com/EliteIntegrity/Ormeq/releases/download/v0.1.0/ormeq-04-creature-card.png)](https://github.com/EliteIntegrity/Ormeq/releases/download/v0.1.0/ormeq-04-creature-card.png)

Curious about the numbers? **The Reckoner** explains recorded combat with
actual dice rolls and a visual trail through the calculation. Its live panel
is optional. **Past Reckonings**, under **Ctrl+P**, lets you revisit recorded
results and inspect the working. It never changes the rolls, reveals hidden
inputs or presents a partial calculation as complete.

[![The optional Reckoner panel explains a dagger strike dealing three damage to a floating eye in The Sink.](https://github.com/EliteIntegrity/Ormeq/releases/download/v0.1.0/ormeq-05-reckoner.png)](https://github.com/EliteIntegrity/Ormeq/releases/download/v0.1.0/ormeq-05-reckoner.png)

## Work worth remembering

Meet the people offering work, read the objective and directions before you
accept, and keep track of the journey in the **Quest journal** (**Ctrl+Q**).
Active and completed lists, reward receipts and failure records help you pick
up where you left off.

| An offer from Count Uldrik | Your next step, kept in the journal |
| --- | --- |
| [![Count Uldrik offers the Thieves' Hideout quest with a portrait, briefing, directions and failure warning.](https://github.com/EliteIntegrity/Ormeq/releases/download/v0.1.0/ormeq-06-quest-offer.png)](https://github.com/EliteIntegrity/Ormeq/releases/download/v0.1.0/ormeq-06-quest-offer.png) | [![The quest journal shows active quests on the left and the accepted Outpost assignment's objective and contact on the right.](https://github.com/EliteIntegrity/Ormeq/releases/download/v0.1.0/ormeq-07-quest-journal.png)](https://github.com/EliteIntegrity/Ormeq/releases/download/v0.1.0/ormeq-07-quest-journal.png) |

*All seven images are actual screens captured from the Windows 0.1.0 game.
Click any screenshot to see it at full size.*

## Start playing

1. [Download the Windows ZIP](https://github.com/EliteIntegrity/Ormeq/releases/download/v0.1.3/Ormeq-0.1.3-windows-x64.zip)
   and extract everything to a writable folder, outside Program Files.
2. Run `ormeq-sdl3.exe` and choose **New Game**. Nothing extra needs installing.
3. Use the arrow keys or numeric keypad to move, **?** for help, and **Esc**
   for the pause menu and settings. The mouse wheel adjusts map zoom.

Before replacing an installation, back up `lib/save/`, `lib/user/` and
`lib/apex/` for your characters, preferences and scores.

[Read the playing guide](PLAYING.md) ·
[Report a bug](https://github.com/EliteIntegrity/Ormeq/issues) ·
[Browse releases](https://github.com/EliteIntegrity/Ormeq/releases)

## Source, credits and terms

Want to build or contribute? See [DEVELOPING.md](DEVELOPING.md) for the Windows
build, source layout and tests. The source runs with glyphs and synthesised
sound when the optional packs are absent.

The playable download includes the sound, music, tile and ASCII portrait packs.
Those packs are not in this source repository; source artwork and production
masters remain private. The screenshots above are release attachments.
See [LICENSE](LICENSE), [audio terms](licenses/Ormeq-Audio.txt) and
[component notices](licenses/) for the separate permissions.

Forked from [FrogComposband](https://github.com/sulkasormi/frogcomposband)
`v7.1.salmiak-63-g3d28f6b1` in August 2026. Its ancestry includes Moria, Umoria,
Angband, ZAngband, Hengband, Chengband, PosChengband and ComPosband, with
contributions from Posband and the Japanese Angband line. The original
authors' [credits](lib/file/credits.txt) and source notices are retained.

Ormeq development is by EliteIntegrity.

Ormeq retains the inherited **Angband not-for-profit licence**. It is not
offered under the modern Angband GPL licence.
