# Ormeq 0.1.2

Fixes birth options becoming read-only when starting another character after
playing and returning to the title screen.

- New characters can change birth options without restarting Ormeq.
- Birth options remain locked for existing characters, including after loading
  a save or cancelling character creation. Beginner presets are unchanged.
- Added regressions for saved option values, keyboard controls and compact menus.

This shared-code fix is available for Android's next source sync. Android-facing
birth text and save compatibility are unchanged. Extract into a new folder; copy
your existing lib/save and lib/user folders to keep saves and settings.

## Ormeq 0.1.1

Character-creation fixes for keyboard navigation:

- Selection bars no longer carry between pages or reappear on unrelated choices.
  Race and class lists still highlight the current choice; arrows and Right work
  as before within each page.
- Quick Start and Random patron choices now have hotspots only when displayed.
- Return plays an acceptance sound only when the page accepts it.

Includes regression checks for desktop and compact layouts, nested pages and
sound feedback. Android-facing footer text is unchanged for the next source sync.
Gameplay and save compatibility are unchanged. Extract into a new folder; copy
your existing lib/save and lib/user folders if you want to keep saves and settings.

## Ormeq 0.1.0

First Windows x64 release.

Ormeq brings FrogComposband's character choices and dungeon systems into a
shared world with Ammerow: Lands Beyond.

- SDL3 frontend with resizable windows, colour themes and map zoom.
- Keyboard and mouse navigation, animated effects and optional authored sound.
- Item and spell selection use clean pages without the underlying map or HUD.
  The Reckoner's Details control stays steady while turns resolve. Messages and
  Past Reckonings remain on Ctrl+P; duplicate pause-menu entries are removed.
- Quest journal with Active, Completed and Failed lists, explicit return steps,
  responsive details, questgiver cards and actual reward receipts. Outpost's
  briefings and responses are revised. Missing portraits use glyphs; browsing
  reveals no extra map or enemy information and does not change random rolls.
- Quest journal highlighting and mouse controls work on first opening, including
  tabs and footer buttons; hiding the map no longer discards the page's controls.
- Count Uldrik and Goodman Odwin have optional portraits on their quest cards
  and journal entries, including town modes that shorten their names.
- Telmora's eleven quest files have revised dialogue and concise objectives and
  directions, including alternate quests. Denegor III has an optional portrait.
  Compact cards and journal details put the next step and objective first,
  with smaller portraits and the full text still accessible by scrolling.
- Permanent town reward receipts preserve the awarded item description, closing
  response and collection time. Existing saves load normally; earlier turn-ins
  show "Reward not recorded". Outpost objectives and directions follow the offered
  quest variant.
- Permanent failure records retain known causes and the actual town response
  once reported. Older missing details are labelled "not recorded". The journal
  remembers the last tab and quest in each list, including across save/load.
  Existing saves remain supported; new saves use format 7.1.4.10 and require an
  updated build to load.
- Morivant's eight castle quest variants have revised Bulin dialogue, clear
  objectives and directions. The snake quest explains its existing whip-recovery
  objective; other commissions explain site clearance and loot-based payment.
  Conditional rewards, alternate quests, saved records and glyph fallbacks remain intact.
- Hjolgar's Fighters' Hall and Paliro's Thieves' Guild have revised conversations
  and journal guidance, including the older hunt identities retained in saves.
  Hunt locations follow the world's dungeon replacements, and Troll Slayer
  correctly asks for day-trolls. Target counts, rewards and quest rules are unchanged.
- Colmanwen's nine Angwil quest variants have revised dialogue and journal
  guidance, including their small-town assignments. Targeted missions distinguish
  their objective from clearing a site without revealing the target's identity.
  Quarry directions follow the town mode; existing goals, rewards and saves remain intact.
- Remaining Anambar, Thalos, Zul and Instant-mode commissions now have clear
  briefings, objectives and replies, including their alternatives. Finished
  entries explain existing service discounts, the Cop Quest home and Zul travel.
- Random dungeon quests record visible reward drops and known stat increases.
  Unseen or unidentified rewards remain concealed. Karrot and Troika histories
  join the journal using their existing saved records; missing historical rewards
  are labelled rather than guessed. The retired Killing Fields remains retired.
- Cards put objectives, directions and warnings before a compact questgiver
  panel. The journal remembers detail scroll positions per quest and provides
  compact controls for small screens. Quest rules are unchanged.
- All 16 town questgivers now have optional portraits, including the small-town
  contacts. The playable artwork pack includes them; public source uses glyphs.
- New Karrot and Troika assignments retain actual patron replies, witnessed
  effects, visible reward drops, failure causes and the gifts earned on that
  assignment. Reused assignments start with fresh journal details; older
  missing observations remain explicitly unrecorded.
