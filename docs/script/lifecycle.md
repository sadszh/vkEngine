# 组件与系统的生命周期

C++ 原生组件和 C# EntityScript 共用同一生命周期：从 Data 构造并注册、导出当前 Data、Start、Update / FixedUpdate / LateUpdate、Unload。组件负责局部行为，系统负责所有权、实体有效性和调用时机。

## 1. 系统和组件的关系

- `SceneManager` 持有运行时 registry，统一创建、增删组件、分派生命周期和延迟回收实体。引擎与编辑器共用其 Start / Update / FixedUpdate / LateUpdate 入口，由这些入口分派场景组件（含脚本）。场景系统读取 EngineState，决定组件生命周期的调用时机。
- C# `SceneManager` 持有脚本对象和分发列表，在脚本构造并加载字段后完成注册。脚本用 `ScriptState` 枚举记录生命周期，C++ `ScriptManager` 负责互操作桥接。
- `Engine::UpdateSimulation(deltaTime, accumulator)` 统一调度场景更新、固定步长物理模拟和音频更新；引擎与编辑器共用该入口，各自持有累积时间。渲染由各自的帧循环调用。
- Render、Physics、Audio 等子系统持有注册的渲染单元、物理体、声音等资源。组件构造时完成注册，Unload 撤销注册；Start 表示开始运行。
- 系统遍历 registry 或已注册脚本，保证所属实体、组件有效。外部调用（编辑器、C# 原生接口、异步请求）在入口验证实体与组件。
- 原生组件按需实现 `Start()`、`Update(float deltaTime)`、`FixedUpdate(float deltaTime)`、`LateUpdate(float deltaTime)`，场景系统在编译期筛选钩子。C# 对应钩子为无参、默认空实现，按覆盖掩码注册更新。
- 结构修改只允许在主线程安全阶段进行，不能在遍历受影响组件池或渲染上传回调时修改结构。实体销毁和脚本提交的预制体实例化使用队列。

## 2. Data、构造与导出

### 原生组件

构造函数接受对应 `XxxData` 及必要的实体、Transform、registry 上下文，复制组件配置并完成资源注册。Data 中的资源须先经过 ValidateAssets / LoadAssets。`FillData(XxxData&)` 导出当前状态，`Unload()` 从子系统注销。

灯光是空标签，实际可变状态由 LightManager 持有，因此使用静态 `FillData(entity, data)` 和 `Unload(entity)`。Transform 是必需组件，由实体及变换系统管理，不允许单独删除。

原生运行时组件禁止复制；移动转移注册句柄，保持所属实体和生命周期状态。SkeletonAnimator 的上传回调捕获组件地址，使用 in_place_delete，手动移动时重绑回调。移动赋值释放目标原有资源，只能在安全结构修改阶段使用。组件删除前由系统调用 Unload；Reset 使用独立清理路径。

### C# 脚本

`EntityScriptData` 是导出时使用的快照，由完整类名和自有二进制字段缓冲区组成。加载时，托管管理器借用原生缓冲区，交给生成的 reader 同步解析：先调用脚本的 UInt32 构造函数，再解析所有导出字段，最后由系统注册。原生指针仅在同步加载调用期间有效。依赖其他脚本的初始化逻辑应放在 Start，此时本批脚本均已加载。

`EntityScript.FillData()` 调用生成的 writer，将当前 `[Export]` 字段和属性写入与 reader 相同的二进制格式。支持继承字段、Export 别名、标量、UTF-8 字符串、向量、结构体和一维数组（包括嵌套的一维数组）。导出成员须公开可读写；格式不支持 null，null 字符串或数组、非有限浮点数、超限长度会使导出失败。

C++ 通过 `ScriptManager::GetEntityScriptsData(entity)` 同步获取该实体全部已注册脚本的当前状态。托管端先生成快照，再通过回调借出类名与字节；原生端用 TypeInfo / ValueView 校验并复制为持有二进制的 ScriptStateData。指针仅在回调内有效。

