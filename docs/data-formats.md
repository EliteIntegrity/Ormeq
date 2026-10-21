# Data formats

Where the game's content lives, what shape it is in, and which parts are still
trapped in C.

**Most of `lib/edit` documents itself.** Every one of those files opens with a
comment block explaining its own line codes — `r_info.txt` spends **128 lines**
on this before its first record. **Read the file's own header first**; this
document does not duplicate them. What it does cover is the map, the traps, and
the formats *we* invented, which are written down nowhere else.

---

## Where content lives

| path | what | size |
|---|---|---|
| `lib/edit/*.txt` | the game's content — 109 files | see below |
| `lib/theme/*.thm` | 10 colour themes | ours |
| `lib/art/**/*.art` | 631 portrait plates, binary | ours |
| `lib/tiles/` | sprite sheets + name→cell indexes | ours |
| `lib/pref/*.prf` | keymaps, font and graphics preferences | inherited |
| `lib/help/` | in-game help — **regenerated on startup** | inherited |
| `lib/file/` | flavour text, death messages, `credits.txt` | inherited |
| `lib/data/*.raw` | parsed binary cache — **generated, gitignored** | — |
| `lib/apex/` | scores and character dumps — **gitignored** | — |

The big content files:

| file | lines | holds |
|---|---|---|
| `r_info.txt` | 25,466 | monsters (1,389) |
| `m_info.txt` | 6,703 | **class magic** — spell books, realms, and each spell's level / mana / fail / exp |
| `k_info.txt` | 4,614 | object kinds (536) |
| `a_info.txt` | 4,019 | artefacts (386) |
| `f_info.txt` | 1,768 | terrain features |
| `b_info.txt` | 1,182 | **body templates** for player monster races and the possessor |
| `e_info.txt` | 1,144 | ego items |
| `d_info.txt` | 481 | dungeons (44) |
| `q_*.txt` | 81 files | one quest each, plus `q_info.txt` indexing them |
| `t_*.txt` | — | town layouts |

Two of those names mislead, and both have caught me out: **`m_info` is not
monsters** (that is `r_info`) and **`b_info` is not buildings or shop owners**
— it is body slot templates, referenced from `r_info` by `P:Body:<name>`.

---

## The `lib/edit` line format

Every file is line-oriented. A line's first character is its record code,
followed by `:`-separated fields. `#` is a comment. Blank lines separate
records.

The codes differ per file — `r_info.txt`'s `N:` is a serial number and a name,
`B:` an attack method — but three conventions hold everywhere:

- **`N:`** opens a record.
- **`D:`** is prose the player reads.
- **ALL-CAPS tokens are parser keywords**, not prose. `AMBERITE` in `r_info.txt`
  is a monster flag; `KILL(cyclops)` in `q_info.txt` is a goal.

### Three traps

**1. Prose is one `D:` line per *screen* line.** A phrase can span several
records. Join consecutive description lines when searching for complete
phrases; a line-by-line search can miss them.

**2. ALL-CAPS tokens are parser identifiers.** Preserve them when editing prose.

**3. `lib/data/*.raw` is a generated cache** of the parsed text. It is
gitignored and rebuilt when the text changes. Never edit it, and never diff it.

### Referential integrity is checked, not hoped for

Quest rewards, quest goals, `OBJ()` references, tile names and portrait names
all point at things by *name*, and nothing in the parser notices when a name
stops resolving — you get a quest that hands out nothing, silently.

Run `python tools/check_public.py` after editing `lib/edit`. These checks cover
references, objects, identity rules and optional media mappings. The normal
build also runs data checks when Python is available.

---

## Quest journal metadata and receipts

`lib/edit/quest_contacts.txt` maps `Q:` quest-id lists to
`C:name|building|town`. Conditional town-mode sections follow the actual town
plans, including substitute quests. It supplies display information only.
An optional `P:portrait name` after `C:` selects a stable artwork name when a
town mode shortens the speaker's name. It defaults to the `C:` name. Missing
or unreadable artwork keeps the speaker's glyph; portraits ship separately
from the public source.

