# Architecture

How the Ormeq frontend works, and why it is shaped this way.

---

## The problem

Every *band frontend is a terminal emulator wearing a costume.

The game talks to its display through **Term** (`z-term.c`), inherited
essentially unchanged from Angband 2.7. Term can say exactly one thing: *put
this character, in this colour, at this cell.* That is the whole vocabulary.

It is enough to draw the game and it has lasted thirty years, but it cannot say
"a fireball went off here". As far as a Term-based frontend knows, nothing
happened except that some cells changed colour for a moment and then changed
back. You cannot animate what you cannot be told about.

Angband 4 solved this by separating `ui-*` from the game. **Frog never got that
separation** — it is Hengband-lineage and branched long before — which is why
this fork adds channels alongside Term rather than refactoring the game behind
an interface. A rewrite of 356,000 lines is not on the table; adding narrow,
additive channels is.

---

## Four channels

```
                    ┌──────────────────────────────────┐
                    │            the game              │
                    └──┬────────┬──────────┬────────┬──┘
         what to paint │  what  │   what   │  what  │ is on screen
                       │ happened  the level│  looks like
                       ▼        ▼          ▲        ▼
                  ┌────────┐ ┌────────┐ ┌──────┐ ┌──────────┐
                  │  Term  │ │ui_event│ │ui_map│ │ ui_screen│
                  │ z-term │ │ (push) │ │(pull)│ │  (model) │
                  └────┬───┘ └────┬───┘ └───┬──┘ └─────┬────┘
                       └──────────┴─────────┴──────────┘
                                    │
                            ┌───────▼────────┐
                            │  main-sdl3.c   │
                            └────────────────┘
```

### 1. Term — what characters to paint

Essentially unchanged, and **still the authority on what the screen contains.**
Everything below is additional; nothing replaces it. This is what keeps the
Win32 and headless builds working.

One addition worth knowing: **`Term_dirty()`** marks the whole screen as needing
a repaint *without performing one*. `Term_redraw()` also refreshes, which puts a
half-finished screen in front of the player — a frontend that has drawn
something Term cannot account for needs the region *marked*, not painted now, so
the next natural refresh does it in the right order. Forcing the paint showed the
bare map for an instant every time a menu redrew.

### 2. `ui_event` — what happened *(game → frontend, push)*

The semantic channel. The game announces events — a bolt travelled from here to
there, something took 47 damage, a blast expanded one cell, a panel is about to
be painted at this rectangle, a portrait of *this creature* belongs in this box.

**The contract has two halves, and both matter:**

- **Emitting is nearly free when nobody is listening.** `ui_event_*()` returns
  `FALSE` immediately if no handler is subscribed.
- **A handler returning `TRUE` has *claimed* the effect** — the game skips its
  own rendering. `FALSE`, or no handler, leaves the classic character-cell path
  untouched.

That second half is what makes this safe to add one event at a time. Every call
site keeps its original code as the fallback, so curses and Win32 behave exactly
as before, and any event a frontend has not been taught simply falls through.

Positions are **UI points — terminal cells** — because that is what a frontend
can turn into pixels without knowing anything about the dungeon. The emitters
take *cave* coordinates and convert, so callers pass what they already have.

Full event list with per-event reasoning: `src/ui_event.h`.

### 3. `ui_map` — what the level looks like *(frontend → game, pull)*

**The one place traffic runs the other way.**

Term carries the 80×27 *window* onto the level and nothing else. To zoom out to
the whole dungeon, a frontend needs the whole dungeon — and Term does not
transmit it. So the frontend asks.

A pull rather than a push, because the frontend only wants this while it is
actually drawing zoomed out; streaming a level on every change to answer a
question nobody asked would be silly.

`ui_map_cell()` returns `FALSE` for anything undiscovered, which is what makes
the zoomed view show *explored ground* rather than handing over the level.

Two rules this interface exists to hold:

