# UIText 文字排版算法

## 输入、输出与坐标

`UIText` 使用所选字体的 SDF 字形，默认字体为内置 Arial。`TextLayoutOptions` 定义字号、水平对齐、垂直对齐和自动换行；输入文字使用 UTF-8。排版以 Canvas 逻辑单位计算，RectTransform 的旋转、缩放和 Canvas 的屏幕适配在几何生成后作用于整体。

实现分为两部分：

- [`include/ui/text_layout.hpp`](../include/ui/text_layout.hpp) 中的 `LayoutText`：依据码点和 advance 生成行区间、行宽与内容尺寸，不依赖渲染器。
- [`src/ui/text.cpp`](../src/ui/text.cpp) 中的 `UIText`：准备字体度量、调用排版函数、应用对齐，并生成字形 Quad。

每行使用 `[begin, end)` 字符区间和 `width` 表示。`TextLayout.width` 是各行宽度的最大值，`height` 是行数乘缩放后的字体行高。文字测量和绘制使用相同的 `LayoutText`，绘制直接遍历其行区间。

## 字号与字体度量

字号 `fontSize` 必须为有限正数，默认值为 48，与内置字体的采样字号一致。字号表示字体 em 的逻辑尺寸，并不等于可见字形高度或行高。

```text
scale = fontSize / font.pixelSize
advance = glyph.advanceX * scale
lineHeight = font.lineHeight * scale
ascender = font.ascender * scale
```

字号变化缩放字形几何，复用字体的 SDF atlas。字形间距使用 FreeType 提供的整数像素 advance；当前没有额外的 kerning 或 shaping 步骤。

`prepareCharacters()` 解码 UTF-8，将 CRLF 和单独的 CR 统一为 LF，保留显式换行。Tab 的 advance 固定等于四个空格，不使用制表位。缺失字形尝试使用 `?`；替代字形也无法生成时，该码点使用零 advance 和空几何。字体数据复制进节点的字符缓存，供字号变化、测量与重新排版使用。

## 按词贪心分行

先以 LF 分段，再顺序读取每段中的空格段和词。这里的“词”是由 U+0020 空格或 Tab 分隔的连续码点序列。

对每个词执行：

1. 计算当前行起点到该词末尾的宽度。
2. 若启用宽度约束、当前行已有词，且加入该词会超宽，提交当前行，在该词起点开始新行。软换行处的分隔空白不进入新行。
3. 若词本身超过可用宽度，逐码点寻找能容纳的最长前缀，提交后继续处理余下码点。初始缩进也参与这一过程。
4. 更新当前行的结束位置，继续读取下一个词。

段落结束时提交最后一行。所有行的尾部空格和 Tab 都从行区间中裁去，因此不影响内容宽度或对齐；段首缩进和行内空格保留。大量初始缩进在窄宽度下可能产生只有空白的行。

例如每个字母和空格的 advance 均为 1，可用宽度为 7：

```text
输入：one two three
输出：one two
      three

输入：abcdefghij
输出：abcdefg
      hij
```

没有宽度约束时，只在显式 LF 处换行。启用自动换行时，约束来自文字节点的 `resolvedSize.x`；节点高度不限制行数。

### 边界规则

| 输入或条件 | 结果 |
| --- | --- |
| 空字符串 | 零行，尺寸 `(0, 0)` |
| 只含空格或 Tab | 一行，宽度为 0，高度为一个行高 |
| `"\n"` | 两个空行 |
| `"A\n"` | 一行文字和一个尾部空行 |
| CRLF / CR / LF | 都表示一个显式换行 |
| 宽度为 0 | 每个正 advance 的非空白码点独占一行；算法始终向前推进 |
| 单个字形比约束更宽 | 该字形单独占一行，允许行宽超过约束 |
| 内容高度超过矩形高度 | 保留全部行，应用垂直对齐；不裁剪 |

### 复杂度

排版先建立 advance 前缀和：

