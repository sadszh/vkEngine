Editor icons from [Tabler Icons v3.35.0](https://github.com/tabler/tabler-icons/tree/v3.35.0/icons), licensed under MIT; see [LICENSE.tabler](LICENSE.tabler).

| Asset | Upstream icon | Use |
| --- | --- | --- |
| camera.svg | filled/video.svg | Camera |
| sun.svg | outline/sun.svg | Directional light |
| bulb.svg | outline/bulb.svg | Point light |
| lamp-2.svg | outline/lamp-2.svg | Spot light |

Fill or stroke colors are white so the editor can tint the icons.

SCons uses `tools/texture_conv.exe` to rasterize these SVG sources into 96 × 96 RGBA8_UNORM `.texture` files with one mip level. The editor loads the generated textures. To regenerate an icon manually:

```powershell
./tools/texture_conv.exe editor_assets/icons/camera.svg editor_assets/icons/camera.texture --width 96 --height 96 --purpose data --format rgba8 --no-mips
```
