# LunaSVG

Vendored source from [LunaSVG v3.5.0](https://github.com/sammycage/lunasvg/tree/v3.5.0), including its bundled PlutoVG 1.3.1.
The upstream `include`, `source`, `plutovg/include` and `plutovg/source` directories are included.
Local adaptation: `source/svgelement.cpp` guards file-backed image loading with `LUNASVG_DISABLE_EXTERNAL_RESOURCES`.
Both projects' license files are included.

Source archive: `https://codeload.github.com/sammycage/lunasvg/zip/refs/tags/v3.5.0`
SHA-256: `f9e5d601db3191610939cb7d95a1e17a9f11b7bf04b18c978d498204e7b82d5c`

`Sconscript` builds both libraries statically into the shared engine sources.
System font discovery and external resource loading are disabled to keep SVG assets self-contained.
Use paths for text and embedded data URIs for images.