- **It is the *apparent* race.** A mimic pretending to be a chest stays a chest.
- **Nothing is returned while hallucinating.** The game is already lying about
  what is there; answering honestly would give the trick away.

> **A card is allowed to save keystrokes. It is not allowed to hand over
> anything the player has not earned.**

### 4. `ui_screen` — what is on the screen right now

A **phase** (`STARTUP` / `HOME` / `BIRTH` / `WORLD`) plus a **layer bitmask**
(`MODAL` / `MENU` / `PROMPT`, a bitmask because a prompt can sit over a menu).
The map owns the screen when the phase is `WORLD` and no layer is up.

This exists because three bugs came from a frontend *inferring* it: the zoomed
map painted over menus, menus came up with bands of canvas through them, and a
menu's frame was left stranded on the dungeon after it closed. Each was a rule
bolted onto a system with no model of what the screen held, and each fix
revealed the next gap.

The other half of its job is **teardown**. A frontend draws things Term knows
nothing about — a panel's ground colour, for one, which Term has no way to
express. Anything of that sort must be undone when the layer that justified it
goes away, and the region repainted, or it survives on screen with nothing left
to remove it. That is why layer changes are announced
(`UI_EVENT_SCREEN_CHANGED`).

---

## The document pipeline

`z-doc.c` is inherited: a virtual terminal that renders rich text — colour tags,
named styles, word wrap, links, topics — and blits a region to the real Term.
The help system, the character sheet and most menus are built on it.

**This is the leverage point for menus.** A document declares its own structure
through the semantic channel — `UI_EVENT_MENU_CLEAR` when it paints,
`UI_EVENT_MENU_ITEM` per row with the key that row stands for — and a frontend
that understands those makes the menu clickable, hoverable and themed **without
any change to how the menu draws or reads keys**. Every original letter key
still works.

The practical consequence: **a new menu opts in with one line.** The object
prompt family (inventory, equipment, quaff, read, drop, wield) came for free
once `doc_sync_menu` was doing this.

Two gotchas that cost real time:

- **`_normal_style` wraps at column 72 and counts colour markup toward it.**
  Three `<color:...>` spans add ~50 invisible characters, so a 60-column centred
  line breaks at what looks like column 58. Use `<style:screenshot>` for
  anything laid out.
- **`doc_insert_doc()` must carry the portrait.** A page composed in a block and
  inserted into another used to lose its picture — box drawn, nothing in it,
  nothing saying why.

---

## Layout: a page, or an overlay

Everything full-screen in this game is one of two things, and the difference
decides how it is drawn.

**A page owns the screen.** Help, the option browsers, knowledge, every screen
of character creation, the character sheet, the death card. It has no border —
the window is its edge — and the theme's panel colour runs to the window's own
corners, including the strip of pixels below and right of the grid that no cell
reaches. `ui_event_panel_screen()` says so; `sdl3_panel_fullscreen()` is what
the frontend decides on.

**An overlay covers something.** A menu during play, the monster and object
lists. It is the size of its content, two rows down to keep clear of the
message line, with a ground under it and a border round it, and the dungeon
still readable either side.

`ui_page_rect()` in `xtra2.c` is the box a page lays itself out in — it lives
beside `ui_map_rect()` and the other layout rects. Two rules in it are worth
knowing:

- **The margins are a fraction of the screen, not a count of cells.** `cx/14`
  and `cy/10`, floored at 2 and capped at 16 and 8. The first version capped at
  8 and 4, which is a reasonable margin in a 127x40 window and three per cent
  of the width on a 1440p fullscreen grid. *Rendering it and looking at it is
  not enough if you only ever render it at one size.*
- **`ui_page_text_rect()` narrows that to a measure of at most 100 columns.**
  Prose wants a column; a two-column browser wants the width. Each caller says
  which it is.