`ExportAllEntities()` 和 `ExportEntitySubtree(root)` 返回 `Result<SceneData>`。原生组件和脚本均导出实时状态；脚本导出失败时整个操作返回错误，编辑器保留原场景文件。脚本导出仅采集标记 Export 的字段和属性，保持生命周期状态不变。

脚本实时状态由 C# 实例持有。编辑器通过托管注册表查询脚本名称并读取展开脚本的字段，场景保存从实例导出当前状态。

## 3. 创建与 Start

### 场景和预制体

初始场景经 `LoadSceneFile → PrepareSceneData → Instantiate` 加载。异步预制体请求经 `RequestInstantiate` 入队，在运行帧取出队列快照，依次执行 `LoadSceneData → Clone → PrepareSceneData → Instantiate`。

Instantiate 的顺序：

1. 验证 Ready 阶段、父实体和根变换的单根条件；本批包含脚本时检查 ScriptManager 已初始化。脚本二进制须在读取或导出入口通过校验。
2. 分配本批全部实体和 GameObject / Transform，建立层级并计算世界变换。
3. 从 Data 构造全部原生组件，完成子系统注册。
4. 批量构造、解析并注册本批脚本。
5. 运行模式立即启动本批组件；编辑模式不调用 Start。

Start 执行时本批全部组件已可用。原生组件先启动，再调用本批脚本 Start；实体之间和脚本之间的顺序没有约定。没有脚本的实体同样启动原生组件。

Instantiate 显式调度加载与启动，loadScripts 构造并注册脚本。批次原生启动遍历本批实体映射，托管 Start 接收本批带脚本的实体 ID，每个实体一次。全场景 Start 遍历有 Start 钩子的原生组件池，再通过托管 StartAll 启动已注册脚本。托管端在调用脚本前生成脚本对象快照，随后遍历快照分派回调。

编辑器从编辑切换到运行时，显式调用 SceneManager::Start，启动当前场景全部实体的组件。调用方负责保证每轮运行只调用一次全场景 Start。独立运行入口已处于运行模式，初始场景在 Instantiate 时启动。运行中新加载的批次只启动本批组件。

Start 内发出的实例化请求若发生在正在处理的实例化批次内，留到下一批；进入运行时发出的请求在随后的运行帧处理。编辑期间不处理请求，Reset 或退出时清空。Load 发生异常时不提供回滚保证；生命周期钩子异常由托管端捕获、记录，并继续分派后续回调。

### 空实体和组件增删

`CreateEntity(name, pos, scl, rot, isStatic)` 创建带 GameObject / Transform 的实体。`AddComponent(entity, ComponentType)` 或相应 Data 重载构造并注册组件，运行时立即调用该新组件的 Start，编辑时由进入运行的全场景 Start 统一启动。`RemoveComponent` 先 Unload，再删除组件。

接口拒绝无效、待销毁实体和管理器退出期间的修改，也拒绝重复添加、删除不存在的组件。SkeletonAnimator 要求使用提供网格、材质和骨骼的数据重载。Script 的动态增删尚未接入这些接口；C# 的实体操作接口为异步 InstantiatePrefab、DestroyEntity、IsPendingDestroy。

## 4. 每帧调度

引擎与编辑器在调用场景 Update 前读取一次帧 deltaTime（秒），同一帧的原生 Update、LateUpdate 和音频更新共用该值。Engine 的固定循环将调度所用的 stepTime 显式传给 SceneManager::FixedUpdate 和 PhysicsManager::FixedUpdate，原生组件通过参数接收步长，与 Jolt 模拟使用相同的值。

