# 纹理资源

引擎的文件纹理入口使用 `.texture`，保存可直接上传的 2D 纹理数据，尺寸、格式、mip 数量和布局版本保存在文件头中；JSON／SQLite 资产记录保存资产身份、路径、采样参数和 GPU usage。SVG 作为转换工具的输入，通过 LunaSVG 按指定尺寸栅格化，再生成 mip 和编码为 `.texture`。

## 数据布局与元数据

`.texture` 使用 24 字节文件头，整数均为小端 uint32：

| 字节偏移 | 字段 | 含义 |
|---|---|---|
| 0 | magic | 四个 ASCII 字节 `VKET` |
| 4 | textureVersion | 文件格式版本，当前为 1 |
| 8 | width | 基础级宽度 |
| 12 | height | 基础级高度 |
| 16 | mipCount | 文件内 mip 级数 |
| 20 | format | Vulkan 格式枚举值 |

像素数据从字节 24 开始，布局版本 1 按 mip 0、mip 1 等顺序存储，原点在左上角，各级内部逐行紧密排列。未压缩格式每级相对于像素数据起点的偏移对齐到 `max(4, texelSize)` 字节，级间填充为零；文件末尾没有额外填充。尺寸逐级取 `max(1, dimension / 2)`。多字节分量使用小端。支持的格式为：

| Vulkan 格式 | 文件头 `format` | 每像素字节数 |
|---|---:|---:|
| R8_UNORM | 9 | 1 |
| R8G8_UNORM | 16 | 2 |
| R8G8B8A8_UNORM | 37 | 4 |
| R8G8B8A8_SRGB | 43 | 4 |
| R16G16B16A16_UNORM | 91 | 8 |
| R16G16B16A16_SFLOAT | 97 | 8 |

BC7 格式使用 4 × 4 像素块，每块 16 字节：`BC7_UNORM_BLOCK` 的 `format` 为 145，`BC7_SRGB_BLOCK` 为 146。每级按块逐行紧密排列，大小为 `ceil(width / 4) * ceil(height / 4) * 16`，相对于像素数据起点按 16 字节对齐。宽高仍保存实际像素尺寸；不足一块的边缘和小 mip 在编码时复制边缘像素补齐。文件头保持 24 字节，上传 staging buffer 只包含文件头之后的像素／块数据。

例如，一张颜色纹理的资产记录（尺寸、格式和 mip 数由文件头提供）：

```json
{
  "type": 0,
  "id": 1024,
  "name": "Wall",
  "path": "textures/wall.texture",
  "minFilter": 1,
  "magFilter": 1,
  "addressMode": 0,
  "anisotropy": true
}
```

`mipCount` 可以是 1 到完整 mip 链长度之间的值。转换器默认生成完整链，`--no-mips` 仅输出 mip 0。`.texture` 的资产记录使用 `usage` 指定 GPU 用途（默认 `VK_IMAGE_USAGE_SAMPLED_BIT`），上传时自动添加 `TRANSFER_DST`；最终布局为 `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL`。采样参数继续由资产配置决定。SQLite 的 `texture_assets.data` 保存同样的 JSON 字段。

加载器校验格式、版本、尺寸、mip 数量、设备能力以及文件精确长度，再通过 staging buffer 上传全部 mip。同步加载由 `Texture2D` 使用 graphics 队列的一次性命令完成上传并等待结束；异步加载通过调用者提供的 `AssetUploadContext` 录制上传命令，并处理 transfer 到 graphics 队列的所有权转移。文件头提供解析所需的元数据；资产库加载只验证资产配置，文件头和数据长度在导入或纹理加载时验证。运行时拒绝无有效文件头的 `.texture` 文件。

版本 1 支持上述未压缩格式及 BC7 的 2D 纹理；array、cubemap、3D 和其他 GPU 块压缩格式属于后续扩展。设备初始化按支持情况启用 `textureCompressionBC`，加载时检查格式与 usage 的设备支持。

## 独立转换工具

在仓库根目录构建和运行：

