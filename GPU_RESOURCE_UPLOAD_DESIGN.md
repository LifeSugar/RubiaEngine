# GPU 资源上传设计记录

记录日期：2026-09-11。

本文记录当前上传实现的问题、Godot / Unity 本地源码对照，以及后续重构方向。
它是设计讨论记录，不代表下述方案已经实现。CPU Asset 体系暂时保持现状，
本轮重点是 GPU 资源的增量准备、上传、更新、同步和回收。

## 1. 当前实现与问题

现有启动流程：

```text
App 首帧之后启动 CPU 加载
  → std::async 解析 shader、导入模型、解码贴图
  → 得到独立的 PreparedContent
  → 提交 SceneResourceRequest
  → VulkanScenePreparation 分批上传
  → 准备场景 pipeline
  → Ready
  → App 启用场景并移交 CPU 内容
```

当前已经完成的职责迁移：

- App 不再为首次场景加载持有上传 CommandPool 和 UploadContext。
- VulkanScenePreparation 管理上传批次、完成查询、pipeline 准备和取消清理。
- VulkanRenderer 对外提供 begin、advance、status、activate、cancel 接口。
- 正常推进通过 fence 查询完成状态；最多保留一个尚未确认完成的上传批次。
- 每批在完整资源之间检查 4 ms、16 MiB、16 个资源的软预算。
- GPU 资源创建、staging 填充和命令录制仍在主线程执行，上传使用 graphics queue。
- pipeline 创建仍在主线程执行；取消清理可能等待已提交的工作。

代码入口：

- [SceneResourcePreparation.hpp](engine/render/SceneResourcePreparation.hpp)
- [VulkanScenePreparation.hpp](renderer/vulkan/VulkanScenePreparation.hpp)
- [VulkanScenePreparation.cpp](renderer/vulkan/VulkanScenePreparation.cpp)
- [AppContentLoading.cpp](apps/editor/AppContentLoading.cpp)
- [RenderAssetCache.hpp](renderer/vulkan/RenderAssetCache.hpp)
- [UploadContext.hpp](renderer/vulkan/UploadContext.hpp)
- [AppTextureReimport.cpp](apps/editor/AppTextureReimport.cpp)

这次迁移解决了“由谁执行”，尚未消除 Demo 加载流程带来的限制：

1. SceneResourceRequest 以 models、单个 materialTemplate 和 presentProgram 为入口。
   Mesh、Material、Texture 通过模型依赖关系找到；materialTemplate 重复表达了材质已有的信息，
   presentProgram 则属于呈现配置。
2. 一个 renderer 只管理一个场景准备会话，要求场景和 cache 为空。
3. RenderAssetCache 同时承担 GPU 资源存储、待上传列表和上传游标管理；初始化会重建缓存，
   不适合运行过程中持续增量添加资源。
4. cache 只有一套共享材质布局及相关 descriptor 分配逻辑。
5. 上传设施随一次场景准备创建和结束，尚不是长期运行的后端服务。
6. 纹理重导入仍在 App 中创建 Vulkan 上传对象，并通过 waitIdle 保证替换安全。

因此，仅给请求增加 textures、meshes 等 handle 列表不足以完成通用化。
需要一起调整缓存、调度、完成语义和生命周期。

## 2. Godot 本地源码对照

源码目录：`F:/godot`。`version.py` 标记为 **4.7.0 beta**。
以下结论来自这份本地源码，不外推到其他版本和所有渲染后端。

### 2.1 资源语义与设备传输分层

纹理更新路径：

```text
TextureStorage::texture_2d_update
  → 检查纹理尺寸、格式、layer 等
  → RenderingDevice::texture_update(RID, layer, data)
  → staging、复制操作、资源依赖
  → RenderingDeviceDriver
  → Vulkan 等具体设备后端
```

TextureStorage 理解纹理对象；RenderingDevice 处理 GPU 资源和数据。
设备传输层不需要知道资源属于哪个模型或场景。