**The box is measured from the screen and never from the content.** That is
the whole point of it: character creation is a dozen pages sharing one header,
and while each page was centred on its own contents the header moved every time
you opened a chooser and came back.

---

## The theme system

`theme.h`, loaded from `lib/theme/*.thm`. **Two palettes, deliberately
separate:**

| | |
|---|---|
| **Map palette** — 32 colours | The game's own. **Colour carries meaning**: a red `D` is a different dragon from a blue one. A theme may *retint* these; it must never change what one means. Every theme lists them in the same order under the same names. |
| **Interface palette** — 8 roles | `canvas`, `panel`, `border`, `title`, `text`, `muted`, `accent`, `selection`. Chrome only. |

**Chrome must not reach into the map palette, and the map must not borrow a
role**, or a retint in one place quietly wrecks the other.

Applying a theme overwrites `angband_color_table`, which is why *every* frontend
gets themes without knowing they exist. A theme listing no map colours (Classic)
leaves the game's palette alone; the original is kept at startup and restored.

---

## Portraits

`ui_art.c` — and **it is not a tile set.** There are no pixels on screen that
are not characters, and the characters are the ones your chosen typeface draws.

1. Every candidate glyph is rendered once in the current font and its ink
   measured. Sorting by ink gives a ramp from light to dark that is *correct for
   that font* — VT323's comma is not Cozette's comma, and a ramp built for one
   is wrong for the other.
2. The source painting is cropped to its subject and divided into a grid. Each
   cell's pixels are pooled: average alpha picks the glyph off the ramp,
   alpha-weighted average colour picks the colour.

So a portrait re-renders in a different alphabet when you change typeface, and
stays a picture, because the ramp was *measured* rather than guessed.

**The game never learns whether art exists.** It says only "a portrait of X
belongs in this box" (`UI_EVENT_PORTRAIT`) and supplies the classic glyph as a
fallback. A frontend with a plate draws it; one without draws the letter large;
one that ignores the event leaves a box the game already filled sensibly.

Plates live in `lib/art/**.art` (binary, `FROGART2` magic), 631 of them, built
from `_art-src/` by `ormeq-sdl3.exe -A`. Sources are working material and are
not part of the game.

---

## The frontend

`main-sdl3.c` (4,443 lines) plus `sdl3_map.c`, `sdl3_anim.c`, `sdl3_look.c`,
sharing `sdl3_local.h`.

`sdl3_local.h` carries exactly three things — frontend state, the small drawing
helpers the whole port shares, and each layer's entry points — and refuses a
fourth. Anything a layer only calls from inside itself stays `static` in that
layer's file.

> **The point of a split is a smaller surface, not the same surface spread
> thinner.** If `sdl3_local.h` ever becomes a copy of the old file's contents,
> the split has failed and is worth undoing.

### The single-transform rule

`sdl3_map_view_get()` is **the only** transform between cave grids and pixels,
and both the drawing and the mouse go through it.

This is not tidiness. A map drawn with one transform and clicked with another
lies about where things are — and the bug presents as *bad aim*, not bad
arithmetic, so you look in the wrong place for a long time.

---

## Two principles that keep recurring

**Ask the game, do not infer.** Several bugs came from the frontend guessing
state the game already tracked — whether a menu covers the map, whether the
screen is saved, whether the player is blind. `ui_screen` and `ui_map` both
exist because inference lost.

**Compute a transform once.** See above. Renderer and input must share it.

---

## HUD clearance and Look

SDL3 owns the map transform in every mode, including rectangular ASCII at 1x.
`sdl3_map_safe_rect` reserves the sidebar, message height, status strip and the
maximum live calculation height. Reserving the full calculation height before
the first event keeps the camera stable as its empty panel fills. The map still
paints behind translucent HUD surfaces, but focus placement and map input use
the clear region. `ui_map_safe_axis` shifts the camera just far enough to keep
the complete focus cell and up to three neighbouring cells clear. Margins shrink
in cramped views. SDL3 uniformly caps the effective scale when an extreme zoom
would make the focus cell larger than that region, retaining the requested zoom
for when more space is available.
It does not clamp back to level edges. Drawing, markers, hover and mouse input
share that transform. Pixel-to-grid conversion floors negative coordinates.

