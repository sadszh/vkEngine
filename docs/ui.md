# UI 系统设计

## 职责与渲染接口

UI 系统负责 Canvas、节点层级、RectTransform 布局、文字、图片、按钮和鼠标交互，并提供场景保存、编辑器和脚本接口。布局结果和几何数据交给 [2D 渲染系统](2d.md) 绘制。

`UIRenderableComponent` 持有材质、渲染单元和 Quad ID。布局更新通过 `updateRenderTransform` 同步矩阵和绘制顺序；几何可原位更新已有 Quad，数量变化时通过 `setGeometry` 更新 Quad 及其实例 ID 列表。卸载时释放渲染单元和 Quad 槽位。

文字颜色、图片颜色和 UV 存在 `QuadManager` 中，属性读取和 `ToData()` 从 Quad 取值。`UIImage` 单独持有纹理的 `AssetRef`，Quad 保存用于绘制的纹理表索引；序列化保存资源句柄。`UIButton` 保存各状态颜色，并将当前显示颜色写入 Quad。

## Canvas 与 UI 节点

`vke_component::Canvas` 是挂在场景实体上的组件，组件类型为 `ComponentType::Canvas`，拥有独立于实体 Transform 层级的 UI 树。UI 节点由 `unique_ptr<UIComponent>` 持有，父指针为非拥有引用。

```text
Canvas（场景组件，拥有根节点列表）
└─ children: unique_ptr<UIComponent>
   └─ 每个节点拥有自己的 children 列表

UIComponent（节点基类）
├─ UIContainer
└─ UIRenderableComponent
   ├─ UIText
   └─ UIImage
      └─ UIButton
```

`include/ui/` 下的节点类型、RectTransform 和节点数据属于 `vke_ui` 命名空间；`Canvas` 与 `CanvasData` 位于 `include/component/canvas.hpp`，属于 `vke_component` 命名空间。

节点类与对应的数据类型位于同一头文件：`ui.hpp` 定义基类与容器，`text.hpp`、`image.hpp`、`button.hpp` 分别定义文字、图片和按钮。`UIManager` 持有场景 registry 引用及悬停、按下目标，负责 Canvas 排序、布局调度和鼠标事件分发；Canvas 和节点保存布局状态。

引擎和编辑器在 `SceneManager::Init()` 后调用 `UIManager::Init(registry)`，在销毁场景管理器前调用 `UIManager::Dispose()`，使 registry 覆盖 UIManager 的生命周期。实例通过 `UIManager::GetInstance()` 访问。

`UIComponent` 保存名称、RectTransform、父子关系、交互状态和最近一次布局结果。所有节点均可包含子节点，`UIContainer` 只提供布局和交互区域。节点创建后地址稳定，Canvas 在 EnTT 存储中移动不会移动内部节点。

原生节点的 `name` 可直接修改。层级修改使用 Canvas 操作函数，以维护所有权和句柄索引；布局修改使用 `SetRectTransform`、`SetSelfLayout` 或 `SetGroupLayout` 校验并标脏，直接赋值 `rectTransform` 不会触发布局重算。`handle` 由 Canvas 分配，`resolvedSize` 由布局系统写入。

Canvas 通过哈希表将 `UIHandle` 映射到节点指针，查找平均 O(1)。句柄为进程内单调分配的 64 位编号，父句柄 0 表示 Canvas 根。节点销毁后句柄失效，原生指针也随之失效；场景恢复后需重新查询节点。

## 屏幕空间

Canvas 当前支持 `screenSpace` 模式，JSON 的 `mode` 也仅接受此值。Canvas 可以挂在任意场景实体上，宿主 Transform 不参与屏幕布局。所有 Canvas 使用当前渲染目标的像素尺寸，左上原点，X 向右，Y 向下。

`referenceResolution` 默认 `(1920, 1080)`，两个分量必须为正。画布等比适配，宽高比变化时扩大对应方向的逻辑范围：

```text
scale = min(viewportWidth / referenceWidth, viewportHeight / referenceHeight)
logicalSize = viewportPixelSize / scale
canvasMatrix = Scale(scale, scale, 1)
```