```powershell
scons tools/texture_conv.exe
./tools/texture_conv.exe source/wall.png textures/wall.texture --purpose color --format bc7-srgb
./tools/texture_conv.exe source/normal.png textures/normal.texture --purpose normal
./tools/texture_conv.exe source/mask.png textures/mask.texture --purpose data --format r8 --no-mips
./tools/texture_conv.exe source/environment.hdr textures/environment.texture --purpose color --format rgba16f
./tools/texture_conv.exe source/icon.svg textures/icon.texture --width 96 --height 96 --format rgba8-srgb --no-mips
```

输出目录需要事先存在。工具读取 SVG 及 stb_image 支持的输入，例如 PNG、JPEG、TGA、BMP 和 Radiance HDR，并输出包含文件头和像素数据的 `.texture` 文件。PNG 的 16 位分量会保留到浮点处理中；`rgba16` 可用于需要 UNORM16 精度的 LUT。默认输出为普通颜色的 BC7_SRGB、线性数据的 BC7_UNORM、法线的 RGBA8_UNORM，以及 HDR 的 RGBA16F。BC7 是有损压缩；LUT 和精度敏感数据应显式选择 `r8`、`rg8`、`rgba8`、`rgba16` 或 `rgba16f` 等未压缩格式。

SVG 的 `--width`、`--height` 指定栅格化尺寸，每个参数范围为 0～4096，默认 0。两者均为 0 时使用 SVG 的固有尺寸；仅指定一边时按宽高比计算另一边；两边均指定时保持比例并居中，空白区域透明。最终尺寸每边最多 4096 像素。非 SVG 输入不支持指定非零栅格化尺寸。修改 SVG 或目标尺寸后需重新转换；运行时资产路径应指向生成的 `.texture`。

`--format` 可选 `r8`、`rg8`、`rgba8`、`rgba8-srgb`、`rgba16`、`rgba16f`、`bc7`（UNORM）、`bc7-srgb`。R8／RG8 取解码后 RGBA 的 R／RG 分量。灰度输入扩展为 RGB，灰度加 alpha 输入的 alpha 放入 A。法线输出要求保留 XYZ，不能选择 R8／RG8；SRGB 输出仅适用于 `color`。

- `color`：LDR 输入按 sRGB 解释并转换到线性空间；HDR 输入按线性值解释。mip 在预乘 alpha 的线性空间过滤，再恢复直通 alpha。输出为 SRGB 格式时重新编码 RGB，alpha 始终为线性。显式选择 `rgba8` 会输出线性 UNORM 数据。
- `normal`：RGB 映射到 [-1, 1] 的切线空间向量，在基础级及每级 mip 归一化；零长度向量取 +Z。
- `data`：分量作为线性数值独立过滤。

BC7 使用随转换工具构建的 [bc7enc](https://github.com/richgel999/bc7enc_rdo) CPU 编码器。每级 mip 由未压缩数据过滤生成，再独立编码为 BC7；sRGB 输出采用感知误差权重，线性输出采用等通道权重。运行时直接上传压缩块，由 GPU 采样硬件解码。

缩小使用面积加权过滤，包含奇数宽高的边缘像素。UNORM 输出截断到 [0, 1]；RGBA16F 截断到有限 half 范围。NaN／Inf 输入被拒绝。

编辑器的 Texture 导入接受 `.texture`，读取文件头并检查文件长度，项目资产库只保存资产信息和使用配置。glTF 转换器把元数据写入纹理文件头；颜色、法线和数据用途分别生成纹理，以便各自正确构建 mip，并采用与独立转换器相同的默认输出格式。纹理资产上传完成后的布局为 `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL`。

编辑器图标保留 `editor_assets/icons` 下的 SVG 源文件，SCons 调用转换工具生成同名的 96 × 96、RGBA8_UNORM、单级 mip `.texture`。图标使用 `--purpose data --format rgba8` 保留栅格化后的分量值，由编辑器进行颜色调制；资产记录引用生成文件。

仓库内置纹理、UI 示例及 Sponza 示例资产记录使用此格式。Sponza 的大型资源目录仍由使用者提供，本地资源可由对应 glTF 转换工具重新生成。`init_project.py` 调用的 BRDF LUT 生成器直接输出带文件头的 `brdf_lut.texture`，尺寸由文件头提供。