`UI_EVENT_MAP_FOCUS` carries cave coordinates while Look, targeting or Locate
browse away from the player; a negative coordinate restores player focus.
This does not change the engine viewport, visibility, random state or turns.
The pure axis calculation is covered by 49,312 size/zoom/focus/inset combinations
in the headless fixture, including input round trips and repeat-frame stability.

Automatic Look uses `_target_look_card`, separate from combat targeting's manual
recall toggle. SDL3 fixes the card at the left and moves the selected creature
into clear space beside it. R switches to an opaque fullscreen version with
larger type, retaining subject and document position; the size choice is retained
for later Look commands in the same session. Brackets and the wheel scroll.
The game supplies current health, disposition and range, plus available action
keys. The frontend exposes them as buttons; actions still run through the normal
Look controller. Input cannot click through a card into the map. The position
button becomes Subjects when browsing individual squares. Esc exits either view.
Terminal frontends retain a full-width document. No preference or save format
changes are required.

## The Reckoner and Past Reckonings

`melee_calc.c` observes combat and spell resolution. `damroll_record` and
`damroll` share the original loop in `z-rand.c`: recording takes its actual faces
without adding random draws. A bounded session ring groups up to 24 events per
block and 48 retained blocks. Adjacent resolutions in one player turn share a
block when space allows; a full block continues in the next slot. History
allocates only its retained results, and pending records clear one result at
creation instead of clearing/copying every unused slot. Nested effects share
the pending action; overflow is reported. The viewer lives in `ui_reckoner.c`.
Starting or loading a game clears the history. No save format changes are needed.

Weapon and martial attacks in `cmd1.c` retain hit and critical checks, damage
dice, scaling, bonuses and penalties. Natural attacks and possessed-body attacks
also record their resolved damage; some special modifiers remain partial.
`cmd2.c` and `py_throw.c` observe physical projectiles through the original
hit/critical functions, with optional record arguments. Ordinary damage stages
keep their existing statements and order; observers copy the resulting values.
Projectile constructors freeze ammunition/throwable knowledge as well as equipment
knowledge. An arrow can be unidentified even when the equipped bow is known.
Explosion power is an effect record, not an extra damage hit; explicit source
traces carry its dice into the existing projection path. Critical quality records
whether weight is multiplied by the draw (shots) or added to it (throws).
Needle outcomes do not expose the fatal hp+1 amount. Wounded-target bonuses and
custom throwing callbacks stay partial. Ammo breakage/catch/return and secondary
special shot effects are not claimed as fully covered.

`spell_calc.c` carries an integer value and a small immutable generation-checked
handle into a 128-slot provenance pool. Each builder creates a new generation,
so copied locals cannot alter each other's trace. Reset or exhaustion discards
expired provenance, never the numeric value. A compile-time size guard prevents
large by-value traces from returning. `spell_power_record` and the unobserved
spell-power functions share the same formula in `do-spell.c`. Effect
call sites pass that value through wrappers around the original spell functions.
Dice use the original loop; single random draws retain their original integer
return type. Flat bonuses and spell power preserve integer rounding, including
the separate division before addition and the zero floor. Local integer damage
variables can retain a companion trace: any intervening unsupported arithmetic
is a labelled boundary, never a formula inferred from matching numbers.

`project` scopes the source around the projection, clearing it from nested
reflections and callbacks. Each `project_m` target gets its own copied record,
distance step and `gf_affect_m_record` defence chain. Multi-target and repeated
projectiles reuse the original roll only when the game does. Elemental immunity,
vulnerability, fixed and random resistance, barriers and pact resistance have
explicit stages. The live spell diagram shows dice/base, cast power, power at
the target and final damage. No-target effects retain their supplied power
without claiming HP damage. The detailed account is Past Reckonings; the optional
live tool and its setting are The Reckoner.

