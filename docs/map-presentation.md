# Map presentation

Open **Settings > Map presentation** to choose:

| Mode | What you see |
|---|---|
| ASCII | Coloured glyphs for terrain, creatures and items. |
| Hybrid | Available creature/item sprites over glyph terrain. |
| Full Tile | The same sprites plus terrain, shorelines and special environments. |

ASCII is the default. Its **ASCII grid** setting selects Classic rectangular
cells or Square cells. **Enhanced Square** gently widens visible stone-wall
glyphs; it leaves creatures, items and punctuation alone. The checkbox appears
only in Square ASCII, and its preference is remembered when changing modes.

Hybrid and Full Tile offer **Tile detail: 32 x 32 / 64 x 64**. This changes the
source artwork, not camera position, targeting or map-cell size. Use the mouse
wheel to change map zoom. The 64-pixel option retains more detail; 32 uses less
texture memory. Only the selected artwork resolution is loaded.

With Full Tile selected, **Keep ASCII floors only** leaves ordinary floor,
dirt and grass as their normal glyphs, including the ground beneath props.
Walls, creatures, items and special terrain (water, lava, snow, ice and glass)
keep their artwork. The checkbox is remembered when changing modes and
restarting, and does not affect ASCII or Hybrid.

Missing artwork always uses the normal glyph. This is expected: the initial
pack covers 567 monster illustrations, 44 item illustrations and 66 scenery images.
Ordinary floor, dirt and grass use quiet, dark textures in Full Tile. Trees,
flowers and transparent props have suitable ground beneath them, so their
backgrounds do not become dark square holes. A background never replaces a
missing feature's glyph. Doors, staircases, rubble, and shallow
and deep water/lava have distinct tiles. A missing or unreadable sheet also
falls back safely.
Creature lunges, death fades and knockback use the same sprite or glyph as
the map.

In Full Tile, village buildings alternate between sandstone, grey stone,
dark stone and slate using the existing scenery. Connected masonry keeps one
finish, stable across wilderness scrolling and reloads. This also works in
the compact Outpost layouts; natural mountain towers retain their rock appearance.

Shop signs reuse item illustrations as emblems. Entrances, shafts, the arena
gate, ordinary building doors, flowers, mirrors, wards and Sigil stones also
have artwork. Shop numbers and letters, stair directions, trap marks and Sigil
colours remain visible as corner symbols. At small zoom the normal full-size
symbol takes priority over the picture. Symbols never paint over the player,
creatures or found items. If an emblem is missing, the sign and symbol remain;
if the base picture is missing, the normal glyph remains.

Discovered trap doors, pits, dart/gas traps, discoloured spots, runes, bells,
bear traps and icicle traps use neutral family pictures. Covered traps still
look like floor. A pit picture does not disclose spikes or poison; dart, gas
and discoloured-spot pictures do not identify their hidden effects. Rune and
Sigil pictures can be shared, with the existing symbol colour retaining the
visible distinction. Active and spent Sigil stones have different artwork.
Rare class traps and other markers without suitable art keep their glyphs.

Water, lava, acid and waste have feathered shores and shallow-to-deep blends.
Snow, ice, slush, swamp and chasms also meet known ground with narrow edges.
The edges stay inside their own square: the centre retains its terrain, and
neighbouring safe ground is never painted as a hazard. Only discovered,
apparent neighbours contribute; secret doors still behave visually as walls,
and unknown terrain adds no edge. Regional world-map cells do not blend.
Subtle surface texture flips and shading remain fixed across scrolling and
reloads, without using gameplay randomness. At very low zoom the edges are
omitted to keep terrain legible. Missing edge artwork leaves the base tile.

Adjacent discovered pits and crevasses join by trimming their shared rock/ice
rims. Unknown neighbours retain the isolated picture, and different chasm
materials stay distinct. Curtain runs trim repeated end caps; vertical runs
turn to follow their length. Open curtains retain their visible gap. Corners
and junctions favour the horizontal orientation, rather than inventing a
different kind of terrain. All connections stay within their own map square.

Special environments include snow, ice slicks, slush, frozen walls, slopes,
glaciers, crevasses, dark pits, swamps, shallow acid and deep toxic waste.
Glass floors, ordinary/reinforced glass walls, open/closed/broken glass doors,
curtains, webs and snow-covered trees have distinct artwork. Locked and jammed
glass doors share the closed-door picture until their appearance changes.
Glyphs drawn over terrain tiles have a dark outline so bright snow cannot hide
the player or an unpainted creature. Health-warning colours remain unchanged.