```text
prefix[0] = 0
prefix[i + 1] = prefix[i] + character[i].advance * scale
width(begin, end) = prefix[end] - prefix[begin]
```

前缀和使用 double，返回行宽时转换为 float，减少长文本区间相减时的精度损失。每次候选词宽度查询为 O(1)，无需从行首重新累计。分段、分词、超长词拆分和尾部空白裁去各自只遍历字符常数次。

对于 n 个码点，排版的时间复杂度为 **O(n)**，前缀和及行结果的空间复杂度为 **O(n)**。字形准备还包含字体查找和首次字形光栅化开销，不计入纯排版函数的复杂度。

## 对齐与 Quad 生成

水平对齐分别作用于每一行；垂直对齐作用于整个行块：

```text
horizontalFactor = left: 0, center: 0.5, right: 1
verticalFactor   = top: 0, middle: 0.5, bottom: 1

lineX = (resolvedWidth - line.width) * horizontalFactor
blockY = (resolvedHeight - textHeight) * verticalFactor
baseline(row) = blockY + ascender + row * lineHeight

glyphX = penX + glyph.bearingX * scale
glyphY = baseline - glyph.bearingY * scale
```

字形 Quad 宽高使用 `glyph.width/height * scale`，UV 保持 atlas 像素坐标。SDF 的透明边缘参与 Quad 几何，但不参与 advance 宽度与行高测量。内容尺寸是排版尺寸，不是可见像素的紧包围盒；斜体悬伸等情况可能超出测量矩形。

内容超出矩形时，对齐偏移可以为负。例如居中对齐的超长单字会向矩形两侧溢出。命中仍由 UI 节点的布局矩形决定。

## 更新与测量接口

- `SetText`：内容相同时直接返回；变化时准备字符缓存并重新分行和生成几何。
- `SetLayoutOptions`：校验整组属性；字号或换行模式变化时重新分行，仅对齐变化时复用行结果生成几何。
- `onSizeChanged`：启用自动换行且宽度约束变化时重新分行；其他尺寸变化在存在非默认对齐时更新几何。分行结果记录所用宽度，用于判断缓存是否仍有效。
- 位置、旋转和视觉缩放变化只更新渲染矩阵；颜色变化只更新 Quad 颜色。
- 没有可见字形时保留零面积 Quad，用于保存节点颜色。

几何更新在 Quad 数量相同时原位写入已有槽位，数量变化时通过 `setGeometry` 更新分配及渲染单元的 Quad ID。

原生接口：

```cpp
const TextLayoutOptions &GetLayoutOptions() const;
Result<void> SetLayoutOptions(const TextLayoutOptions &value);
glm::vec2 Measure() const;
Result<glm::vec2> Measure(float maxWidth) const;
glm::vec2 GetTextSize() const;
```

这里的 `Result` 位于 `vke_common` 命名空间。

`Measure()` 返回只有显式换行的自然尺寸。`Measure(maxWidth)` 始终按给定宽度换行，不受节点 `wordWrap` 开关影响；宽度须为有限非负数，非法值返回错误。测量只读取字符缓存，不改变节点矩形、当前排版、atlas 或 GPU 几何。

`GetTextSize()` 返回当前分行结果的尺寸。它采用最近一次 Canvas 布局求解得到的矩形；刚调用 `SetRectTransform` 时，矩形要到 `UIManager::Update(viewportSize)` 才更新。C# 的对应属性为 `TextSize`，两个 `Measure` 重载与原生接口对应，非法测量宽度抛出 `ArgumentOutOfRangeException`。


## 支持范围

当前实现适用于简单的从左到右文字。超长词按码点拆分，不能保证组合字符或 emoji 序列的完整性；未实现 Unicode 行断规则、双向文字、复杂文字 shaping、字体回退链、富文本、省略号或文字裁剪。所选字体无法提供的字符按上述替代字形规则处理。

自动布局和内容自适应是后续功能；当前 `Measure` 提供尺寸查询，不自动修改 RectTransform。
