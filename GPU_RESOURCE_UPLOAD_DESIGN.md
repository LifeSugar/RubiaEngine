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


## 10. 上传描述、完整纹理策略与执行器分离（2026-09-17）

本节替代第 8、9 节中“只支持新建完整 image”和“校验集中在 Context”的限制描述。

### 当前边界

- `GpuTexture`：只持有 Image / ImageView / Sampler，根据 Vulkan 创建描述分配资源，
  不再引用 TextureAsset，也不生成上传请求。
- `TextureUploadBuilder`：资产适配器。`makeTextureCreateInfo` 转换格式与采样配置；
  `makeTextureUploadRequest` 为新建、尚未发布的 2D 纹理构建完整 mip 上传。
  它明确选择 UNDEFINED → transfer write → fragment shader sampling，并负责完整纹理策略。
- `ImageUpload`：描述目标、源数据、copy regions、barrier range，以及显式 `before/after`。
  `ImageAccessState` 包含 layout / stages / access。默认 stages 为零，遗漏状态会被拒绝。
- `UploadValidation`：Service 入队和 Context 录制共用的结构校验。只检查可从描述得出的
  范围、usage、源数据跨度、格式块对齐、常见 stage/access 配对及已支持能力。
  不根据 Image 的创建布局推断运行时布局，不要求覆盖整个 mip 或整个 barrier range。
- `VulkanUploadService`：持有源/目标、检查设备与 owner、调度请求、批次和 ticket；
  `UnsupportedRequest` 区分尚未支持的能力，`InvalidRequest` 表示描述或服务使用约束不满足。
  所有拒绝均保留请求；同一目标仍不允许同时有多个待完成操作。
- `UploadContext`：按描述创建 staging、复制 CPU 字节、录制两道 barrier 与 copy，
  提交并管理 fence。第一道为 before → TRANSFER_DST，第二道为 TRANSFER_DST → after。
  两道分别处理复制前后的访问依赖；不固定第一道 oldLayout 为 UNDEFINED。

### 支持的 image 复制

单采样 2D color image，包含 mip 和数组层；允许局部矩形、同一 mip 多个不重叠区域、
非零 bufferOffset、bufferRowLength / bufferImageHeight、多层复制。
支持的格式块元数据直接使用 VkFormat，包含常用 R/RG/RGBA、BGRA 与 BC1–BC7，
不再经过 CPU TextureFormat 反向映射；其他格式、3D、depth/stencil 等明确返回 UnsupportedRequest。
边界布局支持 UNDEFINED（仅 before）、GENERAL、TRANSFER_SRC/DST、SHADER_READ_ONLY、COLOR_ATTACHMENT。

源数据长度按实际最后一个被读取的块计算，包含行/层之间 padding，不要求末行之后的 padding。
BC 区域的起点按块对齐，接触 mip 边缘时允许 extent 小于完整块；普通 R8 在 graphics queue
上按单字节对齐，不额外强制 4 字节对齐。重叠目标区域、越界或溢出均拒绝。

### 调用方必须提供的同步契约

`before` 必须描述 barrier range 内所有子资源在这次上传前的真实状态。
范围中状态不一致时，由调用方先统一状态或分次安排上传；当前 service 不做状态跟踪。
复制区域可以小于 barrier range，但若 before=UNDEFINED，则整个 range 的旧内容都可以被丢弃。
要保留未覆盖像素，必须提供实际旧布局及先前访问范围。

本轮只使用 graphics queue。调用方负责之前访问与上传的排序，并在 GPU 完成前阻止后续
渲染或其他写入访问该目标；service 不能发现渲染器之外的资源访问，也不转移 queue ownership。
这提供了已有 image 的区域传输能力，尚未提供自动协调在用纹理的更新调度器。

取消未提交操作不改 GPU 状态；取消已经提交的操作不会撤回 copy，也不会恢复旧布局。
Cancelled 或 releaseTicket 不代表 GPU 已停，原地更新的调用方必须等已提交工作完成（如 drain），
再根据操作是否已提交处理 after 状态。失败时不能盲目假设 before/after，需进入资源恢复流程。
BufferUpload 本轮保持原有边界，没有增加 buffer 原地更新的 before 依赖描述。

### 验证