Shared realm, specialist class and racial power implementations pass explicit
sources, including psionics, blasts, force, mirrors, necromantic touches and
evaporation. Player-cast borrowed monster magic carries dice or health-based
breath calculations through its normal projection path. Delayed Ego Whip records
the later save and damage separately. Bomb damage records sound and shard
components without changing the inherited argument ordering.

Secondary details have their own bounded list on each event. Scoped observers
capture actual draws, comparisons, arithmetic, outcome notes and accepted status
counters. Damage completeness is separate from secondary coverage. Target scopes
nest and restore; player backlash statuses become player events. Setters observe
the accepted value after the original clamps, before the original assignment.
Unknown details are discarded, including details appended after damage finished.
Monster-dependent secondary numbers require recall-level knowledge frozen at
creation. Individual timers retain only categorical transitions. HP-derived
special damage and capture discard the entire amount/chain and retain a public
outcome. Outgoing hit-check thresholds are uniformly concealed: race armour lore does
not reveal individual modifiers, and conditional omission would itself leak
them. Private monster HP/armour inputs are not exposed by attribute-change notes.

Opposed saves explicitly preserve each call site's pre-change MSVC draw order.
Wrapping the two operands in functions can change that order despite consuming
the same number of random values: previous-engine outcome comparisons are
essential. Single-draw comparisons evaluate each operand once and preserve
short-circuit conditions. There is no global RNG interception. Standalone dice
with an unknown role remain separate. Utility outcomes such as summons, item
changes and travel still use normal messages; device and special attack formulas
can retain explicit boundaries.

Incoming events span `melee1.c`, `gf_affect_p`, `res_calc_dam` and `take_hit`.
Separate defence records precede damage: monster melee accuracy and repulsion,
early substitution, reflection, nimble dodge and spell turning. Their public
verdicts carry no invented damage amount. Numeric details use scoped observers;
monster-dependent checks require visibility, true appearance and relevant lore,
and conceal private individual scaling. Player-only checks can name an unknown
source without revealing its attributes.
Reflection checks are omitted with unknown equipment, since naming even a
failed attempt could disclose an unidentified item flag. Hit and repulsion scopes
remain at their original call positions, on either side of `disturb`; nested projectiles
and retaliation do not inherit them. `take_hit` scopes actual foresight and
invulnerability rolls, shadow/force notes and minimum-damage checks, restoring
the previous scope afterwards. Melee ties miss: its draw is 1..power and the
comparison is strictly greater than armour x 3/4.
Incoming damage records capture actual resistance percentages, armour, protection, stances and
mana shields where applied. The HP result is frozen before downstream healing
or form-change callbacks. Nested damage restores the outer observation context.
Healing records its spell source, class and racial penalties, observed modifiers
and the maximum-HP cap. Unsupported modifiers remain explicit boundaries.

Records own their values and names, with visibility and equipment knowledge
captured at the event. Unknown inputs are discarded before presentation. Incoming
monster dice additionally require the same lore predicate used by creature
recall. Later discoveries cannot reveal old inputs. A complete arithmetic chain
requires every recorded stage to join, validate and reach the result. This does
not claim coverage of every secondary-effect or saving-throw calculation.
Outgoing damage can exceed the target's remaining HP.

`do_cmd_history` owns the Messages/Past Reckonings tabs on Ctrl+P. History
preserves the selected account/result and compact-list versus detail view
across tab switches. Recent rows show immutable session serials, not viewport
shortcut numbers. Live selection traverses retained results chronologically
across all accounts with the latest recorded player_turn. It uses only stored
records; a new account resets selection to its preferred result. Previous/next
clicks are UI-only, gated to the normal command prompt, and Details opens that exact
account/result. Panel geometry does not change for multiple results.
Wide pages show a clickable recent list beside a scrollable
account; narrow pages open one view at a time. Controls stay outside the account.
Results are named Casting, Damage, Healing or Status change, with observed
adjustments and checks shown directly. E retains the complete exportable working.