`lib/edit/quest_briefings.txt` provides concise journal guidance for all towns,
main-story entries and the retained legacy Killing Fields commission.
`F:` selects the actual quest filename; `O:` is the objective and `D:` the
directions. Summaries must use only information already disclosed in that
quest's briefing. Match the substitute's file, not its original quest id.
The same summaries follow these quests when small town modes reassign them
to an Outpost questgiver; contact metadata still supplies the actual speaker.
Kill counts come from the quest state; clear-site quests never enumerate
undiscovered enemies. No map or reward directives are evaluated here.
Morivant's eight castle variants include a recovery quest (the lost whip) and
seven clear-site quests. Their summaries follow the actual variant; the
Underground City also follows its existing reassignment to Uldrik in Lite mode.
Dialogue retains the existing race-dependent Underground City reward branches.
Fighters' Hall guidance leaves `O:` unset so targets and counts come directly
from the quest state. The journal resolves dungeon locations through the same
read-only lookup as quest activation, including suppressed dungeons and modes
without wilderness. The seven hunt identities include three older quests retained
for save compatibility. Thieves' Guild guidance also follows Lardbottom's
small-town assignments. The nine guild files change dialogue only; their maps,
conditions and reward branches are unchanged.
Angwil's nine files include seven clear-site missions and two single-target
missions. Target summaries describe only the presence disclosed in the briefing,
not the hidden monster identity. `q_pit2.txt` directions use the existing
`$WILDERNESS` condition: south of Angwil normally, forest northwest of Outpost
without wilderness. Guidance follows the actual file and the existing Odwin,
Uldrik and Kimuk assignments; it must not assume Colmanwen is always the contact.

Player save format **7.1.4.7** adds town receipts after the existing payload,
before the two checksums. The `OQRC` block contains a u16 block version (1),
a u16 record count, then each quest's s16 id, u32 collection turn, reward text
and closing response. Each text field is a u16 byte count followed by those
bytes, capped at 8192. The receipt uses the description of the reward actually
awarded and the response actually delivered, never a second reward roll.
Only finished, visible town quests are serialized. Readers validate the block
version, count, identities and text lengths before accepting it.

Older player saves have no block and remain supported; no historical receipt
is invented. The existing early `0.1` stamp repair remains in place. New saves
require a build that understands the receipt block. Temporary floor files keep
their existing payload. Receipts are released with their quest on cleanup, so
starting another character cannot retain the previous character's receipts.

Player save format **7.1.4.8** appends an `OQJR` block after `OQRC`, still before
the checksums. It starts with u16 version (1) and u16 failure-record count. Each
record stores s16 actual quest id, byte reported flag (0/1), u32 reporting turn,
reason text and response text (the same bounded encoding as receipts). The
failure date and character level remain in the original quest payload.
Only visible failed quests are written. A response is saved only for a reported
town failure; unreported records have an empty response and zero reporting turn.
Readers reject bad versions/counts, unknown or duplicate ids, invalid flags,
inconsistent reports, oversized text and truncated data.

After the failure records: byte last journal tab (0 active, 1 completed, 2 failed)
and three s16 selected quest ids (zero for none). Selection is resolved against
visible lists on opening, so sorting cannot change its identity and removed or
concealed entries fall back to the first visible entry. A missing Failed tab
falls back to Active. Empty lists remain valid. Version 8 did not save scroll positions.

Version-7 saves keep their reward receipts and start with no failure records
and default journal selection. Earlier saves also remain readable. Missing
historical causes/responses are never reconstructed from today's dialogue.
An old unreported failure can gain a response when actually reported, with an
unknown cause. Records and selection are cleared on character creation/load;
these fields never drive quest rules, reward generation or penalties.

Player save format **7.1.4.9** writes `OQRC` version 2 with the same record
encoding, now also allowing finished, visible non-town quests. Their reward
text contains only floor drops observed at completion and their response field
contains observed stat increases. `drop_near` calls a scoped observer after
successful placement (after normal visibility updates) or merging into a
currently visible, known floor stack. It uses ordinary
known object descriptions, excludes blindness/hallucination/currently unseen
grids, and records only the delivered quantity, not a pre-existing stack.
Failed deliveries add nothing; a partial merge remains recorded even if
placing the remainder fails. The observer never rolls, identifies, collects or
changes an object. Old versions still read their original version-1 block.

After the unchanged-layout `OQJR`, version 9 adds `OQRM`: u16 version (1),
u16 count (at most 160), then s16 identity, u16 detail top line and u16 text
width per entry. Zero/unknown/duplicate identities and widths outside 1..1000
are rejected. Positions are clamped against the current document; changed
width resets the top because the text has rewrapped. New/load clears memory.

Negative journal identities -1..-30 denote Karrot's existing assignment slots;
-31..-45 denote Troika's. They are permitted in v9 selection/scroll memory only,
never in gameplay quest maps, receipts or failures. Read-only adapters expose
completed assignments from the already-saved patron arrays. Active assignment
82 stays a normal quest. No patron gameplay serialization changes; old saves
therefore supply their histories without invented reward or punishment data.
Finished/failed history rows are borrowed UI snapshots, not game quests.

