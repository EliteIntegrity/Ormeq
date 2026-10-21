# Playing Ormeq

Extract the whole package to a writable folder and run `ormeq-sdl3.exe`.
The SDL libraries and fonts are already included. Nothing extra needs installing.

Choose New Game on the opening screen. Use the keyboard or mouse to select a
character. In the game, `?` opens help and Escape opens the pause menu. Arrow
keys and the numeric keypad move; the mouse wheel changes map zoom.

New character preserves existing saves. If a suggested or typed name is already
in use, an unused numeral is added, such as Hyddyddin II. Check the name shown
before Begin Play. Continue and Load keep the selected character's savefile.

Press **Ctrl+Q** for the **Quest journal**. Active quests awaiting a reward
are labelled **Ready to return**; finished quests move to Completed.
Select a quest to read its objective, progress, briefing and return contact.
Small screens open details separately, with Escape returning to the list.
Town quest offers have **A: Accept / Esc: Later** cards; turn-in shows the
actual reward. Completed town quests keep that receipt and the questgiver's
response after saving and loading. Older turn-ins say "Reward not recorded".
Failed quests keep the known failure reason and, after reporting back, the
questgiver's actual closing response. Older missing details say "not recorded".
The journal remembers your last tab and selected quest in each list, including
after saving and loading. If that quest leaves the list, its first entry is selected.
Outpost, Telmora and Morivant's castle quests include concise objectives and
directions from their briefings. Compact cards put the next step and objective
before the portrait. Bulin's snake quest requires recovering the lost whip;
his other castle quests require clearing their enemies. Their cards distinguish
loot kept from the site from a separate reward given at turn-in.
Hjolgar's Fighters' Hall hunts show the actual target, count and dungeon level,
including replacement dungeons. Only kills on the active quest floor count.
Paliro's Thieves' Guild jobs explain which room to clear and where to return;
their journal never lists undiscovered inhabitants or hidden treasure.
Colmanwen's Angwil quests distinguish clearing a site from destroying one
particular source of danger. Their directions follow the actual quest variant
and town mode, including the quarry's different location in small-town games.
Objective completion stays in the messages during play.
Missing portraits use glyphs. Reading cards and the journal costs no turns.
Press `?` in the journal for controls, failure warnings and information limits.

**The Reckoner** explains recorded attacks, spells, healing, saves and status effects.
Press Ctrl+P and switch to **Past Reckonings**, or click **Details** on the live panel.
The history lists recent accounts by source and target. Click an entry or use Up/Down and Enter to open it. Its #number is a stable
identifier, not a shortcut. Wide screens show the list beside the account;
small screens use `L` to switch views. `n` / `p` select older / newer accounts,
`]` / `[` select their named results, and arrows or Page Up/Down scroll the
account. `e` opens exact working (`|` exports text or HTML); `r` replays the
visual explanation without rerolling. Results from a turn share an account (busy turns continue in another block);
the history holds 48 accounts this session.

The optional live panel stays at bottom left. Escape > Settings > The Reckoner
(`c`) cycles Off (the default), Static and Animated. Click its top-right **Details**
button to inspect the displayed result directly. The panel selects the latest
account's last resolved result; history exposes its other results too. The location is beneath the
right-hand stats. Small windows automatically use a compact result card;
Details still opens the full working. Escape also offers Past Reckonings and
Current effects, including any that do not fit the sidebar. Menus and full Look cards temporarily hide the live panel.

Glyphs show the source, weapon or spell, and recipient as they appeared at the
time. A ribbon grows or shrinks with the recorded amounts; its thickness uses
that account's scale, not remaining monster HP. Animated sparks follow the
settled values without holding up play. Their count does not represent damage.
The dice marker shows the actual sum against its range and average, not a
probability. The account explains changes and statuses alongside the numbers;
exact working retains every recorded formula and its integer rounding.

Press `?` in Past Reckonings for **The Reckoner guide**, also available from the
main help index. It contains a worked spell example and explains dice, rounding,
damage versus effect power, saves, status counters, healing limits and missing
information. A status counter is not a promise of that many player turns.

Specialist class and borrowed monster spells carry their explicit source into
target calculations. Secondary checks show the actual comparison and observed
outcome. Unexplained adjustments retain their before/after boundary and remain
labelled partial. A complete damage chain does not mean every possible side
effect is recorded. Hidden equipment and creature knowledge stay hidden, even
when inspecting an old record after learning more.
History clears when a game is started or loaded; it is not saved with the
character. Browsing, exporting and replaying calculations cost no turns.

Sound effects can be adjusted under **Esc > Settings > Sound**. Choose the
sound pack (default) or the original synth, set the volume, or mute effects.
The pack uses effects shared with Ammerow; missing or damaged effects fall
back to the original synth. Rapid repeated cues are limited and other sounds
overlap, so a busy turn does not leave a delayed queue of noises. Audio does
not change combat, turns, or random rolls.

Bow shots and throws have separate release and contact sounds. Contact is
generic: it does not identify an unseen creature or reveal its toughness.
Common ailments sound when they begin and when they clear; timer ticks and
extensions stay silent. Buffs use a quieter, neutral cue when they expire.
Pickups, equipping, quiver loading, completed trades, stairs and successful
disarming also have cues. Cancelled actions do not play a success sound.

Footsteps are deliberately subtle and can be switched off separately, as can
menu sounds. Holding a direction against a wall sounds one bump; a fresh
physical press allows another. Menu navigation, selection, document scrolling,
inventory tabs and Past Reckonings have quiet feedback. Shops, homes, museums
and service buildings sound on entry and exit; opening chests and unlocking
doors/chests also have cues. Successful book spells have a generic casting
cue, including utility spells; cancelled casts stay silent.

**Settings > Sound > Ambience** controls the background separately (25% by
default). The playable package includes two minute-long recordings for each
of surface day, surface night and five dungeon depth bands (1-20, 21-40,
41-60, 61-80 and 81+). They loop with blended seams and fade between locations.
Menus lower ambience to one quarter of its chosen level; losing window focus
pauses it. Source builds, missing recordings and the original-synth setting
use a quiet generated background. Ambience reveals no unseen creatures or
undiscovered features. Muting effects leaves ambience and title music alone;
each has its own switch.

Optional title music has separate on/off and volume controls under
**Settings > Sound > Title music**, available from the title screen too.
It loops on the title screen, fades out when you start or load a game, and
pauses while the window is unfocused. Muting effects does not mute music.
If no track is installed, it stays silent and the menu says **Not installed**.
Official playable packages include **Ormeq - Home Theme (Dark)**. The source
distribution omits the music file.

Characters are stored in `lib/save`, preferences in `lib/user`, and scores in
`lib/apex`. Keep those folders when moving or upgrading an installation.

Do not copy the new release over a running game. Keep the supplied data, fonts,
DLLs, LICENSE and the `licenses` directory together.

This is the Ormeq 0.1.0 package for Windows x64.

Map presentation is selected in Settings: ASCII, Hybrid or Full Tile. ASCII
offers Classic/Square grids and optional Enhanced Square walls. Hybrid and
Full Tile offer 32/64-pixel artwork, with glyphs for missing images. See
[map presentation](docs/map-presentation.md).