## RectTransform

`include/ui/rect_transform.hpp` 定义布局和局部视觉变换：

- `anchorMin`、`anchorMax`：相对于父矩形的归一化锚点，每轴满足 `0 <= min <= max <= 1`，默认 `(0, 0)`。
- `offsetMin`、`offsetMax`：相对锚点的逻辑单位偏移，允许负数，默认 `(0, 0)`。
- `pivot`：自身矩形中的旋转、缩放中心，每轴范围 `[0, 1]`，默认 `(0.5, 0.5)`。
- `scale`：二维局部缩放，默认 `(1, 1)`，允许零和负数。
- `rotation`：绕 pivot 的局部 Z 轴旋转，单位弧度，默认 0。

求解公式为：

```text
minimum = anchorMin * parentSize + offsetMin
maximum = anchorMax * parentSize + offsetMax
size = max(maximum - minimum, 0)
position = minimum + pivot * size - parentPivot * parentSize
layoutMatrix = parentMatrix * Translate(position) * Rotate(rotation) * Scale(scale)
geometryMatrix = layoutMatrix * Translate(-pivot * size)
```

Canvas 的父 pivot 为 `(0, 0)`。子节点按父节点未缩放的逻辑尺寸布局，视觉变换沿 UI 层级组合。零尺寸合法，负尺寸收敛为零。布局矩形不裁剪文字；尺寸来源由 `selfLayout` 逐轴指定。

`SetRectTransform` 标记节点布局为脏，`Canvas::SetReferenceResolution` 标记整棵树为脏。`Engine::UpdatePresentation()` 在编辑、暂停和运行状态下均执行以下流程：

1. `UIManager::Update(viewportSize)` 按当前渲染目标尺寸更新布局。
2. `UIManager::ProcessPointer(viewportSize, input)` 处理鼠标事件。
3. `Renderer::Update()` 绘制。

交换链在渲染阶段重建时，布局在下一帧适配新尺寸；零尺寸目标保留上次布局结果。

每次布局更新先排序 Canvas，再检查节点脏标记。任一布局输入、文字度量、图片纹理／UV 或层级变化会触发所在 Canvas 的完整布局计算；没有变化时只同步绘制顺序与渲染变换。布局进行常数次树遍历，节点处理为 O(n)，文字测量另外按字符数线性计算。`resolvedSize` 保存最近一次求解结果，首次求解前为零。需要提前读取布局时，可调用 `UIManager::GetInstance()->Update(viewportSize)`。

## 自动布局与内容自适应

每个节点分别配置自身布局和子节点布局组，两组配置可独立读取和更新：

| 配置 | 原生类型与接口 | C# 属性 | JSON 字段 / Inspector 分组 |
| --- | --- | --- | --- |
| 自身尺寸及参与父布局的方式 | `UISelfLayoutOptions`；`GetSelfLayout()` / `SetSelfLayout()` | `UIComponent.SelfLayout` | `selfLayout` / **Self layout** |
| 直接子节点的测量和排列 | `UIGroupLayoutOptions`；`GetGroupLayout()` / `SetGroupLayout()` | `UIComponent.GroupLayout` | `groupLayout` / **Group layout** |

JSON 中两组对象均可省略；默认自身使用 RectTransform 尺寸并参与父布局，子节点使用锚点布局。修改任一配置均校验输入并标记布局为脏。

| 配置 | 属性 | 值与含义 |
| --- | --- | --- |
| `selfLayout` | `width` / `height` | `rect`（默认）或 `content`，分别指定自身尺寸来源 |
| `selfLayout` | `ignoreLayout` | 默认 false；true 时自身使用锚点布局，不参与父布局组测量及排列 |
| `groupLayout` | `direction` | `none`（默认）、`horizontal`、`vertical`；按兄弟顺序排列直接子节点 |
| `groupLayout` | `padding` | `[left, top, right, bottom]`，布局组内边距，默认全零 |
| `groupLayout` | `spacing` | 相邻参与布局的子节点间距，默认 0 |
| `groupLayout` | `mainAlignment` | `start`（默认）、`center`、`end`，整组子节点沿排列方向对齐 |
| `groupLayout` | `crossAlignment` | `start`（默认）、`center`、`end`、`stretch`，子节点沿垂直于排列方向的轴对齐 |

