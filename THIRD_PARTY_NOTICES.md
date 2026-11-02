# Third-party notices

ZARO itself is MIT OR Apache-2.0 (D-012). It builds with the pinned dependencies below
(`cmake/ZaroDeps.cmake`, which also holds their versions and hashes). Each one keeps its own
license; the full texts are in the dependency's source under `.cache/deps/` after a configure.
Lint rule Z010 fails when a dependency in `cmake/ZaroDeps.cmake` has no entry here.

| Dependency | License | Used for | In the Android runtime |
|---|---|---|---|
| wgpu-native | MIT OR Apache-2.0 | the GPU backend (D-002) | yes |
| Slang | Apache-2.0 WITH LLVM-exception | compiling shaders at build time (D-003) | no (build tool) |
| SDL | zlib | windows, input, audio output, Android activity (D-002, D-022, D-032) | yes |
| enkiTS | zlib | the job system (D-007) | yes |
| tomlc17 | MIT | reading scenes and other TOML files (D-017) | yes |
| Luau | MIT | game scripts (D-009) | yes |
| stb_truetype | public domain or MIT | fonts (D-019) | yes |
| stb_image | public domain or MIT | PNG decoding | yes |
| miniaudio, with its bundled stb_vorbis | public domain or MIT-0; stb_vorbis public domain or MIT | decoding and mixing sound (D-032) | yes |
| cJSON | MIT | `zaro import-design` (D-023) | yes |
| LunaSVG, with its bundled PlutoVG | MIT; parts of PlutoVG under the FreeType License | `zaro cook`, SVG art into sprite atlases (D-028) | no (zaro tool only) |
| Noto Sans KR | SIL Open Font License 1.1 | the bundled font (D-019) | yes |
| Tracy | BSD-3-Clause | profiling, only in `profile` builds (D-010) | no (opt-in) |

## FreeType

PlutoVG's scan converter and stroker (`plutovg-ft-raster.c`, `plutovg-ft-stroker.c` and their
headers) come from FreeType. A binary distribution of the `zaro` tool must state:

> Portions of this software are based in part on the work of the FreeType Team
> (https://freetype.org).

The Android runtime (`libmain.so`, D-022) does not contain this code: the cook module that links it
is built only for the desktop `zaro` tool (D-028).