`reckoner_result`, `reckoner_reason` and `reckoner_flow` are pure presentation
helpers. Identity snapshots own apparent glyphs, colours and source names; no
history renderer looks up a living/dead creature or item. Incoming source names
are observed without changing numeric knowledge gates. Unknown sources use ?.
`sdl3_calc.c` paints the outcome and explanation first, followed by the
source/tool/recipient row directly above its numerical stages and a ribbon
scaled to that card's maximum amount. Sparks use SDL time, never game RNG;
values remain fixed. Counters, concealed inputs and casting checks never use
a damage ribbon. The actual dice range includes an average marker, not a
probability scale. Generic effects/statuses do not invent a successful save.
The Details button queues history only at the normal command prompt and selects the
result shown live. Panel input does not reach the map.

The live panel shares `sdl3_hud_surface` with the message and status panels. Its
text scales with terminal cells, its empty state is compact, and its bottom
anchor reserves a status row whenever that panel is enabled. Message surfaces fit their text.
Health bars, effects, Study and location form one sidebar; a crowded sidebar
falls back to the bottom status strip for location. Menus and full Look cards
hide the live panel. Off/Static/Animated is a local preference, default Off;
history remains available in all three modes.

`tools/test_game.py` and headless-only `test_melee.c`/`test_reckoner.c` cover actual attack and spell
resolution, defences, healing, nested auras, multiple targets, record limits,
knowledge concealment, history/export and menu behaviour. The review fixture adds
416 realm/song menu entries, actual song/device effects, hidden HP outcomes and
lore-off, disguised and unseen targets. Debug builds also audit stack frames. Recording disabled,
enabled and all live display modes must preserve HP, SP, energy, RNG and other
recorded game-state fingerprints. The specialist fixture exercises saves,
resisted/immune targets, stacking, delayed effects, borrowed spells and hidden
data. Python independently recalculates emitted arithmetic, comparisons, roll
ranges and dice totals; negative examples prove the oracle rejects wrong data.
The guide in `lib/help/reckoner.txt` is accessible from history with `?` and from
the main help index. This coverage does not exhaust every game combination.
The incoming-defence fixture also tests actual blows and projections, strict
threshold ties, prevention successes/failures, short-circuit paths, unknown
sources and equipment, and recorded damage against HP lost. The same fixture
can be built against the previous engine for outcome and random-state checks.

## What is not done

- **Content extraction** (moving mechanics from C into data) has not started.
  Calculation observers preserve the existing combat decisions and draw order.
- **Behavioural coverage is incomplete.** The isolated gameplay suite covers
  seeded play, saves, menus and mixed combat fixtures; most special mechanics
  still lack focused regression tests.
- **Audio variety** can expand further: specialised class buffs, richer
  elemental/context-specific cues remain future work. The optional
  title track is Ormeq - Home Theme (Dark).

The SDL3 audio layer (`ui_sound.c`) maps 107 non-silent events through
`ui_sound_map.h` to optional PCM WAV variants. Missing/corrupt effects use the
original synth recipes. A 12-voice callback mixer and 32-sample cache bound
playback work and memory; per-event repeat limits prevent sound backlogs.
Variation uses a private counter, never game randomness. Volume, mute and
sound-pack selection live in display preferences, outside character saves.
Public source includes the mapping, inventory and tests but excludes WAVs and
production files. `test_sound.c` exercises the actual decoder/mixer using
synthetic fixtures, including unchanged game RNG state.

