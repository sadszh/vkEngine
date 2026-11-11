# SceneData 资源化与实例化

SceneData 统一表示场景和预制体的数据，由加载、展开、资源解析三个阶段逐步准备。AssetManager 管理预制体及其依赖资源，SceneManager 根据准备好的数据创建运行时实体和组件。

接口返回 `Result<T>`，即 `std::expected<T, std::string>`。

## 加载入口与生命周期

| 入口 | 持有方式 | 用途 |
| --- | --- | --- |
| 资源数据库或资源 JSON 中登记的 SceneData | AssetManager 按 AssetHandle 加载和缓存 | 作为预制体被引用或直接实例化 |
| 直接打开的场景文件 | 调用方持有临时 SceneData | 准备并实例化后释放 |

两种入口使用相同的数据格式和准备流程。临时场景中的预制体引用通过 AssetManager 解析。运行时组件持有所需资源的引用，生命周期独立于加载时使用的 SceneData。

## 数据结构与阶段

SceneData 使用 EnTT registry 按类型存放组件数据，包含：

- 实体局部 ID 与数据实体的映射。
- 实体属性、组件数据和父子关系。
- 实体上的可选预制体引用及覆盖数据。
- 作为预制体使用时的根实体标识。
- 当前准备阶段。

```cpp
enum class SceneDataStage
{
    Parsed,    // 已反序列化并通过结构校验
    Expanded,  // 已展开预制体并应用覆盖
    Ready      // 已加载最终依赖资源
};
```

准备操作修改同一份 SceneData，并在每个阶段成功完成后推进状态。失败返回错误，调用方可以丢弃该份数据重新准备。Ready 数据可重复用于实例化。

校验按阶段分工，后续阶段依赖前面已经建立的约束：

- 私有 `validateParsed()`：检查实体映射、必要组件、父节点、父子环，以及已声明的 prefabRoot。`FromJSON` 在字段解析后执行一次，成功后标记 Parsed。
- `ValidatePrefab()`：对已校验的数据补充检查必须声明 prefabRoot。
- `ValidateExpanded()`：展开完成后检查没有残留 PrefabReference；展开算法通过重映射已校验的子树保持层级合法性。
- `ValidateReady(requireSingleRoot)`：实例化前检查 Ready 状态；指定 rootTransform 时，以已声明的 prefabRoot 或根实体数量确认单根。SceneManager 检查运行时父实体；数据包含脚本时，还检查 ScriptManager 是否已初始化。

JSON 数据统一通过 FromJSON 解析并校验：组件字段在构造前由相应 Data::ValidateJSON 检查，场景结构由私有 validateParsed 检查。手工构造或修改数据的调用方负责保持相同约束，修改后的数据从 Parsed 重新准备。阶段由准备操作在成功后推进，校验函数保持阶段不变。Clone 保留源数据阶段，展开失败保留输入数据及其阶段。

组件校验由各组件 Data 封装：

- `ValidateJSON(json)` 检查自身必填字段、类型及参数约束；SceneData 仅检查分派用的 type、重复组件并添加实体和组件错误上下文，通过后才构造组件 Data。
- `PhyscisShapeData::ValidateJSON` 由刚体、传感器和角色控制器复用；`SkeletonAnimationData::ValidateJSON` 由动画组件调用；TransformData 负责变换字段。
- `ValidateAssets()` 通过 `AssetManager::ValidateXXX(handle)` 检查组件的资源依赖；`LoadAssets()` 使用 `AssetManager::LoadXXX(handle)` 加载资源并填充引用。
- `ScriptStateData::FromJSON()` 在文件读取时查找脚本元数据、检查并编码状态和校验互操作数据长度；运行时导出入口校验返回的二进制。SceneManager 实例化时只检查所需的脚本管理器是否可用，加载时直接借用已有缓冲区。

公共 json_validation 工具提供数值、向量、字段类型和必填项检查。各组件定义并校验自身消费的字段，忽略未知扩展字段。直接调用 JSON 构造函数前，调用方须先调用 ValidateJSON。

AssetManager 缓存只读的 Expanded 数据，其中已完成预制体内部的递归展开和覆盖。引用时将缓存内容复制到目标 SceneData，重映射实体并应用当前引用的覆盖，再解析最终资源依赖。调用方可复用准备好的结果。

缓存记录展开时依赖的预制体句柄。资源重新加载时，使其自身及所有传递依赖它的展开缓存失效，后续加载重新构建；已创建的运行时实体保持当前状态。

加载、准备和缓存失效在主线程同步执行。

AssetHandle 标识资源，局部 ID 标识一份 SceneData 内的实体，运行时 entt::entity 标识 SceneManager 中的实体。展开和实例化分别建立各自的实体映射。

## 资源引用

组件数据通过统一的 AssetRef 表示资源引用：