间距和内边距必须是有限非负数。`mainAlignment` 不接受 `stretch`。枚举、数字和 JSON 字段类型在输入边界校验；无效设置失败并保留原配置。

布局组控制参与子节点的位置，忽略其锚点与偏移位置；`rect` 尺寸取 `max(offsetMax - offsetMin, 0)`，`content` 尺寸取内容测量值。`crossAlignment=stretch` 优先于子节点的尺寸模式，填满父节点扣除内边距后的交叉轴空间。旋转、缩放和 pivot 保留为视觉变换，不参与空间分配。内容超出可用空间时从起始边排列，保持间距并允许溢出。

未被父布局组控制的节点使用 RectTransform 锚点定位；`content` 轴替换对应的计算尺寸，左上边界仍由 `anchorMin * parentSize + offsetMin` 决定，`pivot` 负责旋转和缩放中心。`selfLayout.ignoreLayout` 只影响与父组的关系，该节点仍按 `groupLayout` 排列自己的子节点。自身使用 `content` 尺寸时，测量结果由固有内容和布局组共同决定。

内容尺寸的来源如下：

- 文字：宽度为未自动换行的自然宽度，高度按最终分配的宽度及 `wordWrap` 测量。固定宽度配合内容高度可实现段落自动撑高。
- 图片／按钮：纹理 UV 区域的像素尺寸作为逻辑尺寸；无纹理时固有内容尺寸为零。`preserveAspect` 影响矩形内绘制，不改变布局测量。
- 布局组：主轴累加子节点尺寸与间距，交叉轴取子节点最大尺寸，再加内边距；有固有内容时逐轴取二者最大值。空组的内容尺寸为内边距之和。
- `direction=none` 的容器：没有固有内容，内容尺寸为零；锚点子节点不撑大父节点。需要由子节点撑大面板或按钮时，为其配置布局方向。

求解顺序为自然宽度自底向上测量、实际宽度自顶向下分配、内容高度自底向上测量、位置及高度自顶向下排列。内容宽度父组依据子节点的自然宽度决定自身宽度，随后执行交叉轴拉伸；内容高度父组同理依据子节点首选高度决定高度，避免父子尺寸互相依赖。所有结果在一次 `Canvas::Update` 内完成，尺寸变化同步通知几何与命中区域。运行时新增、删除、换父级或重排，以及修改文本、字号、换行选项或布局配置，均在下一次更新重算。

布局输入随节点复制、场景保存和预制体保留；实际尺寸及排列位置为运行时结果，不覆盖 RectTransform。

## UIText

`UIText` 使用内置 Arial 字体，将 UTF-8 文本转换为字形 Quad，支持字号、水平与垂直对齐、显式换行和按矩形宽度自动换行。`fontSize` 使用 Canvas 逻辑单位，默认 48；RectTransform 的缩放作用于整个节点。`selfLayout.width` / `height` 设为 `content` 时，矩形对应轴使用文字测量尺寸。

`UITextData` 保存 `text`、`color`、材质引用和 `layout` 排版属性。JSON 使用 `fontSize`、`horizontalAlignment`（`left/center/right`）、`verticalAlignment`（`top/middle/bottom`）和 `wordWrap` 字段，默认左上对齐且关闭自动换行。材质句柄 0 使用默认文字材质。

`SetText` 在内容变化时重建排版和几何，`SetLayoutOptions` 更新排版属性，`SetColor` 更新所有字形颜色。没有可见字形时保留一个零面积 Quad，使颜色在后续修改和重建中保持一致。`Measure()` 查询自然尺寸，`Measure(maxWidth)` 查询给定宽度下的换行尺寸；`GetTextSize()` 返回最近求解矩形下的排版尺寸。C# 对应 `LayoutOptions`、四个独立排版属性、两个 `Measure` 重载和 `TextSize`。

分行规则、边界条件、尺寸定义和复杂度见 [文字排版算法](text_layout.md)。