Items try their known base-type illustration first, then a category icon.
Weapons, armour, food, lights, ammunition, books, coins and tools therefore need
far fewer pictures than there are individual items. A category icon represents
the family, not the exact material, quantity, enchantment, curse or value.
Identified and unidentified magical-item categories share neutral artwork;
an unknown potion never gains an effect-specific picture. Mushrooms remain
mushrooms after identification rather than borrowing the food-ration icon.
Corpses, bones, statues, figurines, junk, empty bottles and dragon-scale mail have neutral
category icons. The depicted shape does not identify the creature represented
by a corpse, statue or figurine. Skeletons use bones rather than a whole-corpse
picture. Photographs, separate body parts and other objects
without suitable category artwork retain their glyphs.

The player remains `@`, with the existing health-warning colours. Remembered
terrain stays dim. Disguises, vague sightings, hallucinations and unidentified
objects do not gain extra information from artwork. Map sprites remain sprites
when zoomed in; the existing ASCII illustrations remain in inspection cards.

These settings affect presentation only. They do not change terrain rules,
monster identities, combat, saves or gameplay random rolls. Previous enabled
tile preferences migrate to Full Tile; disabled preferences remain ASCII.

## Optional artwork and development

Playable packages supply `lib/tiles/pack/`. Public source excludes that folder
and all original artwork. `lib/tiles-manifest.json` records file hashes without
containing images. Builds without the pack remain usable in all three modes.
Artwork is covered separately in LICENSE.

`tools/build_sprite_pack.py` accepts an explicit JSON recipe of owned artwork
and source hashes. Pillow 12.0.0 and NumPy 2.3.5 were used for the current pack.
They are conversion tools, not game dependencies. Transparent portraits are
cropped to their silhouette and sampled into 32/64 squares with one pixel of
padding. Only foreground pixels contribute colour; output alpha is binary.
Walls use opaque box sampling with matching opposite edges. No new artwork is
generated by the converter. New essentials use original AI-generated source
sheets; the recipe can select a regular `cell=[column,row,columns,rows]` or an
explicit pixel `crop=[left,top,right,bottom]`. Both are checked against the
unchanged source image, and the bounds are recorded in the pack inventory.
The same identities occupy corresponding cells at
both resolutions; monster, object and terrain names have separate namespaces.

```text
python tools/build_sprite_pack.py --recipe YOUR_RECIPE.json --out lib/tiles/pack
python tools/build_sprite_pack.py --recipe YOUR_RECIPE.json --out lib/tiles/pack --check
python tools/check_sprite_pack.py --require-pack
python tools/test_sprite_pack.py
python tools/test_tiles.py
python tools/test_map_ui.py
```

The converter tests use synthetic images. The SDL loader tests generate their
own images and exercise pixel output, bad indexes/images, fallback, namespace
separation, switching and texture cleanup. Edge tests check all four directions,
texture flips, tiny cells, seamless fractional zooms, unchanged hazard centres
and no drawing outside the cell. Connected-art tests cover all sixteen neighbour
patterns, preserved curtain openings, vertical/non-square cells and quiet-ground
brightness. Headless game checks cover hidden neighbours, secret and glass doors,
treasure, disguises, unidentified objects and cosmetic RNG isolation. SDL map
tests use dummy video/audio and private saves, comparing camera coordinates
across 32/64 and missing-pack runs, with pond, frozen-terrain, glass and marker
courtyard/cellar fixtures at normal and small resolutions. Marker checks compare
identical load/save cycles with and without inspection, including saved RNG state.
Knowledge checks cover covered traps and every supported trap/marker family. Pass `--keep DIRECTORY` to retain captures.

Renderer-reset checks compare immediate captures of the map, HUD, menus,
help, messages, Past Reckonings and Look cards, including menu clicks after
reset. In capture mode (`-P`), scripted `%0` simulates a device reset and `%9`
simulates lost render-target contents; both capture before any further input
or forced refresh. This avoids masking a missing repaint with the ordinary
`%p` capture command. These are headless simulations, not GPU-driver loss tests.