`RenderingDevice::_buffer_update` 按 buffer、offset、size、data 工作，
能够分段申请 staging，将复制操作加入命令图。动态持久映射 buffer 还有直接写入路径。
这说明不同更新频率和资源用途可以采用不同传输策略。

### 2.2 初始传输、资源更新与绘制依赖

源码同时存在 TransferWorker 管理的初始数据传输，以及加入命令图的资源更新路径。
TransferWorker 持有 staging buffer、command pool、command buffer、fence、barrier 和操作计数。
这里的 worker 是传输上下文，不等同于“一次上传新建一个线程”。

资源能够记录自己依赖的 transfer worker 和操作序号。绘制使用资源时收集依赖，
提交时安排 transfer 提交、semaphore 等待和 barrier。
因此，上层不一定需要先在 CPU 上等待上传 fence 完成，才能组织后续绘制；
后端可以让 GPU 按正确的依赖顺序执行。

设备初始化尝试取得 transfer queue family，不支持时使用 main queue family。
不能据此假定所有设备都有独立传输硬件或一定能与绘制并行。

### 2.3 staging 与延迟回收

- staging 按块管理、复用，并受容量限制。
- 容量不足时存在 flush / stall 路径，并非所有上传调用都保证无阻塞。
- 资源释放先进入按帧保存的待销毁列表。
- 复用帧时先等待该帧完成，再清理待销毁资源。

PipelineCacheRD 独立管理 pipeline 变体，包含顶点格式、framebuffer 格式、render pass、
wireframe 和 specialization 等条件。上传纹理不需要携带呈现程序。

### 2.4 源码定位

| 相对于 `F:/godot` 的路径 | 重点符号或位置 |
| --- | --- |
| `servers/rendering/renderer_rd/storage_rd/texture_storage.cpp` | `_texture_2d_update`，约 1590 行 |
| `servers/rendering/rendering_device.cpp` | `_buffer_update`，约 1087 行 |
| `servers/rendering/rendering_device.cpp` | `texture_update`，约 2254 行 |
| `servers/rendering/rendering_device.h` | `TransferWorker`，约 1690 行 |
| `servers/rendering/rendering_device.cpp` | `_submit_transfer_worker`，约 7272 行；`_submit_transfer_workers`，约 7353 行 |
| `servers/rendering/rendering_device.cpp` | `_free_pending_resources`，约 7897 行；`_begin_frame`，约 8043 行 |
| `servers/rendering/rendering_device_graph.h` | `ResourceTracker`、buffer / texture 更新命令 |
| `servers/rendering/renderer_rd/pipeline_cache_rd.h` | `PipelineCacheRD` |

## 3. Unity 本地源码对照

源码目录：`E:/unitysrc/unity20190433`。
`Configuration/BuildConfig.pm` 标记为 **2019.4.33f1**，目录中包含定制改动。
以下描述以实际读到的代码为准，不将其当作未经修改的官方版本。

### 3.1 类型专用流程接入共享调度器

```text
AsyncUploadTexture / MeshAsyncUpload
  → AsyncUploadManager
  → GfxDevice
  → Vulkan 等具体后端
```

AsyncUploadManager 是长期存在的服务，管理读取请求、CPU 处理依赖、上传队列和
渲染线程回调。纹理和 mesh 各自提供 handler，解释类型特有的数据和处理步骤。

例如，mesh 的上传回调在真实图形设备线程执行，最终调用 InitializeBufferInternal
初始化 vertex / index buffer。纹理路径则处理 mip、格式转换、创建、上传内存获取和拷贝等。

这条异步加载路径不是 Unity 所有资源更新的唯一入口，不能将其等同于所有 GfxDevice 操作。

### 3.2 时间片、继续执行与内存压力

AsyncResourceUpload 按时间片消费队列。回调可以返回：

- Complete：请求处理完成。
- Requeue：重新排队，可继续处理。
- RequeueDelayed：留到下一帧处理。