```text
帧边界处理销毁请求（编辑时也执行）
运行时：
  处理实例化请求（含新组件 Start）
  C# Update → 原生 Update（动画 CPU 采样及根运动）
  零次或多次：
    物理接触事件 → C# FixedUpdate → 原生 FixedUpdate（角色控制器）
    → Jolt 模拟 → 物理结果同步到 Transform
  原生 LateUpdate → C# LateUpdate → 音频位置同步
渲染：帧 fence 完成 → 上传动画姿态及相机、灯光等快照 → 提交渲染
```

物理模拟在场景 FixedUpdate 返回后推进，场景通过物理更新监听器同步 Transform。音频在场景 LateUpdate 返回后更新。调度层在推进物理和音频前重新检查运行状态，脚本回调中暂停或终止运行会阻止后续系统更新。

编辑模式下，组件保持加载状态，实例化队列和三个运行更新阶段暂停处理，渲染可以继续。

SkeletonAnimator 构造时以零时间计算初始姿态，运行时 Update 推进动画、采样混合并应用根运动。渲染回调在帧 fence 完成后，将已算出的姿态上传至 GPU。

AudioSource 构造时初始化声音，Start 根据 `playOnStart` 决定是否播放。SetClip 负责重新加载声音，主动播放由调用方调用 Play / Replay。CharacterController 在 FixedUpdate 推进，物理模拟后的场景监听器负责同步结果。

托管脚本状态为 `NotStarted → Starting → Started`，卸载时进入 `Unloading → Unloaded`；尚未启动或正在启动的脚本也可以卸载，Reset 直接进入 `Unloaded`。仅 `NotStarted` 可以开始启动；Start 回调返回后，仅仍为 `Starting` 的脚本转为 `Started`，避免覆盖回调期间发生的卸载或 Reset。Start 异常记录后仍按同样规则转为 `Started`，不重试启动。

托管分派复制回调列表，并在调用前确认脚本已注册且状态为 `Started`。`Starting` 期间的重入更新不会执行该脚本。没有覆盖 Start 的脚本同样须经过系统启动才能 Update。仅标记实体待销毁不会立即停止本帧回调，实际卸载发生在下一清理边界。

## 5. 卸载、销毁和 Reset

DestroyEntity 立即收集并标记当前子树，层级和资源保留至帧边界清理。ProcessDestroyRequests 在帧边界取待销毁快照，先卸载整批 C# 脚本，再回收整批原生组件和实体。所有脚本 Unload 执行时，本批原生组件及父子关系仍可用。

托管清理按实体进行：系统将仍已注册的脚本标记为正在卸载，调用 Unload，标记已卸载，移除注册和分发记录，最后清理 RigidBody / Sensor 回调。系统对每个脚本最多分派一次 Unload；回调异常记录后继续标记失效和清理。

脚本通过 DestroyEntity 请求系统销毁所属实体；脚本自身资源统一在 Unload 中释放。Unload 是用户可重写的生命周期回调，直接调用它不会注销脚本或销毁实体。独立移除脚本须由系统提供接口，目前尚未接入。

原生清理统一调用组件 Unload，再删除 registry 中的组件，清理父实体引用，最后销毁实体。移除刚体/传感器时在 BodyID 仍可查询时清理对应托管物理回调。重新添加物理组件后须重新获取 C# 包装对象和订阅回调。

Dispose 场景时停止接受创建、清空实例化队列、循环处理所有剩余销毁请求，再清理残留托管脚本与物理监听。

Reset 是批量系统复位，不逐个执行 Unload。系统直接清空注册及资源；托管脚本旧引用的状态设为 `Unloaded`，不能再次注册或导出字段。脚本放在 Unload 中的资源清理不会在 Reset 时执行。引擎停止运行更新，加载后显式进入运行。编辑器停止运行时调用 Reset，并在暂停状态下恢复运行前快照。

## 6. 接口与资源约束