```cpp
AssetRef<T> reference(handle);
reference.Handle();             // 序列化标识
reference.Get();                // 已解析的 shared_ptr
reference.SetHandle(newHandle); // 更换句柄并清空指针
reference.Resolve(asset);   // 填充解析结果
reference.SetAsset(asset); // 从运行时资源提取句柄和指针
```

反序列化只设置句柄，资源解析阶段填充指针，序列化始终输出句柄。替换句柄时同步清空旧指针，资源访问使用已经解析的指针。

`handle == 0` 表示空引用，是否允许为空由组件决定。非零句柄解析失败时返回错误。复制已解析的 AssetRef 共享资源所有权；动画播放进度等实例状态由运行时组件分别持有。

RenderableObjectData、SkeletonAnimatorData、AudioSourceData，以及 CanvasData 节点树中的 UITextData，其材质、网格、骨骼、动画和音频引用均采用这一类型。实体对预制体的引用使用 `AssetRef<const SceneData>`，只读访问缓存数据。

## 预制体展开与覆盖

实体的预制体句柄为空时，按其数据创建独立实体；句柄非零时，展开对应 SceneData 并应用当前实体上的覆盖。

普通场景允许多个根。作为预制体使用的 SceneData 声明一个根，全部实体属于该根的子树。

展开一个引用的步骤：

1. 获取被引用预制体的 Expanded 缓存；缓存缺失时递归展开并缓存成功结果。
2. 将预制体根映射到当前引用实体，为其他实体分配目标数据实体和局部 ID。
3. 重映射预制体内部父子关系，根使用引用实体的父节点。
4. 应用引用实体上的属性和组件覆盖。
5. 保留当前场景中挂在引用实体下的附加子实体。

覆盖规则如下：

| 数据 | 合并规则 |
| --- | --- |
| name、static、transform | 显式提供则替换，否则继承 |
| 普通组件 | 按组件类型匹配，存在则整体替换，否则添加 |
| 脚本 | 按 className 匹配，存在则替换完整状态，否则添加 |

引用实体保留属性是否出现的信息，例如 `static: false` 表示明确覆盖。组件内部缺省字段采用该组件的默认值。同实体同类脚本最多一个，脚本状态须符合对应元数据。

Transform 覆盖替换根的完整 local transform；内部子实体保留各自的 local transform。

展开通过当前递归路径检测循环引用，同一预制体的多次引用分别建立映射。创建运行时实体前，校验重复 ID、父节点有效性、父子环和预制体根；错误包含资源、实体及引用路径。

支持根属性覆盖、整组件覆盖和附加子实体。内部子实体覆盖、继承内容删除和字段级覆盖尚未实现。

## 数据准备接口

以下接口由 AssetManager 提供，负责获取数据、展开预制体和加载最终依赖：

```cpp
// 获取只读 Expanded 数据，缓存缺失时加载并展开。
Result<std::shared_ptr<const SceneData>> LoadSceneData(AssetHandle handle);

// 读取普通场景文件，返回调用方持有的 Parsed 数据。
Result<SceneData> LoadSceneFile(const std::filesystem::path& path);

// 统一执行展开和最终资源解析。
Result<void> PrepareSceneData(SceneData& data);

Result<void> InvalidateSceneData(AssetHandle handle);
Result<std::shared_ptr<const SceneData>> ReloadSceneData(AssetHandle handle);
```

PrepareSceneData 按当前阶段执行展开和资源解析，Ready 数据直接返回成功。展开和资源解析分别由私有 expandSceneData、resolveSceneAssets 实现。依赖资源在覆盖完成后加载，仅解析最终组件数据中的引用。

各组件 Data 分别提供只读的 `ValidateAssets()` 和负责加载的 `LoadAssets()`，由组件处理可选引用和已解析引用。私有 `resolveSceneAssets` 先对全部组件调用 `ValidateAssets()`，全部通过后再对全部组件调用 `LoadAssets()`，全部成功后标记 Ready；加载接口要求此前验证成功。

`AssetManager::ValidateMesh(handle)`、`ValidateMaterial(handle)` 等接口只读检查资源记录和文件。材质验证进一步检查 shader 和各纹理依赖，顶点/片元 shader 验证分别检查两个文件，错误包含相应的依赖上下文。文件存在检查不保证资源内容可被解码，实际加载仍可能失败。资源由 `AssetManager::LoadXXX(handle)` 加载，组件将结果写回 `AssetRef`。

缓存数据通过 `Clone()` 复制为可修改的 SceneData，再调用 PrepareSceneData。ReloadSceneData 清除自身及传递依赖缓存，并立即重建指定资源；其他受影响资源在下次请求时重建。缓存失效由调用方显式触发，旧 shared_ptr 持有的快照继续有效。InvalidateSceneData 在读取所有受影响的资源记录成功后才清理缓存和依赖关系；读取失败返回错误并保留原缓存及依赖关系，ReloadSceneData 将该错误传给调用方。