`VulkanUploadTests` 增加真实 GPU image 回读：完整多 mip/数组初始化、已有 image 局部更新、
有 padding 的多层源布局、同 mip 两个区域、未修改像素与 mip 保留、子范围更新、R8 字节偏移、
BC7 非整块边缘更新。非法范围、源数据截断/溢出、块对齐、同步描述和未支持能力分别验证。
现有取消、批次、容量、完整 BC7 资产上传及启动/运行/编辑器测试继续覆盖原链路。
仍未做真实驱动 OOM/device-lost 故障注入，也没有多队列或自动资源状态跟踪。

参考 Vulkan 规范：[VkBufferImageCopy](https://docs.vulkan.org/refpages/latest/refpages/source/VkBufferImageCopy.html)、
[vkCmdCopyBufferToImage](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdCopyBufferToImage.html)。

本轮验证结果：`cmake --build --preset debug-vs --parallel 1` 构建通过；
`ctest --test-dir build/debug-vs -C Debug --output-on-failure` 全部 5 项通过。
GPU 测试日志未发现 Vulkan VUID/validation error；存在环境中 RenderDoc/OBS 重复注册层的 loader 警告。


## 11. 通用资产内容版本基础设施（2026-09-20）

为后续缓存复用和准备中去重补齐 CPU 侧版本，不将版本作为稳定 handle 的一部分。
`AssetHandle<T>` 仍由 index/generation 构成；`AssetVersion<T>` 保存 handle/contentRevision。
所有 AssetRegistry 的槽位统一维护 uint64_t 内容版本：创建为 1，成功替换递增，拒绝不变；
删除/reset 清零，复用槽位通过新 generation 区分旧身份。恢复旧内容也产生新版本，溢出时拒绝替换。

AssetManager 为全部七类资产提供 contentRevision(handle)、version(handle)、isCurrent(version)。
当前仅纹理已有公开 replace 入口，其他资产仍保留原有创建/只读策略；基础设施不绕过类型专属校验。
版本快照只表示元数据，不保活 CPU 字节，不跟踪依赖变化，也不提供多线程同步。
不同 AssetManager 的数值版本可能相同，未来缓存键必须包含管理器域或绑定唯一管理器。

后续 Texture 准备层可以使用绑定域内的 `{handle, contentRevision}` 判断缓存命中或共享任务；
目前 GPU cache 和独立准备入口尚未接入版本匹配，重复请求仍按现有逻辑处理。
详见 `engine/asset/README.md` 的版本规则与使用示例。

本轮验证：Debug 全量构建通过，CTest 7/7 通过（新增 asset-revision-test），
覆盖稳定 handle 的内容替换、失败不变、恢复旧内容、删除/reset/槽位复用、插入失败回滚，
以及全部七类资产的版本查询。既有启动、运行、编辑器 GPU 测试通过，未发现 VUID/validation error。


## 12. 独立纹理准备逻辑迁移（2026-09-20）

新增后端内部组件 `VulkanResourcePreparation`，从 Renderer 移入 PendingTexturePreparation、
pendingTextures_、纹理创建/请求构建、状态/取消/ticket 释放及完成后发布逻辑。
同步 uploadTextureAndWait 的实现一并迁入，仍返回未发布的新纹理，并可能 drain 其他共享请求。

Renderer 持有组件，保留原公开接口、场景与独立准备的互斥规则，以及每帧的统一推进顺序：
ScenePreparation::advance → UploadService::tick → ResourcePreparation::advance。
ResourcePreparation::advance 只查询传输结果并发布，不再次 tick 或等待 GPU。
组件借用 Device/UploadService，缓存必须覆盖请求和已发布资源的使用期。

清理顺序为：取消/释放场景会话，销毁资源准备组件（取消/释放其 ticket），然后 shutdown 上传服务。
组件析构不访问缓存、不发布资源、不关闭共享服务；在途 Part 继续持有资源直到 GPU 完成。
已发布纹理归 cache 所有，不随准备 ticket 或准备组件的销毁而移除。

本轮保持原 ticket 和状态语义：仍复用 UploadTicket/UploadStatus，上传完成但尚未发布时对外仍为
Uploading，取消该阶段不会误发布。已缓存或重复请求仍拒绝，尚未接入版本匹配和任务合并。
场景专用的 RenderAssetCache::prepareNext 路径暂未迁移，也未改变其整体取消策略。

新增生命周期回归：分别在排队中和已提交时销毁准备组件，确认其 ticket 被释放、纹理不发布、
已提交来源保活到 GPU 完成，而且同服务上的其他请求继续成功。
现有启动测试继续覆盖独立贴图发布、GUI 预览、取消、drain 完成后取消及重导入路径。

本轮验证：Debug 全量构建通过，CTest 7/7 通过；新增准备组件销毁回归与原有纹理发布、
取消、重导入测试均通过，GPU 测试日志未发现 VUID/validation error。


## 13. Texture / Mesh 共用资源准备流程（2026-09-20）

本节更新第 12 节的类型专用记录和复用上传 ticket 的设计。

### 类型差异与公共流程

`IResourcePreparation` 是后端内部抽象接口，只包含三个步骤：

1. `createGpuResources(device)`：创建尚未发布的 GPU 对象。
2. `buildUploadRequest()`：构建传输请求，允许包含多个 operation，也允许为空。
3. `publish()`：所有传输完成后把对象发布到 cache；失败必须保持 cache 不变。

`TexturePreparation` 组合 GpuTexture / TextureUploadBuilder，`MeshPreparation` 组合 Mesh。
两者持有未发布对象，通过 RAII 清理；请求构建后源数据保活交给 UploadRequest。
派生类不管理 ticket，不轮询 fence，不 tick，也不处理取消状态。

`VulkanResourcePreparation` 用统一 PreparationRecord 保存目标、类型准备对象、可选 UploadTicket
和 ResourcePreparationStatus。统一完成接收、容量限制、上传入队、状态推进、取消、发布和回收。
目标按 cache + 资源类型 + 槽位识别冲突；同数值 Texture / Mesh handle 不冲突。
初次发布仍拒绝覆盖已存在对象；尚未接入 contentRevision 匹配或重复请求共享。

### ticket 与生命周期

对外返回 `ResourcePreparationTicket`，通过 `resourcePreparationStatus`、
`cancelResourcePreparation`、`releaseResourcePreparation` 统一操作。Renderer 新增 prepareMesh，
prepareTexture 改为返回同一套准备结果；旧 texture 专属状态/取消/释放入口删除。

- Preparing：已接收，等待上传调度或等待零上传任务发布；创建和请求构建目前仍同步执行。
- Uploading：正在传输，或传输已经完成但尚未发布。
- Ready：全部传输完成，且成功发布到 cache。
- Failed / Cancelled：不再发布，记录保留错误和传输进度供查询。

只有终态允许释放准备 ticket。终态立即回收类型对象和内部上传 ticket，保留轻量状态直到
调用方 release。默认最多保留 1024 个准备记录（包含未释放终态），零上传任务也受限。
字节计数仅表示传输进度，不代表 CPU 创建或 pipeline 编译进度。

空 UploadRequest 不进入 UploadService，因此不占上传 ticket 容量；由下一次 advance 发布。
单个 Mesh 的 vertex/index 操作可以跨批次，必须全部完成才发布。
取消已提交任务不会撤销 GPU copy；UploadService 的在途 Part 继续保活源/目标直到 fence 完成。
取消准备不修改已发布资源，不影响共享服务上其他请求。准备组件全部入口限所属渲染线程，
缓存必须覆盖请求和已发布资源使用期，服务与设备必须比准备组件活得更久。

### 本轮边界与验证

Renderer 仍统一推进 ScenePreparation → UploadService → ResourcePreparation。
准备组件 advance 不 tick、不 drain；同步 uploadTextureAndWait 仍是显式阻塞的重导入适配器。
场景专用 RenderAssetCache::prepareNext 尚未迁入公共准备记录，只复用新的 publishMesh 入口。
尚未实现 Material / ShaderProgram 准备器、依赖调度、缓存版本命中、请求合并或多线程执行。

回归覆盖 Texture/Mesh 同槽位共存、重复拒绝、Mesh 部分完成不可见、完整完成后发布、
取消在途 Mesh 的来源保活、零上传任务不占传输容量、准备记录容量与释放、发布失败隔离、
创建失败清理、错误线程调用拒绝，以及 Renderer/App 主循环的独立 Mesh 发布入口。

本轮验证：Debug 全量构建通过，CTest 7/7 通过；GPU 测试日志未发现 VUID/validation error。


## 14. 版本缓存复用、共享准备与资源替换（2026-09-20）

本节替代第 13 节的重复拒绝和仅初次发布策略。目前 Texture / Mesh 共用以下流程：

- 同 cache、domain、类型、handle（含 generation）、revision 已驻留：返回 CacheHit，
  状态立即 Ready，不创建 GPU 对象、不申请上传 ticket，传输计数为零。
- 同一准备组件内，同版本任务正在准备（包括传输完成尚未发布）：返回 Shared。
  每个调用方拥有自己的 PreparationTicket，共享一个内部准备记录与 UploadTicket。
- 新版本：返回 Started，创建新 GPU 对象并上传，成功后替换旧缓存。缓存对象不原地写入。
  拒绝/失败/取消保留旧驻留版本；只有新任务接受成功后才取代旧版本的未完成任务。

每个 cache 绑定一个资产 domain，避免不同 AssetManager 的数值 handle/revision 相撞。
AssetManager 的 domain token 在移动时跟随资产移交，快照和 cache 保活 token 防止地址复用。
不同 generation 不能覆盖同一缓存槽位，需结束请求并 reset cache 后重建。

入口为 `prepareTexture(cache, assets.snapshot(handle))` / `prepareMesh(cache, assets.snapshot(handle))`。
AssetSnapshot 保存 domain、版本以及不可变数据，独立于 registry 的扩容、内容替换和 reset。
当前 snapshot 捕获复制一次 CPU 内容；调用方可以保存并反复提交同一份快照。
公共 prepare 仍同步创建 GPU 对象和构建请求，返回 ticket 后逐帧上传/发布。

### 共享与取消

对外 records_ 存放调用方 Subscription，内部 tasks_ 只存未终结共享任务。
Subscription 持有共享准备记录以及可选的本调用方取消状态。
取消一个 ticket 只减少等待者数量；最后一个等待者取消时，才取消底层上传并释放未发布对象。
已提交 Part 仍保活资源直到 fence 完成。release 只删除对应终态订阅，不删除缓存资源。
默认 1024 个调用方 ticket 上限也覆盖 Shared/CacheHit，防止未释放订阅无限增长。

cache 记录 resident revision 与最高已接受请求版本。同槽位更新请求接受后，旧未完成任务
标记 Cancelled（error 说明被新版本取代），不会再发布；旧在途 GPU 工作仍正常退休。
新版本入队被拒绝时不会改变版本记录或取消已有任务。低于最新接受版本的未驻留旧快照拒绝；
仍驻留的旧版本可缓存命中，且不撤销正在进行的新版本任务。同版本失败/取消后可以重试，
无需等其他调用方释放其失败/取消 ticket。Ready 记录是历史完成结果，不是资源驻留租约。

### 替换提交与外部使用

新对象上传完成后，Texture / Mesh 替换当前先 device.waitIdle，再提交；整个 advance 必须位于
本帧命令录制之前。原有帧可能仍引用旧 Buffer / ImageView / descriptor，这次保守等待保证其
完成后才替换并销毁旧资源。后续可优化成按帧延迟回收，目前不声称零阻塞更新。

cache 保存已发布材质的纹理 handle 和布局，纹理替换更新所有受影响材质 descriptor；全部
引用及临时容器先准备，再进入 descriptor 写入与资源移动。GUI preview 使用独立发布序号
检测缓存替换，下一次 preview 调用等待旧 GUI 使用结束并重新注册 descriptor。
调用方应每帧获取 preview token，不跨资源推进保留已经录制好的命令或 GUI draw data。

场景路径仍由原 ScenePreparation/prepareNext 协调，但发布时记录 domain/revision，可与独立
准备共享缓存。旧同步重导入事务在 CPU/GPU 成功后标记新版本；rollback 恢复原版本标记。
准备层不自动修改/回滚 CPU AssetManager 或磁盘，也不负责依赖资产更新；特别是改变 Mesh
子网格/材质结构时，调用方必须协调 CPU 场景启用与 GPU Ready 的时机。

验证包括独立 CPU 快照、管理器移动后的域身份、相同数值 handle 的跨域拒绝、单传输容量下
重复任务共享、独立取消/全部取消、完成未发布时共享、缓存命中、Texture/Mesh 版本替换、
失败和队列满保留旧资源、在途旧版本被取代，以及真实场景材质绘制和 GUI preview 自动刷新。

本轮验证：Debug 全量构建通过，CTest 7/7 通过，git diff --check 通过；
GPU 测试日志未发现 VUID / validation error。


## 15. 全部 Asset 类型的 Preparation 与依赖准备（2026-09-20）

本节更新第 13、14 节的类型范围和依赖边界。七种 Asset 都有独立准备入口，
都返回同一套 PreparationTicket / Status / Disposition，并共享取消、版本匹配与发布流程。

| Asset | 准备结果 | 自身是否进入 UploadService |
| --- | --- | --- |
| Texture | Image、ImageView、Sampler | 是，像素传输 |
| Mesh | 顶点/索引 buffer、子网格信息 | 是，几何数据传输 |
| Shader | GpuShader / VkShaderModule | 否 |
| ShaderProgram | GpuShaderProgram、阶段模块引用、反射 descriptor layouts / pipeline layout | 否 |
| MaterialTemplate | GpuMaterialTemplate、材质 descriptor layout、Program 引用 | 否 |
| Material | GpuMaterial、参数 buffer、descriptor set 与所属 pool | 否，当前参数直接写 host-visible coherent buffer |
| Model | 模型层级快照与依赖已就绪的发布记录 | 否，没有额外的 Model GPU allocation |

“不需要 Upload”仅描述该类型自身。Material 依赖 Texture；Model 依赖 Mesh，
Mesh 依赖子网格 Material；Material → MaterialTemplate → ShaderProgram → Shader。
因此首次 prepareModel 仍会间接触发纹理和几何数据上传。

### 快照、依赖与版本

`assets.snapshot(handle)` 支持全部七类，并捕获同一 domain 下的不可变依赖快照。
一次捕获内相同资源只复制一次，多个父节点共享该快照；不同次捕获目前仍会复制 CPU 数据。
这一步在 CPU Asset 层完成，不持有 Vulkan 对象。调用方可以复用快照，避免反复复制大资源。

公共准备目标记录根资源版本和排序后的传递依赖版本集合；缓存命中及任务共享都比较这两部分。
例如 Texture revision 增长而 Material/Model 自身 revision 不变，新快照仍会启动依赖更新，
不会把旧材质准备结果当成 CacheHit。未变化的 Shader/Program/Template 仍命中缓存。
Mesh 自身 revision 未变时只更新依赖就绪记录，复用原顶点/索引 buffer，不重复传输几何数据。
发布前再次核对依赖驻留版本，失败时不覆盖该任务的旧驻留对象。

有依赖的准备记录先持有快照，由 advance 逐个接收直接依赖；依赖自己也走公共准备入口。
每个父任务持有独立的内部订阅 ticket，因此两个父任务可以共享同一次依赖准备。
依赖 Ready 后父任务才创建自身资源、构造可选 UploadRequest、最后发布。
无依赖资源仍在请求接收时创建对象；空 UploadRequest 不占上传容量。

外部 ticket 容量与内部依赖订阅容量分开计算，只有一个外部槽位也能完成整个模型。
目前直接依赖逐个推进，内部容量按七类资产图的最大深度预留；不是并行 job system。
父任务 Waiting 的表现是 Preparing。字节计数只统计自身传输，不聚合共享依赖字节。

取消一个父订阅不影响其他父订阅；最后一个订阅取消才释放该任务的内部依赖订阅。
依赖仍有其他等待者时继续执行。已发布依赖保留在缓存，失败/取消不是整个依赖图的回滚。
依赖失败或被新版本取消，父任务失败并报告原因；调用方可使用最新快照重试。
Ready ticket 依旧是历史完成结果；资源更新后应发起新准备，不能用旧 ticket 判断当前驻留版本。

### ShaderProgram 与 pipeline 的界线

ShaderProgram Ready 表示 shader modules 与反射布局已创建，不表示完整 graphics pipeline 已创建。
`GraphicsPipeline::CreateInfo::program` 可以直接消费缓存中的 GpuShaderProgram，复用模块；
render pass、顶点输入、材质渲染状态、descriptor layouts 与 push constants 由 pipeline 调用方提供。
旧 SPIR-V 字段保留为现有场景路径的兼容入口。

现有 VS+PS、材质单 UBO 和独立 image/sampler 的能力限制保持不变。
本轮没有新增 compute program、全资产 CPU 热重载、自动 pipeline 重建或材质参数迁移。
Shader/Program/Template/Material/Model 的 CPU 内容仍按现有只读资产规则发布。

Renderer 暴露所有七类 prepare 方法并统一推进；Demo 的 ScenePreparation/prepareNext
仍保留原有场景启用和 pipeline 协调，尚未迁移为 prepareModel 的组合调用。
通用准备既不创建 Scene 实例，也不自动启用场景。GPU 替换仍在帧边界采用 waitIdle 保守提交。


入口示例（调用顺序仍是先推进资源，再录制本帧命令）：

```cpp
auto result = renderer.prepareModel(cache, assets.snapshot(modelHandle));
if (result.accepted())
{
    // 保存 ticket；后续每帧推进，直到 Ready / Failed / Cancelled。
    renderer.advanceResourcePreparation();
    auto status = renderer.resourcePreparationStatus(result.ticket);
    // Ready 后可以使用 cache.model / mesh / material 等结果。
    // 终态读取结果后 releaseResourcePreparation(ticket)。
}
// QueueFull 可保留快照重试；其他拒绝读取 result.error。
```

回归覆盖七类缓存命中、模型共享与独立取消、单外部槽位依赖推进、在途依赖取消与重试、
依赖失败向父任务传播、根 revision 不变时的贴图版本更新、旧快照拒绝、未变化几何 buffer 复用，
以及 UploadService 满载时 Program 的零传输准备和使用已准备模块创建真实 graphics pipeline。

验证：Debug 构建通过，CTest 7/7 通过，git diff --check 通过；GPU 测试日志无 VUID / validation error。


## 16. 删除 Demo 独立 Scene Upload 路径（2026-09-20）

本节替代第 15 节末尾“场景尚未迁移”的说明。默认 Demo 与零散资产准备现在走同一条路径：

```text
CPU PreparedContent
  → 场景协调：prepareModel + prepareMaterialTemplate + prepareShaderProgram(present)
  → VulkanResourcePreparation：依赖共享、版本缓存、资源创建、可选上传、发布
  → 所有根请求 Ready
  → 使用缓存中的 GpuShaderProgram / GpuMaterialTemplate 创建 graphics pipeline
  → Ready → Activate → App 移交 CPU 内容
```

删除 RenderAssetCache::initialize / beginUpload / prepareNext / pendingUploadCount /
hasSubmittedUpload / cancelPendingUpload；删除 pendingTextures、pendingMaterials、pendingMeshes、
pendingUpload、上传计数、全局材质布局/descriptor pool 和 uploadMaterialSets。
RenderAssetCache 不再引用 VulkanUploadService，只保存资源、版本并负责发布/替换。
材质使用各自已准备的 Template 布局和 descriptor pool。

VulkanScenePreparation 保留为场景就绪与启用的协调对象，不再接触 UploadService、UploadTicket、
命令池、批次或 fence。它订阅通用 PreparationTicket，QueueFull 保留根快照待下帧重试；
依赖失败则报告场景失败。创建 pipeline 使用准备好的模块，不再在默认工厂中重新读取 SPIR-V。

唯一每帧推进入口为 advanceResourcePreparation：UploadService tick → 通用准备推进/发布 →
场景根请求检查/pipeline 创建。删除 advanceScenePreparation 兼容入口，避免重复推进。
ScenePreparationState::Uploading 改为 PreparingResources，移除 submitted；total/completed 统计
根准备请求，默认是 Model、材质 Template、Present Program 三项，不声称为传输数量或字节数。

场景会话仍要求最初为空的 renderer/cache，并独占尚未启用的场景缓存；它负责取消自己的
根订阅，通用准备负责退订依赖和取消最后一个订阅者的上传。清理场景前由 renderer 等待 GPU，
在途 copy 的来源/目标继续由共享上传服务保活至正常回收。已激活场景不受会话 cancel 影响。
这是场景启用的所有权边界；没有重新建立一套 Scene Upload 队列。

回归在真正的默认加载与渲染路径验证：激活后 prepareModel 必须直接 CacheHit，PresentProgram
必须已在公共缓存中；继续覆盖场景中途/Ready 取消、重试、resize、CPU 移交、Runtime/Editor
绘制以及纹理更新。通用准备测试继续覆盖在途依赖取消与来源保活。

本轮验证：Debug 构建通过，CTest 7/7 通过（含默认场景缓存命中断言）；git diff --check 通过，GPU 日志无 VUID / validation error。