Player save format **7.1.4.10** appends `OQPH` after `OQRM`: u16 version (1),
u16 count (at most 45), then s16 patron history identity and four length-prefixed
texts per entry: known failure reason, observed drops, visible message transcript,
and gifts earned on that assignment. Each text uses the same 8192-byte bound
as receipts. Unknown, duplicate or uncompleted assignment identities, invalid
versions, overlong fields and truncated records are rejected. New/load clears
these observations, and v9 or earlier loads leave them absent rather than
inventing them. Existing class quest arrays and gameplay payload are unchanged.

The observer is scoped to an existing patron completion/failure handler. It
copies only messages already permitted by `cmsg_print`, visible drops through
the shared reward observer, the known failure cause and the actual Troika
milestone reached. Full messages are appended within the text bound; overflow
is marked and further messages omitted. It never rerolls or reconstructs a
reward, reveals internal curse flags, changes knowledge, or collects an item.
Reused active assignment 82 clears only presentation records and scroll memory.

All town contacts, including `P:` aliases in small-town modes, have reviewed
optional portraits in the playable pack. Private production metadata records
the prompts and source/plate hashes; runtime export checks require complete
contact coverage. Source exports exclude both the paintings and runtime plates.

---

## `.thm` — colour themes *(ours)*

Plain text, whitespace-separated. `lib/theme/bog.thm` is the reference copy.

```
name    Bog
blurb   Green-black and amber. Frog's own.

ui      canvas      0A0F0C
ui      selection   2E4A30  D8

map     DARK        0A0F0C
map     L_RED       F4706A
```

| directive | meaning |
|---|---|
| `name` | shown in the settings menu |
| `blurb` | one-line description |
| `ui <role> <RRGGBB>` | one of 8 interface roles: `canvas`, `panel`, `border`, `title`, `text`, `muted`, `accent`, `selection` |
| `map <NAME> <RRGGBB>` | one of the game's 32 colours, by its own name |

`selection` takes an optional second value — an opacity byte.

**The rule that matters:** the 32 map colours may be *retinted* but never
*reordered or repurposed*. A red `D` has to stay a different dragon from a blue
one whatever theme is on. Every theme lists them in the same order under the
same names. A theme that lists no `map` lines (Classic) leaves the game's own
palette alone.

`ui` and `map` must not borrow from each other, or a retint in one place quietly
wrecks the other.

---

## `.art` — portrait plates *(ours)*

Binary. Written and read by `src/ui_art.c`, generated from `_art-src/*.png` by
`ormeq-sdl3.exe -A`.

```
offset  size          field
0       8 bytes       "FROGART2"
8       u16           cols        grid width
10      u16           rows        grid height
12      u16           src_w       source painting width
14      u16           src_h       source painting height
16      4 × cols×rows cells, row-major: coverage, red, green, blue
```

All integers **little-endian, written a byte at a time**, so the file does not
depend on the machine's word order or on how the compiler padded a struct.
Maximum grid is 96×96 (`UI_ART_MAX_COLS/ROWS`).

**The important design point: a plate stores *coverage*, not glyphs.** Which
character a cell becomes is decided at draw time, against a ramp measured in the
typeface currently loaded. That is why a portrait re-renders correctly in a
different alphabet when you change fonts — storing chosen glyphs would have
baked in one font's answer forever.

Grid resolution went 48 → 96 on 29 August 2026: 48 was chosen when the largest
consumer was a 30×20 page, and a shopkeeper's 440×680 box made the coarseness
visible. Detail thrown away at the storing end cannot be recovered at the
drawing end.

---

## Optional sprite pack

Playable packages can supply `lib/tiles/pack/`: 32- and 64-pixel PNG atlases,
text indexes and an inventory. The public `lib/tiles-manifest.json` records
their names, sizes and SHA-256 hashes. `tools/check_sprite_pack.py` checks an
installed pack and its monster names; an absent pack is valid and uses glyphs.

`tools/build_sprite_pack.py` builds sheets from supplied artwork and recipes.
`tools/test_sprite_pack.py` exercises it with synthetic images. The artwork,
production recipes and finished sheets are not included in the source tree.
See [Map presentation](map-presentation.md) for display modes, terrain handling
and missing-art behaviour.

---


## Definitions implemented in C

Spells, devices, races, classes, mutations and effects still combine display
text and behaviour in C. For example, spell dispatchers return a name,
description or information string, or perform an effect, according to the
requested operation. Editing those definitions requires the corresponding
source and gameplay tests; the data-reference checks alone do not cover them.