纹理暂时无法取得上传内存、或后续处理条件尚未满足时，可以延后重试。
时间片在处理步骤之间检查，不意味着任意一次操作都能被强制中断。

管理器的 ring buffer 用于异步读取和 CPU 中间数据；
Vulkan 后端另有 scratch / upload 内存。两者不能统称为同一个 staging 池。

### 3.3 请求完成与 GPU 完成分开

AsyncUploadManager::HasCompleted 通过请求命令的版本变化判断完成。
这个 AsyncFence 是请求处理层的标识，不直接等同于 VkFence。
失败读取也会结束该请求，因此“完成”本身也不等于“成功”。

Vulkan 后端通过 PrepareResourceUploadCommandBuffer 收集资源上传命令，
SubmitCurrentCommandBuffers 安排资源上传命令和绘制命令的执行。
scratch 分配的释放还关联使用帧号，不能在上层回调结束后随意复用 GPU 正在读取的内存。

本地源码还有独立的 VKAsyncPipelineCompiler、pipeline key 和编译调度，
pipeline 准备没有绑定到一次模型上传流程。

### 3.4 源码定位

| 相对于 `E:/unitysrc/unity20190433` 的路径 | 重点符号或位置 |
| --- | --- |
| `Runtime/Graphics/AsyncUploadManager.h` | `AsyncUploadHandler`、`AsyncCommandResult`、管理器接口 |
| `Runtime/Graphics/AsyncUploadManager.cpp` | `ScheduleAsyncRead`，约 169 行；`AsyncReadSuccess`，约 317 行 |
| `Runtime/Graphics/AsyncUploadManager.cpp` | `AsyncResourceUpload`，约 398 行；`HasCompleted`，约 500 行 |
| `Runtime/Graphics/AsyncUploadTexture.cpp` | 上传内存获取、`kAsyncCommandRequeueDelayed`、finalise 回调 |
| `Runtime/Graphics/Mesh/MeshAsyncUpload.cpp` | `AsyncVertexDataProcessingCompleteCallback`，约 149 行；`QueueInstruction` |
| `Runtime/GfxDevice/vulkan/GfxDeviceVK.cpp` | `PrepareResourceUploadCommandBuffer`，约 3616 行；`SubmitCurrentCommandBuffers` |
| `Runtime/GfxDevice/vulkan/VkScratchBuffer.cpp` | `Reserve`、`Release`、内存复用 |
| `Runtime/GfxDevice/vulkan/VKScratchBufferAllocation.cpp` | 释放时 `MarkUsed(frameNumber)` |
| `Runtime/GfxDevice/vulkan/VKAsyncPipelineCompiler.h` | pipeline key、编译请求与调度 |

上述源码行号仅用于定位，源码变化后应按符号查找。

## 4. 对本项目的设计方向

### 4.1 四项职责，暂不强制对应四个新类

| 职责 | 负责内容 | 不应承担的内容 |
| --- | --- | --- |
| 场景 / 模型准备 | 收集所需资产，汇总准备结果，决定何时启用场景 | staging、Vulkan fence、command pool |
| GPU 资产准备与缓存 | 将 Texture / Mesh / Material 转成 GPU 表示，管理依赖、缓存复用和更新 | Demo 启动流程、文件导入 |
| Vulkan 上传服务 | 数据传输排队、staging、批次、命令录制、同步和完成回收 | 模型依赖遍历、材质语义、呈现配置 |
| Pipeline 缓存与准备 | shader、布局、顶点输入、渲染状态、目标格式等组合 | 贴图 / mesh 上传队列 |

GPU 资产准备层可以接受类型化资产 handle；更底层的传输工作应面向：

```text
BufferUpload：目标 buffer、目标偏移、源数据范围、源数据所有权
ImageUpload：目标 image、mip / layer / 区域、数据布局、源数据所有权
```