Ranged contact is emitted after the existing hit decision, with the same
generic information already provided by the hit message. No cue identifies
unseen targets or distinguishes their armour, resistances or remaining HP.
Throws announce release only after item/target validation. Status setters
emit onset/ending cues through `sound_status` after their existing validation,
gated by their notice flags: timer refreshes, ticks and effects still supplied
by a song remain silent. Twenty-six common counters are covered (ailments,
haste, protection, heroism, senses and temporary elemental resistance).
Recovery uses the recovery family; buff expiry uses a neutral UI material
shift. Shop names are from the shop's perspective: `_buy_aux` is a player
sale, `_sell_aux` a player purchase. Bulk purchase emits one cue if anything
was bought, including a free item. Explicit equipment actions have cues;
birth/load and programmatic inventory placement do not. Ordinary book casting
now emits a generic spell cue after successful execution, matching the
already audible specialist casting paths; cancelled casts remain silent.

`test_audio_events.c` is compiled only in the headless test frontend. It
captures the actual terminal sound events and verifies status transitions,
prevention, cancellations, purchases/sales/gifts, pickup/equipment/quiver,
stairs, disarming, and visible/unseen projectile contact. Seeded enabled and
muted runs must match gameplay/RNG traces. All 230 packaged effects and 14 soundscapes are
hash-checked against the manifest, while source-only builds retain synth
recipes for every event, including the appended IDs.

Ambience (`ui_ambient.c`) uses an independent stream and two bounded PCM16
tracks, with a one-second crossfade and a 100 ms blended loop seam. A third
temporary buffer can exist while a new recording loads on the main thread;
each is bounded to 16 MiB and 65 seconds. The callback allocates nothing and
does no I/O. Missing/corrupt recordings use a quiet procedural bed whose seed
is private to audio. The source selector affects effects and ambience.
Surface day/night and known dungeon depth select seven contexts; no monster,
secret room or hidden terrain state is consulted. Menu ambience is ducked to
25% of the chosen volume; mute stops it immediately and focus loss holds its
position. Generated fixtures test arithmetic, decoding, transitions, loop
seams, focus/mute, ownership and unchanged game RNG; installed-pack checks
also decode every recording. Preferences live outside character saves.

Wall bumps latch on the first cue after a fresh SDL key/mouse press. Keyboard
repeat and text-input events never re-arm the latch. This changes audio only:
the existing input queue, movement, collision handling and turn costs remain
untouched. Footsteps follow successful movement; common menu/document input
paths provide quiet navigation/accept/back cues and have a separate switch.

Title music (`ui_music.c`) has a separate SDL audio stream, independent mute
and volume, and no game-state or RNG access. The main SDL loop brackets
`start_screen()` with title activation; starting/loading fades out over 250 ms,
returning to the title restarts with a 700 ms fade. Focus loss clears queued
music and holds the cursor; focus gain fades back in. Mute and zero volume
also hold the cursor. Preferences are stored in `lib/user/display.txt`.
The loader accepts bounded PCM16 WAV (44.1/48 kHz, mono/stereo, 10 minutes,
128 MiB), rejects truncated files, and fails quietly. It keeps one PCM buffer;
the callback converts small blocks to stereo floats, with SDL resampling.
Ten-millisecond edge envelopes suppress loop clicks, but the final musical
seam and level still need a listening pass. Stream locks protect control
changes; shutdown destroys the callback before freeing its data. No file I/O
or allocations occur in the callback. Generated fixtures exercise the actual
mixer and both independent streams; no private music is required for CI.

## Supported frontends

The Windows CMake build provides `sdl3`, `win32` and `none` (headless tests).
SDL3 is the primary frontend. Presentation hooks retain terminal fallbacks for
Win32 and headless operation; new hooks must keep those paths working.

The public source distribution includes the maintained Windows build, its
vendored SDL development packages and redistributable fonts. Obsolete upstream
platform build files and Cocoa resources are not included. Linux and macOS
would require porting and testing the current SDL3 frontend and build setup;
they are not supported by this release.