## UIImage

`UIImage` 使用一个 Quad 和共享的内置图片材质，通过纹理索引访问 2D Renderer 的图片纹理表。尺寸变化原位更新 Quad，移动、旋转和缩放更新渲染单元矩阵。图片和文字遵循相同的绘制顺序，每个可渲染节点独立提交一次 draw。

`UIImageData` 的属性包括：

- `texture`：`AssetRef<Texture2D>`，JSON 保存资源句柄；0 表示内置白色纹理，可配合颜色绘制纯色背景。
- `color`：RGBA 值，默认全 1；shader 输出为纹理采样值乘颜色，使用普通 Alpha 混合。
- `uvRect`：`(minU, minV, maxU, maxV)`，默认 `(0, 0, 1, 1)`。各分量在 `[0, 1]` 内，每轴 minimum 不大于 maximum；坐标原点为图片左上角。
- `preserveAspect`：默认 false，填满布局矩形；true 时根据纹理尺寸和 UV 区域计算宽高比，居中等比缩放到布局矩形内。空 UV 区域在等比模式下生成零尺寸几何。子节点始终按完整布局矩形定位。

原生 `SetTexture(AssetHandle)` 校验、加载并注册纹理，成功后替换图片，失败保留原纹理和几何。`SetColor` 修改颜色，`SetUVRect` 校验并修改 UV；三者返回 `SceneResult<void>`。`SetPreserveAspect(bool)` 更新适配方式。