ScriptStateData 持有 className 和二进制状态，预制体复制和 SceneData 快照使用这一数据表示。文件读取时由 FromJSON 按脚本元数据编码，文件输出时由 ToJSON 转回 JSON。SceneData::ToJSON 返回 Result，以传播类型缺失或二进制无效等错误。脚本元数据必须在读取含脚本的场景文件前加载。

## 运行时实例化

```cpp
struct InstantiateOptions
{
    entt::entity parent = entt::null;
    std::optional<TransformData> rootTransform;
};

// SceneManager 接口。
Result<void> Instantiate(
    const SceneData& data,
    const InstantiateOptions& options);
```

Instantiate 接受 Ready 数据，要求脚本数据已经在读取或导出入口通过校验；后续复制或修改必须保持脚本类型与二进制数据有效。parent 为空时挂到世界根，否则挂到指定的有效且未进入待销毁状态的实体。rootTransform 用于单根数据：提供时替换根的 local transform，省略时保留数据中的值。多根场景的各根使用各自的 local transform。

实例化顺序：

1. 检查场景管理器可用、数据处于 Ready 阶段、父实体有效，以及 rootTransform 的单根条件；数据包含脚本时，检查 ScriptManager 已初始化。
2. 分配本批全部实体，建立数据实体到运行时实体的映射。
3. 建立父子关系和外部挂接，计算世界变换。
4. 构造全部原生组件，注册到渲染、物理、音频等系统。
5. 创建并注册本批全部 C# 脚本。
6. 运行模式下先启动本批原生组件，再启动本批脚本；编辑模式下由进入运行的入口统一启动。实例化完成后返回成功状态。

每次实例化创建独立的实体和组件状态。组件的构造函数接收对应的 Data 并完成系统注册，场景加载和动态添加组件共用此入口。实例化在创建阶段发生异常时不提供回滚保证。Start 的调用规则见 [组件与系统的生命周期](script/lifecycle.md)，其外部副作用由脚本负责。

场景格式、引用、脚本编码和缺失资源在数据读取、展开及资源解析期间校验。C# 脚本创建异常遵循未处理异常行为。音频资源记录和路径错误、音频文件读取失败、解码接口返回的错误及零帧结果通过 Result 返回。AudioSource 构造时的音频引擎不可用或播放资源创建失败，以及其他底层资源解码器和 GPU 分配中的致命错误会使引擎退出。

Instantiate 返回成功或错误信息。数据实体到运行时实体的映射仅用于实例化内部，完成后释放。销毁通过 DestroyEntity 发起延迟销毁请求，按当前子树规则回收，具体顺序见 [组件与系统的生命周期](script/lifecycle.md)。

## 场景保存

ExportAllEntities 导出运行时的扁平场景快照。保留预制体引用与覆盖结构的编辑保存，需要编辑器持有源场景文档或实例来源信息。

导出时，C++ 通过 `ScriptManager::GetEntityScriptsData` 获取 C# 实例的当前导出字段，并保存为二进制 ScriptStateData。编辑器在导出和 `SceneData::ToJSON` 均成功后打开文件并写入 JSON；导出或转换失败时记录错误并保留原文件。编辑器脚本字段编辑与写回尚未实现。

## 文件格式与使用

场景使用 `objects` 数组。实体的 `prefab` 字段保存 SceneAsset 句柄，预制体文件的 `prefabRoot` 指定根实体局部 ID。Transform 使用 `pos`、`scl`、`rot` 字段，四元数顺序为 x、y、z、w。

```json
{
  "prefabRoot": 1,
  "objects": [
    {"id": 1, "name": "Enemy", "static": false, "components": []},
    {"id": 2, "parent": 1, "prefab": 1024, "name": "Weapon"}
  ]
}
```

直接加载场景文件使用 `LoadSceneFile → PrepareSceneData → Instantiate`。按句柄生成预制体使用 `LoadSceneData → Clone → PrepareSceneData → Instantiate`，每一步检查返回的错误。

## 异步实例化与验证

C# 通过 `SceneManager.InstantiatePrefab(handle, position, rotation, scale, parent)` 提交请求，返回值表示是否成功入队。运行帧在销毁处理之后、脚本 Update 之前处理当前请求快照，Start 中新增的请求留到下一运行帧。暂停期间保留请求，退出时清空。处理失败时记录日志，父节点在入队和执行时分别检查。编辑器保存预制体后可接入 ReloadSceneData。

交互测试配置为 `tests/cfg/test_prefab.json`，场景和球体预制体位于 `tests/scene/test_prefab_scene.json`、`tests/scene/test_ball_prefab.json`。从仓库根目录运行 `out/engine.exe tests/cfg/test_prefab.json`；每次左键按下生成一个带刚体的球，Esc 退出。

验证同一预制体多次实例化、嵌套引用、根组件覆盖、指定父节点与变换、循环引用及缺失资源报错。销毁其中一个实例后，其他实例和缓存源数据应保持正确；释放临时 SceneData 后，运行时组件应继续正常工作。