目标对象及 Vulkan 同步细节留在后端，不要求 App 直接提供 VkBuffer、VkImage 或 barrier。
首次实现可以只覆盖当前需要的完整资源上传，但接口和工作记录不能阻止以后按范围分块。

### 4.2 请求与批次不是一回事

- 请求表达调用方的需求，批次表达后端的提交安排。
- 一个请求可以跨多个批次，多个小请求可以合并进入一个批次。
- 同一版本的资源应复用；不同请求等待同一准备结果时，不重复上传。
- 一个请求失败或取消，不能清空其他请求共用的 GPU cache。
- 调度预算属于整个上传服务，不应让每个请求分别消耗一份完整的每帧预算。
- staging 不足时应有明确策略：延后、允许受控增长，或显式阻塞路径。
- 大资源不能永远因为超过预算而无法开始；需要分块或允许受控的超预算处理。

对外可以使用 ticket 查询请求状态，但不能把 ticket 与某一个 VkFence 一一绑定。
App 是否需要逐资源查询、还是仅查询聚合请求，应由调用场景决定。

### 4.3 缓存从整体初始化改为增量存储

- 新增一张贴图不重建现有 cache。
- cache 保存资源和版本状态；上传游标、待执行工作移到调度或请求记录中。
- 材质布局按模板或兼容布局缓存；descriptor 分配支持增量增长。
- 明确已有资源复用、新版本准备和发布的规则。
- handle generation 与内容 revision 的用途不同；同一贴图重导入后 handle 可以不变。
- 上传期间源数据必须有效且稳定。优先明确现有 CPU Asset 的读取 / 修改约束，
  按需增加请求持有或快照，不以本次重构为由重写整个 CPU Asset 体系。
- 若同时接受不同 AssetManager 的请求，应区分来源，避免局部 handle 碰撞。

### 4.4 三种“完成”必须明确

1. **请求处理结束**：上层准备工作结束，需要另外区分成功、失败和取消。
2. **资源可供后续绘制使用**：数据及绑定准备好，必要的 GPU 执行依赖已经建立。
3. **资源或 staging 可以回收**：所有相关 GPU 使用已经结束。

资源可用可以通过 CPU 确认上传完成来实现，也可以在后端建立 GPU 依赖后交给后续绘制。
第一版可以继续采用当前较保守的 fence 轮询方式，但接口不要把这种策略永久写死。

### 4.5 更新在用资源与延迟销毁

首次创建和更新在用资源需要区别处理。以替换纹理为例：

```text
旧版本继续服务已提交的帧
  → 创建并上传新版本
  → 在安全边界发布新资源及绑定
  → 新帧使用新版本
  → 等待使用旧版本的帧完成
  → 释放旧资源与旧绑定
```

上传 fence 不能替代旧资源的渲染完成判断。
descriptor 的更新也必须考虑在途帧，不能只保证 image 存活。
对于原地更新 buffer / image，则必须处理此前读取与此次写入、此次写入与后续读取的依赖。

普通取消应撤销请求的需求，已提交的工作可以完成后再回收；
不能试图撤回已经提交的 GPU 命令，也不能取消其他请求仍然需要的共享工作。
关闭设备时的阻塞清理与正常帧中的取消可以采用不同策略。

### 4.6 不照搬的部分

- 不为了上传重做 Unity 式 IO / job 系统，现有 CPU 加载先保持。
- 不立即引入完整命令图、多线程录制、独立 transfer queue 或复杂优先级系统。
- 不把场景资产依赖遍历塞进最底层上传服务。
- 不要求相机 / 对象等每帧高频数据更新都走资产请求 ticket；
  这类数据可以继续使用每帧 buffer 或环形分配策略。
- 不把“通用”解释为提前支持全部纹理维度、格式、流式 mip 和任意资源热替换。

## 5. 建议的落地顺序

1. **确定接口契约**：输入数据所有权、目标对象生命周期、请求结果、取消、内存不足行为。
2. **建立长期存在的后端上传设施**：接管 CommandPool / UploadContext、全局预算、批次和回收；
   初期仍可主线程录制、使用 graphics queue、保留一个在途批次。