- Ammerow sound effects mapped to Ormeq's events, with overlapping
  playback, rotating variations, volume controls and original synth fallback.
  Audio ships only in playable packages; source builds keep the synth.
- Added throw release and projectile contact, common status onset/recovery,
  buff expiry, pickups, equipment/quivers, completed trades, stairs and disarming.
  Status countdowns stay silent; audio does not change gameplay or random rolls.
- Quiet, switchable footsteps and menu feedback; wall bumps sound once per
  held press. Shops, homes, museums, service buildings, chests and unlocking
  have interaction cues; successful book spells have a casting cue. Sound settings are explicitly labelled as a submenu.
- Ammerow background soundscapes for surface day/night and five dungeon depth
  bands, with smooth transitions, quieter menus, focus pause and independent
  controls. Missing recordings use a restrained synth background.
- Ormeq - Home Theme (Dark) plays on the title screen, with independent
  mute/volume, looping, fades and focus pause. Music ships with playable
  packages and is excluded from public source; missing music stays silent.
- New character chooses an unused save name, including manually entered names
  and names that map to the same Windows filename. Existing saves are preserved.
- Revised names, descriptions and regional lore.
- ASCII, Hybrid and Full Tile map modes; retained portrait sprites at 32/64
  pixels, shared Ammerow scenery, and reliable glyph fallbacks for missing art.
  Square ASCII aligns visible glyph shapes; optional Enhanced Square widens
  stone walls. Artwork is supplied separately from public source.
- Village buildings use four existing stone finishes, consistent per connected
  building across wilderness scrolling and reloads. No additional artwork required.
- Full Tile adds distinct shallow/deep liquids, ordinary doors, stairs and rubble.
  Both sprite modes add generic equipment, food, lights, ammunition, books, tools
  and treasure, reusing existing portraits where possible. Hidden doors, locks
  and unidentified effects remain concealed; missing art keeps its glyph.
- Feathered terrain shores, depth transitions and stable surface variation use
  discovered terrain only. Frozen terrain, glass, curtains, webs, chasms, swamp
  and toxic pools now have tiles. Glyph outlines preserve contrast over bright
  surfaces. Artwork remains outside public source; gameplay is unchanged.
- Rendering queries preserve gameplay RNG state, including colour-changing
  creatures and hallucinations. Artwork resolution leaves camera geometry unchanged.
- Look opens creature stat cards automatically, using discovered knowledge.
  SDL3 offers a fixed map card with clickable controls; R switches to a larger
  fullscreen card and back, retaining the selected creature and scroll position.
- The camera keeps the player and nearby ground clear of the fixed HUD, including
  at level edges, in rectangular ASCII, and at different map zoom levels.
- Health-coloured player glyph and an optional static status warning border.
- The Reckoner: actual weapon, unarmed, spell, healing and incoming-damage
  calculations, with an optional bottom-left display and clickable H button.
  Past Reckonings history and exact working share Ctrl+P with Messages. Glyphs,
  a proportional ribbon and optional sparks explain the recorded amounts.
  History has a clickable recent list, named results and scrollable calculations,
  with a compact layout for small screens. Spell
  chains connect original dice, bonuses, spell power, distance and target defences;
  repeated projectiles retain their actual roll, and resistance dice are recorded.
  Specialist and borrowed spells include saving throws and status changes;
  delayed effects record their later resolution. A guide provides worked examples
  and explains missing data. Hidden inputs stay concealed; incomplete formulas
  and omitted details remain labelled.
- Consistent translucent HUD panels: messages fit their text, Study and location
  sit beneath the stats, and The Reckoner scales with the interface.
- Public data checks and isolated gameplay regression tests.

## Platform and installation

The first release targets Windows x64. Extract the complete playable ZIP into
a writable folder and launch `ormeq-sdl3.exe`. No font or SDL installation is
required. Keep the supplied DLLs, fonts, game data and licence notices together.

The Windows build has been tested on Windows 11. A separate clean-machine
compatibility check remains pending; broader Windows support is not yet claimed.

The source download builds with Visual Studio, CMake and the vendored SDL
libraries. It uses glyphs when optional artwork is absent. See DEVELOPING.md.

## Known limitations

- Linux and macOS builds are not maintained or tested for this release.
- The legacy Win32 frontend is retained for development comparisons; its old
  bitmap font pack is not distributed.
- Future development releases may change save compatibility. Back up saves
  before upgrading; preserve `lib/save`, `lib/user` and `lib/apex`.

The project is free to download under its inherited and component licence
terms. Artwork has separate terms; see LICENSE.