- C++ SceneManager：Start / Update / FixedUpdate / LateUpdate 统一驱动场景系统；ScriptManager 提供同名托管桥接。调用方不能绕过场景系统直接推进某个运行组件。
- C# 导出函数表：生命周期：Load、Start、StartAll、Update、FixedUpdate、LateUpdate、Unload、UnloadEntities、Reset；组件回调：UnregisterComponentCallbacks；脚本查询：HasScripts、GetScriptList；数据读写：GetScriptData、SetScriptData、GetEntityScriptsData。原生与托管程序集必须一起构建部署。
- Camera / Transform 使用紧凑存储，不允许长期保存组件地址。相机回调通过 registry / entity 查询，卸载时注销；渲染单元持有模型矩阵副本。
- 变换应通过 SceneTransformSystem 修改，使子节点、渲染、阴影和 UI 同步。直接修改 Transform 只影响组件本身。
- 单独移除 RenderableObject / SkeletonAnimator 时调用 WaitIdle，等待 GPU 完成资源使用后再释放。其余注册和移除通过帧同步路径执行。
- 尚未实现：C# 创建空实体、动态增删组件和异步实例化结果。

## 7. 脚本状态查询与字段编辑

`HasScripts(entity)` 查询托管注册表，判断实体是否挂有脚本；`GetScriptList(entity)` 返回脚本完整类名列表，供编辑器显示脚本节点。Inspector 展开节点时调用 `GetScriptData(entity, className)`，每帧显示当前导出字段；折叠节点只查询名称，隐藏的 Inspector 不查询脚本。

同一实体上同类脚本最多一个。托管注册表按实体和脚本完整类名两级索引，`GetScriptData(entity, className)` 直接定位实例并读取当前二进制字段；`GetEntityScriptsData(entity)` 获取实体全部脚本的二进制数据，供场景导出使用。数据仅在同步回调内有效，C++ 通过复制取得独立所有权。

Inspector 通过 `TypeInfo` / `ValueView` 读取二进制字段，支持数值、向量、字符串、结构体及数组。数组支持增删和分页，每页最多绘制 64 个元素；分页限制控件数量，展开脚本仍完整传输字段数据。含 NUL 字节的字符串显示为只读，因为文本控件不能完整表示它们。

`ValueEditor` 在本帧缓冲区中修改字段：定长数值及等长字符串原地写入，字符串长度变化或数组增删时重排二进制并验证布局。修改在绘制结束后执行，调用方在缓冲区重排后须从 `Root()` 重新获取视图。修改成功后，`SetScriptData` 将已校验的字节写回现有实例；具体写回和 setter 失败语义见 [interop.md](interop.md)。

## 8. 编辑器运行与还原

Start/Stop 按钮和 F6 提交切换请求，由主线程在帧边界处理。Start 先处理待销毁实体并导出 `SceneData` 快照，再清空输入、时间及固定更新余量，进入 Run 并启动组件。导出失败时保持 Edit。

Stop 调用 `SceneManager::Reset`，清空旧实体选择和输入状态并恢复鼠标，再以暂停状态实例化快照。还原成功后释放快照并进入 Edit；还原过程不触发 Start。可返回的还原错误会保留快照，使运行更新暂停、界面保持只读，允许再次 Stop 重试。组件构造和 C# 加载异常遵循加载接口既有的失败行为。

运行期间可以浏览实体和脚本实时字段；Inspector 修改、组件增删、场景保存与创建、资源导入与修改入口禁用。编辑期间保持渲染，运行更新由编辑器和引擎状态共同控制。

快照覆盖实体层级、原生组件数据和脚本导出字段，持有组件资源引用。停止还原时丢弃运行期间创建的实体并恢复被删除的实体。资源文件、资源数据库、脚本静态字段及外部副作用不在快照范围内。

实现入口：[scene.hpp](../../include/scene.hpp)、[scene.cpp](../../src/scene.cpp)、[SceneManager.cs](../../csharp/EngineCore/SceneManager.cs)、[Script.cs](../../csharp/EngineCore/Script.cs)。二进制协议见 [interop.md](interop.md)。