3. **支持增量 GPU cache**：先让独立 texture / mesh 准备成立，拆出 cache 内的上传队列。
4. **接入 Demo 作为普通调用方**：模型准备汇总依赖，pipeline 独立准备，场景层等待启用条件。
5. **接入纹理重导入**：把 Vulkan 替换与同步移出 App，补齐安全发布和旧资源回收。
6. **根据实际瓶颈扩展**：staging 复用与分块、多在途批次、优先级、线程或独立队列。

每一步按实际改动验证，不把后续优化能力作为第一步的前置条件。

## 6. 用于检查设计的场景

- 空渲染器中只准备一张贴图，不存在 Model，也不要求场景 pipeline。
- 已有场景持续绘制时，增量准备一张预览贴图或一个 mesh。
- 两个资源请求引用相同贴图，相同版本只准备一次。
- 一个大请求分批推进，小请求能够按调度策略继续取得进展。
- staging 空间紧张时保持内存受控，等待中的请求不会永久饿死。
- 取消一个请求，其他请求和已可用资源继续正常工作。
- 替换正在使用的纹理，新旧帧各自使用正确版本，descriptor 与旧资源安全回收。
- 上传失败不破坏已激活场景，错误能够归属到对应请求。
- 关闭时未完成请求、源数据、staging、目标资源和设备按正确顺序退出。

## 7. 下一次设计时仍需明确的选择

- GPU cache 是否同时迁入 renderer 所有权，以及它与上传服务的销毁顺序。
- 最小公开接口采用类型化 ensure / update 方法，还是一组类型化请求的集合。
- 第一版请求数据采用共享持有、快照还是受约束借用；大数据如何避免不必要复制。
- ticket 的成功语义采用 GPU 已完成，还是已发布且后端保证后续使用安全。
- 纹理替换的 descriptor 更新采用每帧版本、整体新绑定还是其他受控方式。
- staging 的初始容量、上限、超大资源策略，以及阻塞入口是否需要公开。

优先目标：让“运行中的渲染器安全地接收一张独立贴图上传”成为普通能力。
先使所有权、增量更新、同步和回收正确，再增加并行度。


## 8. 第一版实现进展（2026-09-14）

前文记录的是设计时的状态，本节描述本轮已经实现的部分。

- 新增 `VulkanUploadTypes` 和 `VulkanUploadService`。服务由 renderer 长期持有，
  限定创建它的线程使用；请求与 GPU 批次分离，支持多请求排队、合批、跨批次推进。
- `tryEnqueue` 只在 Accepted 时消费请求；QueueFull / InvalidRequest 保留原数据。
  源数据和目标资源使用共享持有，不能在准备期间修改、reset 或移动。
- `UploadContext::recordBufferUpload` / `recordImageUpload` 只记录已有目标的传输，
  服务统一处理未提交批次失败。原同步包装接口继续保留。
- `GpuTexture::allocate` / `Mesh::allocate` 创建目标，`makeUploadRequest` 组装传输。
  对象存在不代表已上传；准备结果只在完成后发布到 cache。
- renderer 提供独立 `prepareTexture`、状态、取消与 ticket 释放接口；
  空编辑器和已激活场景都可以增量准备贴图，无需模型或 pipeline。
- Demo 的纹理和 mesh 已改走共享服务。场景状态与 pipeline 准备继续由场景会话协调。
- 服务票据 Completed 表示 GPU 传输完成；renderer 贴图接口 Completed 还保证已发布至 cache。
  Cancelled 是请求终态，不表示在途 GPU 工作已经停止或临时资源已经释放。
- 普通取消立即撤销后续需求，已提交批次持有数据直到 fence 完成。
  `releaseTicket` 只删除终态记录；`drain` 显式等待完成；`shutdown` 取消排队并安全清理提交。
