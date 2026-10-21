# Bundled fonts

Selectable at runtime from the dev screen (Shift+` → F) and listed in
`data/fonts.txt`. All are permissively licensed and safe to redistribute.

| file | face | license | copyright |
|---|---|---|---|
| `CozetteVector.ttf` | Cozette (bitmap block) | MIT | (c) 2020-2025 Ines \<ines@moonwit.ch\> |
| `IBMPlexMono-Regular.ttf` | IBM Plex Mono | OFL-1.1 | Copyright 2017 IBM Corp. |
| `RobotoMono-SemiBold.ttf` | Roboto Mono | OFL-1.1 | Copyright 2015 The Roboto Mono Project Authors |
| `ShareTechMono-Regular.ttf` | Share Tech Mono | OFL-1.1 | (c) 2012 Carrois Type Design, Ralph du Carrois |
| `VT323-Regular.ttf` | VT323 (terminal CRT) | OFL-1.1 | Copyright 2011 The VT323 Project Authors |

**The licence texts are in `licenses/`**, and they ship with every release.
That is not a courtesy: both OFL-1.1 and MIT require their text to travel with
the font, and an earlier version of this file listed URLs instead, which does
not satisfy either.

Every copyright line above is read out of the font's own OpenType `name` table
rather than copied from a web page. `licenses/fonts.txt` holds the same data,
generated the same way.

**Two corrections made on 5 September 2026**, both found by reading the files
rather than the documentation:

- **Roboto Mono is OFL-1.1, not Apache-2.0.** Google relicensed Roboto in 2015
  and this table had been wrong since it was written.
- **Cozette's upstream moved** from `slavfox/Cozette` to
  `the-moonwitch/Cozette`, and the copyright holder is Ines.

**Share Tech Mono carries the Reserved Font Name "Share".** Under OFL clause 3
a *modified* version may not be distributed under that name. Nothing here is
modified, so it is not engaged — but anybody who modifies one needs to know.
