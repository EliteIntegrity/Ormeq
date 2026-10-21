# Licences and component notices

This directory accompanies both the source distribution and playable download.
The root LICENSE explains which terms cover game code, artwork, audio and dependencies.

| Files | Coverage |
|---|---|
| `Angband.txt` | Inherited game-code licence |
| `Ormeq-Audio.txt` | Separate terms and production description for optional sound effects and title music |
| `Apache-2.0.txt` | Apache-licensed sections of SDL_egl.h and SDL_opengles2_gl2platform.h; header notices retained |
| `GPL-2.0.txt` | Alternative offered by two inherited source files |
| `Microsoft-Sample-Code.txt` | Licensed bitmap reader, Win32 frontend |
| `SDL3.txt`, `SDL3_ttf.txt`, `SDL3_image.txt` | Top-level SDL libraries |
| `FreeType-FTL.txt`, `FreeType-LICENSE.txt` | FreeType; this distribution uses FTL |
| `FreeType-BDF.txt`, `FreeType-PCF.txt`, `FreeType-hash.txt`, `FreeType-zlib.txt` | FreeType components with additional notices |
| `HarfBuzz.txt`, `HarfBuzz-Microsoft-USE.txt` | HarfBuzz text shaping and Microsoft Universal Shaping Engine data |
| `PlutoSVG.txt`, `PlutoVG.txt` | SVG components compiled by SDL_ttf's upstream Windows project |
| `SDL-hidapi-BSD.txt` | HIDAPI; BSD alternative selected |
| `SDL-yuv2rgb.txt`, `SDL-libm.txt`, `SDL-Khronos.txt`, `SDL-stb_image.txt` | Additional code carried by SDL's Windows sources |
| `SDL_image-stb_image.txt`, `SDL_image-nanosvg.txt`, `SDL_image-nanosvgrast.txt`, `SDL_image-qoi.txt` | Built-in SDL_image components; MIT alternative selected for stb |
| `OFL-1.1.txt`, `MIT-Cozette.txt`, `fonts.txt` | Bundled typefaces and their copyright notices |

The three bundled DLLs were compared byte-for-byte with the official Windows
VC development archives. `dependency-sources.json` records those archive URLs,
archive and DLL hashes, and the original source of each additional notice.
Text hashes normalize line endings so Windows Git checkouts remain verifiable.
The notices are copied from upstream files or their complete licence comments.

FreeType and HarfBuzz are inside SDL3_ttf.dll. They need no separate installation.
Runtime queries report FreeType 2.13.2 and HarfBuzz 8.5.0. The release uses
FreeType under FTL, not its GPL alternative.

Portions of this software are copyright (c) 1996-2023 The FreeType Project
(www.freetype.org). All rights reserved.

Optional libpng, AVIF, TIFF and WebP DLLs from SDL_image's development package
are not shipped. Ormeq's PNG loading uses the built-in stb backend. Optional
DLL filenames embedded in loader code are not themselves bundled libraries.

The font notice texts were read from the supplied fonts, and the top-level SDL
notice texts accompany their official packages. Existing author credits are
retained. No dependency has been relicensed by Ormeq.