- 默认软预算为每 tick 4 ms / 16 MiB / 16 operations；硬上限为 256 MiB staging、
  512 MiB 活跃请求逻辑源数据量、1024 个未释放票据。共享 owner 可能保有更大 CPU 对象，
  逻辑源数据上限不等同于整个进程的物理内存上限。
- 第一版每个目标在一次请求中只接受一个 operation；拒绝同时在途的同一目标。
  image 支持新建单采样 2D color image 的完整紧凑 mip/layer 数据，检查格式、范围、
  offset 对齐和源数据长度；不支持任意旧布局或在用资源原地更新。

当前保留的限制：一个在途上传批次、graphics queue、主线程录制、无 staging 池、
无单 operation 分块；GPU cache 仍由 App 持有，Demo 适配器仍逐资源准备，
材质仍为单模板，pipeline 同步创建，纹理重导入尚未迁移。
这些是后续迭代内容，不应将本轮视为完整资源流式系统。

验证新增于 `tests/VulkanUploadTests.cpp` 和现有启动/渲染 smoke tests：
设备本地 buffer 数据与 offset 回读、跨批次请求、合批取消保活、队列容量与重试、
排队取消、停止清理、线程约束、非法 image 源数据拒绝、空 GUI 贴图预览、
已有场景绘制时增量贴图上传。真实驱动 OOM / device-lost 失败未进行故障注入。


## 9. 上传接口清理与链路复核（2026-09-15）

第 8 节是第一版实现时的状态。本轮完成以下收敛：

- 删除 UploadContext 的 ImageDestination / ImageUploadInfo / BufferUploadInfo，
  直接消费 VulkanUploadTypes 的 BufferUpload / ImageUpload；校验集中在 Context，
  Service 入队与录制共用，去掉双向字段转换及重复的 image 区域拷贝。
- 删除 UploadContext::uploadBuffer / uploadImage、GpuTexture 同步构造/create、
  Mesh 同步构造/create、RenderAssetCache::create / uploadNext / stageTextureReplacement。
- GpuTexture 直接生成统一请求。image 仅初始化新建 2D color 目标，校验完整 mip/layer、
  格式与字节长度、对齐、最终访问及 shader-read 布局所需的 usage。
- 重导入通过 Renderer::uploadTextureAndWait 使用长期上传服务，返回已完成但尚未发布的
  新纹理。方法显式阻塞并可能排空其他请求，负责释放自身 ticket；调用方仍须保证旧
  descriptor 不在使用中，再进行 GPU/CPU/磁盘替换。当前保持原有同步替换事务。
- App 不再创建上传命令池和 UploadContext；仍有 Vulkan cache/descriptor 提交依赖，
  本轮不等同于完成整个 App 的 RHI 抽象。

生命周期复核：

1. 入队失败不消费请求；接受后 service 保留源数据与目标。
2. 录制前 Part 先持有操作；录制失败回滚整个未提交批次。
3. Context 保留 staging/command buffer 到 fence 完成，Service 再结算并释放 Part。
4. 完成后才把纹理/mesh 移入 cache。单独 ticket 保留到调用方接收终态。
5. 取消部分请求不影响同批其他请求；未提交操作释放，在途操作继续保活。
6. 补上“传输完成但尚未发布”的取消状态，避免同步 drain 后取消导致后续 pump
   解引用空纹理或误发布；失败请求也释放准备层的目标引用。
7. 设备/缓存必须覆盖服务和在途工作生命周期，源与目标在保留期间不可被外部修改。

验证扩展：原有 buffer 回读、批次取消、容量、启动、运行与编辑器测试继续使用；
新增部分完成后取消、image mip/layer/重复区域/同步与 usage 校验、完成未发布时取消且
另一纹理正常发布。BC7 多 mip 与 GPU 替换测试已迁到新接口。
仍不覆盖真实驱动 OOM/device-lost 故障注入；仍为单线程录制、一个在途批次，
没有 staging 池、operation 分块或异步 descriptor 热替换。