构造时纹理注册失败会记录错误，节点保留颜色和 UV，但不显示图片，可通过成功的 `SetTexture` 恢复。Quad 分配失败时记录错误，后续纹理、颜色和 UV 修改返回错误。纹理表容量、共享索引和逐帧资源释放见 [2D 渲染系统](2d.md#图片纹理表与逐帧更新)。

```json
{
  "type": "uiImage",
  "name": "Background",
  "texture": 0,
  "color": [0.1, 0.15, 0.2, 0.8],
  "uvRect": [0, 0, 1, 1],
  "preserveAspect": false,
  "rectTransform": {"offsetMax": [400, 200]},
  "children": []
}
```

## 鼠标命中与事件

所有 UI 节点都有 `hitTestEnabled` 和 `interactable`，C# 属性为 `HitTestEnabled` 和 `Interactable`。图片、文字和容器默认关闭命中，按钮默认开启；交互默认开启。两个开关仅作用于自身，子节点独立配置。

命中检测按 Canvas 排序和树遍历的逆序寻找最上层目标。`hitTestEnabled=false` 的节点透传鼠标；true 的节点命中后阻挡后方节点。`interactable=false` 的目标仍阻挡后方节点，但不会接收 Enter、Down、Up 或 Click；已有交互通过 Cancel / Exit 结束。透明度不影响命中。可以使用参与命中的容器作为弹窗遮挡层。

命中检测将渲染目标像素坐标逆变换到节点局部空间，与布局矩形比较，因此包含父子旋转和缩放的影响。零尺寸或不可逆变换不命中。图片 `preserveAspect` 留白属于命中区域，父矩形不裁剪子节点的命中范围。

当前支持单鼠标左键，C# 节点暴露以下事件：

| 事件 | 触发条件 |
| --- | --- |
| `PointerEnter` / `PointerExit` | 鼠标进入或离开最上层可交互目标 |
| `PointerDown` | 在目标上按下左键 |
| `PointerUp` | 左键释放，发送给原按下目标，包括在目标外释放 |
| `Click` | 在同一目标上按下和释放，释放后目标仍有效、可交互且位于鼠标下最上层 |
| `PointerCancel` | 按下期间输入停用、命中或交互被关闭，或按键状态被重置 |

按下后移出会保留按下目标，移回再释放可以点击；在外部释放只产生 Up。删除目标后事件停止。失焦和编辑器退出 Run 模式会清理交互状态，暂停模拟时仍处理鼠标事件，可用于暂停菜单。

事件在脚本 Update / LateUpdate 之后、渲染之前同步分发。处理器可以删除节点、移除 Canvas、调整层级或创建节点；分发器通过实体 ID 和 UIHandle 重新查找目标，每次事件分发后更新布局，使修改在本帧绘制中生效。处理器暴露新控件时，本次按下不会转移给尚未收到 Enter 的控件。

C# 的 `UIFunctions` 与 C++ 的 `CSharpUIFunctions` 对应，通过 `CSharpExports.UIFuncs` 导出事件分发和订阅清理入口。`UIManager` 通过 `ScriptManager::GetUIFunctions().dispatchPointerEvent` 调用托管处理器。

订阅按节点句柄共享，通过 `Get` / `Find` 得到的不同托管包装可以取消同一订阅。每次事件使用订阅快照；处理器异常会被记录，其余处理器继续执行，目标失效后停止分发。节点销毁时自动清理订阅；脚本订阅其他长期存活节点时，应在 `Unload` 中解除订阅。

`UIPointerEvent` 包含 `Target`、`Type` 和渲染目标像素坐标 `Position`。`IsHovered` / `IsPressed` 在调用处理器前更新，关闭命中或交互也会立即清零。UI 事件不会消费原始 `Input` 状态，游戏逻辑可独立读取。

独立运行时按 GLFW 窗口尺寸与渲染目标尺寸的比例换算鼠标坐标。编辑器通过 `EditorRenderer::BeginFrame(pointer)` 获取 Scene 图片区域内的坐标和命中状态，再将运行状态与鼠标按键写入 `UIPointerInput`，传给 `Engine::UpdatePresentation(&pointer)`。编辑器仅在 Run 模式接收 UI 输入，新命中限于 Scene 图片区域，按下目标在区域外仍能收到释放或取消。窗口失焦、最小化或光标处于 `GLFW_CURSOR_DISABLED` 模式时停用输入。

## UIButton

`UIButton` 继承 `UIImage`，复用图片纹理、UV 和矩形几何。标签是普通 `UIText` 子节点，可独立编辑；编辑器的 Add Button 自动创建名为 Label 的文字子节点，C# `CreateButton` 创建按钮本身。

按钮的 `color` 表示普通状态颜色，`hoverColor`、`pressedColor`、`disabledColor` 表示悬停、按下和禁用状态颜色。默认分别为白色、0.85 灰、0.65 灰和 0.4 灰，Alpha 均为 1。禁用状态优先；按下并位于鼠标下时使用按下颜色，移出后显示普通颜色。C# 使用继承的 `Color` 以及 `HoverColor`、`PressedColor`、`DisabledColor` 修改颜色。`Color` 的读取和序列化始终返回普通状态颜色，Quad 保存当前显示颜色。

```csharp
var button = canvas.CreateButton("Pause");
button.RectTransform = new RectTransform
{
    OffsetMin = new NVec2(40, 40),
    OffsetMax = new NVec2(240, 110)
};
button.Color = new NVec4(0.15f, 0.3f, 0.5f, 1);
button.HoverColor = new NVec4(0.2f, 0.4f, 0.65f, 1);
button.Click += e => { /* 处理点击 */ };
var label = canvas.CreateText("Label", button);
label.Text = "Pause";
label.RectTransform = new RectTransform
{
    AnchorMax = new NVec2(1, 1),
    OffsetMin = new NVec2(16, 8),
    OffsetMax = new NVec2(-16, -8)
};
label.LayoutOptions = new TextLayoutOptions
{
    FontSize = 24,
    HorizontalAlignment = TextHorizontalAlignment.Center,
    VerticalAlignment = TextVerticalAlignment.Middle,
    WordWrap = true
};
```

JSON 节点类型为 `uiButton`，在 `uiImage` 字段基础上增加 `hoverColor`、`pressedColor` 和 `disabledColor`。

## 顺序、操作与生命周期

Canvas 按 `sortOrder` 从小到大排列，相同值按运行时实体 ID 排列。每棵 UI 树按先父后子、兄弟数组顺序遍历，后访问的节点后绘制。遍历顺序写入渲染单元矩阵的 Z 平移，供 2D Renderer 排序。`SetSiblingIndex` 调整同级节点顺序，索引范围为 `[0, 同级节点数)`。

`Canvas::Add(const UINodeData &, UIHandle parent = 0)` 校验子树、预检资源，再深复制数据、加载资源并创建节点。`SetParent` 接受同一 Canvas 内的父节点或根句柄 0，拒绝形成环；移动后追加到新父节点的子列表末尾，保留 RectTransform 输入并重新布局。`Find(name, parent)` 查找直接子节点，重名时返回第一个匹配；`Get(handle)` 按句柄查找。

`Remove(handle)` 销毁完整子树，释放渲染资源、句柄索引和托管订阅。场景移除 Canvas 或销毁实体时调用 `Canvas::Unload`；移动赋值先卸载目标，再接管源树和索引。订阅清理不触发用户事件。所有树修改在主线程执行，遍历 `children` 时修改其层级会使迭代器失效。

`SceneManager::Reset()` 依次重置 Renderer、UIManager 和脚本状态，再清空 registry：渲染资源批量释放，交互目标和托管订阅清空，Canvas 与节点随 registry 销毁。普通析构只释放节点自身拥有的 C++ 资源，原生调用方单独销毁 Canvas 前须调用 `Unload()`。ScriptManager 在场景创建前初始化、场景销毁后释放，以支持节点卸载时清理托管订阅。

## 保存与预制体

`CanvasData` 保存参考分辨率、排序值和有序根节点列表。节点数据以 `unique_ptr<UINodeData>` 组成树，公共字段为名称、RectTransform、`selfLayout`、`groupLayout`、`hitTestEnabled`、`interactable` 和子节点；具体类型增加各自的内容属性。

JSON 输入先校验，再按 `type` 构造具体节点数据；`CanvasData::ValidateJSON` 和 JSON 构造函数分别通过 `UINodeData::ValidateJSON`、`UINodeData::FromJSON` 递归处理子树。原生代码可直接构造各类节点数据，`Validate()` 检查布局、类型专有约束和子树，`ValidateAssets()` / `LoadAssets()` 负责资源预检和加载。

`UINodeData::Clone()`、节点数据和 `CanvasData` 的复制操作均深复制子树，已解析的资源通过 `AssetRef` 共享。`ToData()` 和 `Canvas::FillData` 将运行时节点导出为数据树，供场景保存、复制、预制体和编辑器运行快照使用。布局结果、句柄、事件订阅及悬停、按下状态不保存。

```json
{
  "type": "canvas",
  "mode": "screenSpace",
  "referenceResolution": [1920, 1080],
  "sortOrder": 0,
  "children": [{
    "type": "uiText",
    "name": "Title",
    "text": "Hello",
    "color": [1, 1, 1, 1],
    "material": 0,
    "rectTransform": {
      "anchorMin": [0.5, 0], "anchorMax": [0.5, 0],
      "offsetMin": [-200, 20], "offsetMax": [200, 100],
      "pivot": [0.5, 0.5], "scale": [1, 1], "rotation": 0
    },
    "children": []
  }]
}
```

场景实体的 `components` 数组使用 `canvas` 类型；其节点树使用 `uiContainer`、`uiText`、`uiImage` 和 `uiButton`，其中 `uiContainer` 仅包含公共字段。

## 编辑器和 C#

编辑器通过 `Scene > New Object > Empty` 创建实体，再通过 Inspector 的 `Add Component > Canvas` 添加 Canvas。Canvas Inspector 展示内部 UI 树，支持添加容器、文字、图片和按钮、删除子树、修改父节点、调整同级顺序，以及编辑布局、文字和颜色。文字节点可编辑字号、对齐和自动换行，并查看自然尺寸与当前排版尺寸；Add Button 创建的标签拉伸到按钮内边距区域并居中。图片和按钮节点可选择纹理、编辑 UV 区域和等比显示选项。所有节点可配置鼠标命中和交互开关，按钮可编辑各状态颜色。旋转输入显示角度，存储与 API 使用弧度。

C# 的类型层级与原生节点对应。`GetComponent<Canvas>()` 获取场景组件，Canvas 通过实体 ID 定位，UIComponent 保存 Canvas 引用和节点 Handle。节点通过 `Find`、`Get`、`Children` 和各类 `Create*` 方法访问。

```csharp
var canvas = GetComponent<Canvas>()!;
var panel = canvas.CreateContainer("Panel");
var rect = panel.RectTransform;
rect.AnchorMin = rect.AnchorMax = new NVec2(0.5f, 0.5f);
rect.OffsetMin = new NVec2(-200, -100);
rect.OffsetMax = new NVec2(200, 100);
panel.RectTransform = rect;
var title = canvas.CreateText("Title", panel);
title.Text = "Hello";
title.SetParent(null); // 移至画布根，保留布局输入
```

`Get` 和 `Find` 找不到节点时返回 null，`IsValid` 检查句柄是否有效。`Children` 是查询时的列表快照，其中的包装仍引用原生节点；层级变化后重新查询列表，已删除节点的包装失效。

依赖原生节点的属性读写或修改操作失败时抛出 `InvalidOperationException`；跨 Canvas 指定父节点或删除节点抛出 `ArgumentException`；`SetSiblingIndex` 的负数索引抛出 `ArgumentOutOfRangeException`。

C# `UIImage.Texture` 使用 `UInt64` 资源句柄（0 为白色），`Color` 和 `UVRect` 使用 `NVec4`，`PreserveAspect` 使用 bool。

```csharp
var background = canvas.CreateImage("Background", panel);
var imageRect = background.RectTransform;
imageRect.OffsetMax = new NVec2(400, 200);
background.RectTransform = imageRect;
background.Texture = 0;
background.Color = new NVec4(0.1f, 0.15f, 0.2f, 0.8f);
background.UVRect = new NVec4(0, 0, 1, 1);
background.PreserveAspect = false;
background.SetSiblingIndex(0);
```

`Canvas.Remove(node)` 同步删除节点及其子树，清理订阅；返回后，被删除节点的 `IsValid` 为 false。传入 null 抛出 `ArgumentNullException`。

```csharp
canvas.Remove(panel); // 同时删除 panel 下的 background；已移到画布根的 title 保留
```

## 未实现功能

世界空间 Canvas、键盘焦点、事件冒泡、拖拽、滚轮控件、网格布局、弹性空间分配、尺寸上下限和嵌套裁剪尚未实现。

## 综合示例

按 [readme.md](../readme.md#build) 准备构建环境后，从仓库根目录运行：

```powershell
scons
.\out\engine.exe .\tests\cfg\test_ui.json
```

示例使用仓库自带字体和图片，参考分辨率为 1600 × 1320，初始窗口为 1200 × 990。调整窗口大小可以观察居中的展示板、四角锚点和等比适配。场景位于 [test_ui_scene.json](../tests/scene/test_ui_scene.json)，资源表位于 [test_ui_desc.json](../tests/scene/test_ui_desc.json)。默认综合展示页的九个分区覆盖以下能力：

| 分区 | 展示内容 |
| --- | --- |
| Text Layout | 18 / 22 / 26 字号、456 / 300 宽度、九种对齐组合、自动换行开关，以及自然尺寸与当前排版尺寸 |
| Texture + UV | 图片拉伸、等比适配、UV 裁切、纹理切换；可点击按钮操作 |
| Anchors + Transforms | 锚点、父子旋转与缩放；点击旋转按钮和负缩放图片可观察事件 |
| Runtime Tree | 创建容器、按钮和标签，换父级、删除子树及句柄失效；卡片可点击删除自身 |
| Draw Order | 兄弟节点顺序、透明混合、多个 Canvas 的排序 |
| Live Updates | 动态布局与颜色，定期更新文字，动画暂停与预制体创建 |
| Button + Pointer Events | 普通、悬停、按下、禁用颜色；记录最近四次 Enter / Exit / Down / Up / Click / Cancel 事件及像素坐标 |
| Hit Test + Pass Through | 前景图片依次切换透传、禁用遮挡、透明遮挡、可点击四种模式，分别统计前后层点击次数 |
| Pause + Lifecycle | 暂停模拟后的模态菜单、子节点独立交互、可点击关闭的纹理按钮预制体 |

右上角的 **Auto layout / F1** 打开自动布局展示页，**Back to overview / F1** 返回综合展示；也可按 F1 切换。两个页面共用 `test_ui.json` 和场景节点树，切换时保留文本、列表及布局配置。自动布局页覆盖其他 Canvas 并阻挡底层鼠标输入；页面打开期间，综合展示的动画和字母快捷键停止更新，Esc 仍可退出。

自动布局页包含四个区域，操作均通过鼠标按钮完成：

| 分区 | 操作与观察 |
| --- | --- |
| Text that sizes its panel | Text 切换三段文字，Width 切换 638 / 380 面板宽度，Font 切换 22 / 26 字号；观察换行、父面板高度、后续标记与尺寸读数一起移动 |
| Buttons that fit their labels | Label 切换 Publish 标签，Padding 切换按钮内边距，Spacing 切换组间距；按钮和外层横向组均按内容宽高测量，点击 Save / Cancel / Publish 也会修改标签 |
| A list that repacks itself | Add 新增一行，Remove 删除末行，Reorder 把首行移到末尾；纵向列表按内容撑高，允许 0–5 行，操作按钮在达到边界时禁用 |
| Alignment + an independent badge | Axis 切换横纵方向，Main 循环主轴起始／居中／末端对齐，Cross 循环交叉轴起始／居中／末端／拉伸，Badge 切换角标的 `ignoreLayout`；观察浮动角标加入排列或恢复右上角锚点定位 |

自动布局页的尺寸读数每 0.2 秒刷新。窗口缩放展示 Canvas 等比适配，Width 单独改变文字面板逻辑宽度以展示重新换行。列表初始三行来自 JSON，后续行通过 C# 创建；布局系统处理行的位置和尺寸，脚本只修改内容、布局参数与层级顺序。

鼠标左键操作界面按钮。事件探针上按下后拖到外部释放可观察 Up，按住时按 D 禁用或切换窗口可观察 Cancel。事件区的 Hover / Press 显示最近事件目标的状态，Clicks 统计探针点击。

文字区点击预览框可依次查看普通段落、超长单词和含显式换行的 UTF-8 文本。下方按钮切换字号、预览框宽度、水平/垂直对齐和自动换行；尺寸读数每 0.25 秒刷新，`Natural` 来自 `Measure()`，`Layout` 来自 `TextSize`，`Box` 是文字矩形的逻辑尺寸。关闭换行后可以观察文字溢出，重新开启后按当前宽度分行。按钮标签使用拉伸锚点与居中对齐，动态卡片和暂停菜单也使用字号及排版属性。

字母快捷键需使用英文输入模式：I / U / T 切换图片适配、UV 和纹理；N / H 创建或移除子树、换父级；B / O 修改兄弟顺序、覆盖层 Canvas 顺序；R 切换展示板与覆盖层的参考分辨率；Space 暂停或恢复示例动画；P 创建预制体；D 切换探针交互；M 打开暂停菜单；运行中按 Esc 退出。

暂停菜单使用 `EngineState.Paused`，脚本 Update 和快捷键处理停止，鼠标事件继续分发。菜单的全屏容器设置 `HitTestEnabled=true`、`Interactable=false`，阻挡背景；子按钮仍可点击恢复运行或退出。预制体在三秒模拟时间后请求销毁，暂停期间计时停止；点击其中的图片按钮也会请求销毁实体，由下一帧的销毁队列处理。

[UIShowcaseScript.cs](../tests/csharp/TestProj/UIShowcaseScript.cs) 负责基础展示、可点击控件、预制体生命周期和页面切换，其中的 `UIAutoLayoutDemo` 管理自动布局页的交互；[UIInteractionScript.cs](../tests/csharp/TestProj/UIInteractionScript.cs) 负责事件记录、命中模式与暂停菜单。固定节点在 Start 中查询并缓存，事件订阅在 Unload 中解除；动态子树销毁时由 UI 系统释放其订阅。静态节点支持编辑器预览，事件和动态节点需要进入 Run 模式。
