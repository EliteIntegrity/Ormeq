# third-party

The SDL3 development packages the SDL3 frontend is built against, kept here
so that a copy of this folder is a copy of everything needed to rebuild.

| | version |
|---|---|
| SDL3 | 3.4.4 |
| SDL3_ttf | 3.2.2 |
| SDL3_image | 3.4.2 |

Trimmed to what we actually compile against: `include`, `cmake`, and `lib/x64`.
Removed were the arm64 and x86 libraries, the `.pdb` debug symbols for SDL
itself, and SDL3_image's `optional` codec DLLs for AVIF, TIFF and WebP -- every
image we load is a PNG, and SDL3_image decodes PNG on its own. That is 164 MB
down to under nine.

Upstream releases: github.com/libsdl-org -- SDL, SDL_ttf, SDL_image, the
`-devel-VC.zip` asset on each release. Licences travel with each folder. Additional embedded-component notices are
in `licenses/`; `licenses/dependency-sources.json` records the verified sources.

CMakeLists looks here first and falls back to the sibling folders, so an
existing checkout with SDL3 beside it still builds unchanged.
