# 编辑器启动与内容加载

启动以 GUI 可以独立运行作为边界。`App::initVulkan()` 只创建
`VulkanContext` 和基础呈现资源，不读取模型或 shader，不上传场景资源。
相机设置也独立于 Vulkan 初始化。

## 执行流程

1. 创建窗口、设备、交换链、帧命令与同步资源。
2. 初始化 ImGui 和 GUI bridge，进入主循环。
3. 正常启动保持空场景；提交首帧或后续帧均不会自动加载 DemoContent。

以下流程仅在显式调用内容加载入口后执行（当前由测试调用）：

4. 工作线程在独立的 `PreparedContent` 中读取 shader、导入模型和解码贴图。
5. 主线程取得 CPU 结果，调用渲染前端 `makeSceneResourceRequest()` 拆分模型并去重 Mesh，
   再向 renderer 提交只包含具体资源快照的请求，每帧推进准备会话。
6. Vulkan 后端完成上传和 pipeline 准备后返回 Ready，App 在帧边界启用场景并移交完整的
   AssetManager、Scene、TextureImportRegistry 和 DemoContent。

GUI 只访问主线程的资源。在完成移交之前，资源面板和场景保持为空。
加载阶段、准备进度、完成信息和错误统一写入现有 Console，不创建加载面板。
准备进度按约 10% 的里程碑记录，避免每帧刷屏。加载失败不会退出编辑器。
已移除 RunConfig 的自动加载开关和首帧后触发状态。主循环只推进已发起的准备请求，
不会主动发起 Demo 加载；GUI 也不提供加载或重试入口。显式加载逻辑保留供测试及后续入口使用。

## 职责与接口

- `VulkanRenderer::createPresentation()`：创建无需场景 shader 的基础呈现。
  `operator bool()` 表示基础呈现可用，`sceneReady()` 单独表示场景渲染可用。
- `renderGui()`：只清理交换链颜色并绘制 GUI；不会执行场景 pass，也不会读取
  RenderAssetCache。没有场景时，GUI bridge 不注册视口纹理。
- `beginScenePreparation()` / `scenePreparationStatus()`：
  接收 CPU 资源请求并返回阶段、根请求总数、已完成数和错误；不再暴露场景上传提交数。
  请求与状态定义在 `engine/render/SceneResourcePreparation.hpp`，不包含 Vulkan 类型。
- `activatePreparedScene()`：在 Ready 后启用场景，再由 App 移交 CPU 内容。
  Ready 阶段仍允许 GUI 绘制和 resize，`sceneReady()` 直到启用后才为 true。
- `cancelScenePreparation()`：取消尚未启用的准备，清理上传和部分场景资源，
  保留基础呈现。取消已启用的准备不会销毁正在使用的场景。
- `VulkanUploadService`：由 renderer 随基础呈现创建并长期持有，管理上传命令池、
  UploadContext、请求队列、全局预算、批次提交、fence 查询和传输资源回收。
- `VulkanResourcePreparation`：统一管理六类具体渲染资源 的准备记录、依赖、状态、取消和完成发布。
  `IResourcePreparation` 在主线程固定依赖快照，在独立资源工作线程创建 GPU 对象、构建上传请求，
  最后回到主线程发布到 cache；工作线程不读取实时缓存。
  准备 ticket 独立于上传 ticket；空上传请求跳过 UploadService，仍通过 advance 完成发布。
  Renderer 保留外部接口并转发；组件借用 Device/UploadService，不推进共享服务的 tick。
  Texture reimport 通过同一个前端 prepare/ticket 路径提交，发布事务只在主线程最终提交时运行。
  析构取消并释放自己的 ticket，停止派发并 join 自己的创建线程；已提交资源继续由上传服务保活，
  不清空或关闭共享服务。Device 必须在线程退出及 GPU 资源释放之后才能销毁。
- `VulkanScenePreparation`：仅组合通用 Mesh / MaterialTemplate / ShaderProgram 准备 ticket，
  等待 Ready 后创建 pipeline，协调场景启用与取消；不持有 UploadService 或 UploadTicket。
  状态为 PreparingResources → PreparingPipelines → Ready → Activated；失败/取消保持 GUI 可用。
  进度按根请求计数，一个 Mesh Ready 表示其所有依赖已就绪，不表示单个 GPU copy。
- `advanceResourcePreparation()`：每帧推进共享上传与结果发布；App 即使处于 Idle / Ready
  也调用它。顺序为 UploadService tick → 通用 Preparation 发布 → 场景就绪检查和 pipeline 创建。
  原 `advanceScenePreparation()` 兼容入口已删除。
- `prepareTexture()` / `prepareMesh()`：独立资源准备，不需要模型或场景 pipeline。
  输入改为 `AssetManager::snapshot(handle)`：同版本已驻留返回 CacheHit，准备中返回 Shared，
  新任务返回 Started；三者均有独立的调用方 ticket。共享任务只有最后一个等待者取消才取消上传。
  `resourcePreparationStatus()` / `cancelResourcePreparation()` / `releaseResourcePreparation()`
  共用准备 ticket；Ready 表示全部上传完成且已经发布到 cache。取消未完成请求不破坏已有资源。
- `DemoContentLoader::loadBuiltins()` / `loadModel()`：将默认纹理、材质模板和
  shader 的创建与具体模型导入分开；`load()` 保留同步组合入口供 CPU 测试使用。
- `RenderAssetCache`：只保存已发布资源和版本，不调度上传。
  原 initialize / beginUpload / prepareNext、pending 列表和全局材质 descriptor pool 已删除。
  材质布局由 GpuMaterialTemplate 持有，每个 GpuMaterial 的 descriptor pool 由通用准备发布。
  Mesh、材质及共享准备资源替换后将旧所有权交给 Renderer；Renderer 组合 FrameContext
  与退休容器，在成功提交后绑定帧槽，在对应 fence 完成后回收。通用纹理更新创建新 descriptor
  并共享不可变参数 buffer，旧纹理、旧绑定和旧预览 registration 延迟释放；文件重导入的创建与上传也经过异步 Preparation。
  详见 [Renderer 资源退休边界](../../../renderer/vulkan/RESOURCE_RETIREMENT.md)。
- `AppContentLoading.cpp`：管理 Preparing、Uploading、Finalizing、Ready、Failed
  应用状态，负责 CPU 工作线程、提交准备请求、展示状态、结果移交和请求取消。
  不创建命令池、不操作上传批次、不查询 fence、不构造 Vulkan pipeline。

当前一个 renderer 同时管理一个场景准备会话，只接受空场景和空 RenderAssetCache；
失败或取消后可以重新开始，已有场景不会被新请求覆盖。cache 仍由应用持有，
必须比 renderer 活得更久；本次边界调整尚未包含 cache 所有权迁移、场景热切换、
App 整体的 RHI 抽象。纹理重导入已接入通用异步准备，最终文件安装和 CPU 版本交换在主线程发布边界完成。独立贴图可以在空编辑器或已激活场景中准备，
但不能与尚未激活的 Demo 场景准备同时操作缓存。

## 上传与生命周期

`UploadContext` 只提供显式批次和 `recordBufferUpload(BufferUpload)` /
`recordImageUpload(ImageUpload)`，与服务共用描述及校验；旧 Info 类型与同步包装已删除。
完整纹理的资源描述与上传策略由 `TextureUploadBuilder` 从 CPU asset 构建，
`GpuTexture` 只管理 Image / ImageView / Sampler。
`ImageUpload` 显式提供 before/after 状态；共享 `UploadValidation` 支持 2D color image
区域、mip、数组层和带 padding 的复制，不要求完整覆盖。已有 image 的布局与渲染访问协调
由调用方负责；当前没有自动状态跟踪或跨队列同步。Cancelled 也不代表已提交 copy 被撤销。
由 `submitBatch()` 提交、`pollBatch()` 查询 fence，staging 与命令缓冲保留到批次完成。
服务在整个执行期间持有目标引用；源数据在录制阶段复制到 staging。

纹理重导入先在 CPU 后台线程生成临时 KTX2 与候选 TextureAsset，再预留唯一内容 revision，
通过前端 ResourcePreparation 提交候选快照。准备期间不修改当前 CPU 资产和正式文件。
GPU 创建、描述符准备、上传与退休复用通用路径；App 不再访问 Device/GpuTexture 或手动改缓存。
发布事务先检查 CPU 基准版本、安装文件，再发布 GPU 对象，随后以 noexcept 交换 CPU 内容并
结束文件事务。发布失败回滚文件，取消删除临时文件，原 CPU/GPU 版本保持可用。
旧的阻塞上传、同步缓存替换、手动 GPU 版本戳入口已删除。

独立版本更新先上传新 GPU 对象，旧缓存继续可用。上传成功后在帧边界发布替换，
旧 GPU 对象和绑定按提交帧的 fence 延迟回收；普通增量发布不等待整个 GPU 空闲。
材质引用由 cache 更新，GUI preview 按缓存发布序号刷新 descriptor。
advanceResourcePreparation 必须位于录制/绘制本帧 GUI 和场景之前。
缓存绑定一个资产 domain；场景发布也记录版本，AssetManager 移交 App 后仍可缓存命中。
缓存 reset 前必须结束相关准备任务并同步渲染使用；准备 ticket 的 Ready 是历史完成结果，
不能用旧 ticket 判断该版本现在是否仍驻留。

共享服务的请求持有不可变 CPU 来源和目标 GPU 对象的引用，入队只接管数据，
实际 staging 拷贝在 tick 中执行。独立贴图和 mesh 上传完成前不对 cache 查询可见。
服务的 submittedBytes / completedBytes 按提交与 fence 完成分别更新；
场景状态按就绪根请求数报告，依赖上传与不经 transfer 的设备对象创建都由通用准备处理。

每批默认按 4 ms、16 MiB 或 16 个传输 operation 的软阈值停止添加，
最多保留一个在途批次。硬限制默认为 256 MiB staging、512 MiB 活跃请求的逻辑源数据量、
1024 个未释放 ticket。单个 operation 超出硬上限会明确拒绝；暂时排队空间不足返回 QueueFull。
请求可以跨批次，多个请求可以合入一批。Demo 没有独立上传适配器或单独的传输预算。

预算在完整 operation 之间检查；单次 staging 拷贝、启动/Present Pipeline 和绘制兜底的同步创建
仍可能使主线程超时。通用资源创建与 PSO 预热已分别进入独立工作线程；此版本不提供传输线程、
staging 页池或大资源分块。
普通上传取消不等待 GPU，在途批次继续持有所需对象；场景整体取消仍保留原有阻塞清理。
独立纹理还单独记录准备取消状态：即便传输已被 drain 完成，尚未发布的纹理仍可取消，
后续 pump 不会访问被释放的纹理，也不会把它重新发布。

关闭窗口时先请求 CPU 取消并 join，再等待尚未完成的上传 fence，然后释放
目标资源、GUI 和 Vulkan。取消检查位于导入阶段之间及贴图解码前；单次文件
解析或图片解码可以先完成。工作线程从不 detach，也不持有 App 引用。
EditorApp 保证 App 的清理先于 EditorLayer 日志捕获器的销毁。

## 使用与验证

`VulkanApp.exe` 和 `VulkanApp.exe --editor` 默认保持空场景，不自动加载 DemoContent。
`--editor --empty` 保留为兼容别名，与 `--editor` 行为一致；渲染测试仍显式加载 Demo。

```powershell
cmake --preset debug-vs -DRUBIA_ENABLE_GPU_TESTS=ON
cmake --build --preset debug-vs --target VulkanApp
ctest --test-dir build/debug-vs -C Debug --output-on-failure
```

GPU 测试默认不注册，避免无图形环境下自动失败。它们也可以直接执行：

- `--startup-test`：设备本地 buffer/image 上传回读、已有 image 局部更新与未覆盖内容保留、
  多 mip/数组层/padding/BC7 边缘复制、多操作请求、批次合并与取消、容量与重试、
  独立贴图上传和 ImGui 预览、Mesh 分批完成与取消、零上传准备和发布失败隔离，
  以及空 GUI、空场景 resize、CPU 取消、缺失模型、重复请求拒绝、
  上传取消与重启、Ready 时 resize 与取消、CPU 来源生命周期和上传中关闭。
- `--render-test` / `--editor-test`：缺失 shader 后的 GUI 和 resize、测试代码发起重试、上传中
  持续绘制 GUI、完整场景绘制；保留原有贴图替换、重导入及 resize 验证。
- `--asset-test`：原有 CPU 资源导入与校验。


## 通用资产准备入口

前端使用 `render::ResourcePreparation`，通过 `App::resourcePreparation()` 或
`ApplicationGuiContext::preparations` 获取；调用 `prepare(assets, handle)` 捕获当前版本，
或 `prepare(snapshot)` 提交已经捕获的不可变快照。接口及 ticket/status/code 定义位于
`engine/render`，不要求调用方传入 VulkanRenderer、RenderAssetCache 或 Device。
VulkanResourcePreparationBridge 在应用装配处绑定 renderer/cache，并转发到六类后端 prepare。
返回的就是后端订阅 ticket，没有第二套准备状态机。Model 在 CPU 渲染前端拆分为去重后的 Mesh，
后端不接收模型层级，不提供 Model ticket，也不缓存 Model。Mesh 继续自动准备其渲染依赖，
所有类型共用 CacheHit / Shared / Started 规则，并将依赖版本纳入比较。

Shader、Program、Template 自身不产生 transfer 请求。Material 的参数 buffer 默认使用
DEVICE_LOCAL 内存及 UNIFORM_BUFFER | TRANSFER_DST 用途，由 MaterialPreparation 构造
BufferUpload，通过共享 UploadService 的 staging 路径传输；完成后才发布材质。
上传 barrier 根据反射的参数绑定选择 shader stages，目标访问为 UNIFORM_READ。
仅替换纹理时共享原参数 buffer，不重复上传参数。
父请求在依赖就绪前保持 Preparing，字节数仅表示自身传输。取消父请求释放依赖订阅，
不会取消仍被其他调用方等待的共享任务，也不清除已发布缓存。

ShaderProgram Ready 只保证模块与布局可用；完整 graphics pipeline 仍需渲染上下文。
`GraphicsPipeline::CreateInfo::program` 可复用已准备模块。
Pipeline 状态现在按 `inputAssembly`、`rasterization`、`depthStencil`、`multisample`、
`colorBlend`、`viewport` 分组，并支持指定 `subpass`、`dynamicStates` 和逐阶段
`specializations`。默认仍是 Subpass 0、一个不混合的 RGBA 输出、1× 采样及动态
Viewport/Scissor。`colorBlend.attachments` 按 Subpass 颜色槽位配置；纯深度 Pass
可以清空该数组并省略 Fragment Shader。特化常量的映射和字节由描述自身持有。
核心可选功能按 `Device::enabledFeatures()` 校验；当前 Device 未开启这些可选功能，
请求线框、独立混合等能力会明确报错，不会隐式启用或退回默认状态。
目前只支持 VS/可选 FS 和 Vulkan 1.0 核心动态状态，不支持细分、几何、Mesh Shader
及扩展动态状态。静态 Viewport/Scissor 可通过移除对应动态声明来使用。
`GraphicsPipelineKey` 按完整内容编码 Shader 字节码/入口、特化常量、资源布局、顶点输入、
绘制状态、RenderPass 兼容性和 Subpass，Hash 仅用于加速查找，命中仍比较完整键。
动态状态的当前值不进入键；等价的特化常量字节排布、Binding 顺序、ShaderModule 句柄
和 DescriptorSetLayout 句柄也不会拆分缓存。Shader 依赖更新后按实际字节码重新判定；
仅 revision 变化但绘制内容完全相同，可以继续复用 PSO。
`RenderPass::reference()` / `DescriptorSetLayout::reference()` 返回不可变且持有句柄的快照，
替代 Pipeline 描述中的裸句柄。原 wrapper reset/recreate 后旧快照仍有效；Device 必须
活得比所有引用更久。RenderPass 第一版保留附件索引/数量、Subpass 结构、依赖和 flags，
忽略 Load/Store 及附件布局字段，属于安全但保守的兼容性分类。暂不支持 RenderPass
`pNext` 扩展及布局中的 Immutable Sampler；这些请求明确拒绝，不按不完整键缓存。
`GraphicsPipelineCache::getOrCreate(info)` 是同步、单线程、单 Device 的统一缓存入口，
`find(info)` 只查询；缓存持有对象并返回共享引用，失败不发布条目。`clear()` 只释放缓存
所有权，在途使用者必须保留引用直到 Fence 完成。Renderer 的 `preparePipeline(info)`
统一供场景、Present 和后续预热使用，兼容的 Swapchain 重建复用原 Pipeline。
Renderer 在等待 GPU 空闲后清理场景缓存。当前缓存不自动淘汰；不包含驱动
`VkPipelineCache` 持久化或后台编译；按 Draw 选择和切换已贯通，见下文。
Demo ScenePreparation 仅组合这些通用准备并协调 pipeline/场景激活；独立 Scene Upload 路径已删除。


`engine/render/SceneResourcePreparation.cpp` 保存模型拆分逻辑，不再要求所有 Mesh 共用一个材质模板。
SceneResourceRequest 持有 Mesh、MaterialTemplate、ShaderProgram 的不可变快照，
不再持有 AssetManager 或 PreparedContent。CPU 场景的保活和最终移交由 App 自己负责；
后端只保活请求中的具体资源数据。纯节点层级不产生 GPU 准备请求。

## 前端增量准备

```cpp
// 主线程完成导入/CPU 修改后显式提交，保存 result.ticket 到业务状态。
auto& preparations = app.resourcePreparation();
auto result = preparations.prepare(assets, textureHandle);
if (result.accepted())
{
    // 后续帧查询；不要在这里阻塞等待或重复提交。
    auto status = preparations.status(result.ticket);
    if (render::resourcePreparationFinished(status.state))
    {
        // Ready 时使用资源；Failed/Cancelled 时处理状态及 error。
        preparations.release(result.ticket);
    }
}
```

`prepare` 不扫描场景或监听 AssetManager。创建资源或改变内容后需要显式请求；
同版本含相同依赖时复用缓存/共享任务，新版本走替换。QueueFull 没有 ticket，也不被
接口自动排队；调用方保存请求并在后续帧重试。取消只取消该订阅，终态 ticket 仍需 release。
Ready 是这次快照的历史完成结果，不能用旧 ticket 推断缓存当前版本。

主循环在 GUI 和渲染命令记录前独立调用 `resourcePreparation_.advance()`，
`updateContentLoading()` 仅处理 Demo CPU 导入和首次场景初始化，不再推进通用资源任务。
即便 ContentLoadState 为 Idle 或 Ready，独立增量请求也能完成。
退出/清理时 cancelAll 释放该前端接口拥有的订阅；缓存资源仍由后端及帧回收机制管理。

新 Model 在 CPU 上调用 collectModelMeshes，再逐个提交 Mesh；全部 Ready 后才把对应
实例加入可绘制场景。Mesh 改变拓扑时，也应协调 CPU 场景可见性，避免新 CPU 绘制范围
引用旧 GPU buffer。资源准备不新增场景事务；Pipeline 由后续 DrawList 编译按实际驻留版本选择。
磁盘/CPU/GPU 事务式纹理重导入通过可选的 ResourcePublicationTransaction 与准备发布衔接。

验证通过前端接口覆盖六类资源缓存复用、共享订阅及取消、无效 handle、退出释放，
以及空场景资源准备、已有场景中的 Mesh/Texture 新版本、材质绑定和 GUI 预览更新。

Material 参数策略通过 App::RunConfig.resourcePreparation 或 VulkanRenderer::CreateInfo.resourcePreparation
配置，在 Renderer 创建时固定，影响该会话的所有 MaterialPreparation（含场景依赖准备），
不改变 Texture/Mesh 或 staging 自身的内存策略。

```cpp
using Memory = render::MaterialParameterMemory;
config.resourcePreparation.material.parameterMemory = Memory::DeviceLocal; // 默认，staging
config.resourcePreparation.material.parameterMemory = Memory::HostVisible; // 直接写入
config.resourcePreparation.material.parameterMemory = Memory::DeviceLocal | Memory::HostVisible;
```

选项表达必须满足的属性，允许实际内存有额外属性。包含 HostVisible 就直接写入目标 UBO；
仅 DeviceLocal 即便分配到可映射内存仍使用 staging。组合不支持则准备失败，不自动降级；
0 或未知位在创建阶段拒绝。HostCoherent 不作为必需属性，Buffer::write 在实际分配不 coherent
时执行 flush（VMA 对齐 range，原生路径 flush 整个已映射的独占 allocation）。
直接写入发生于未发布的新参数 buffer，GPU 已使用的旧版本仍按帧回收；Ready/取消契约不变。
仅策略含 HostVisible 时 Material 自身没有 UploadTicket/传输字节，其纹理依赖仍可能上传。

修改配置需要重建 Renderer 会话并正确结束旧缓存和请求；不提供运行时逐材质改策略。
UMA 不会因为共享物理内存而获得可依赖的自动 copy 消除；通过显式组合策略可以避免 staging。


## DrawList 的 Pipeline 选择与绘制

当前链路为 `RenderList + RenderAssetCache + Pass CreateInfo → VulkanDrawListCompiler
→ GraphicsPipelineCache → VulkanDrawList（每个 Draw 持有 Pipeline）→ recordVulkanDrawList`。
Compiler 只消费已驻留资源，不触发 Asset 上传。Renderer 在开始命令录制前完成编译；
`VulkanRenderer::compileDrawList()` 也可提前调用，预热与实际绘制共用同一个缓存入口。
绘制时首次缺失仍同步创建 PSO 兜底；显式预热请求由专用工作线程创建，主线程按数量预算派发和发布。

Pass 描述提供 RenderPass/Subpass、MSAA、光栅化和其他固定状态的默认值，set 0 提供
Frame 布局。每个 Draw 使用该 GPU Material 发布时持有的 GpuMaterialTemplate 快照取得
Program 与 set 1 布局，不直接读取可能已更新的全局 Template 条目。材质语义覆盖
Blend、Cull、Depth Test/Write/Compare；Shader、顶点输入和 Push Constants 不继承
启动 Pipeline 的资源。当前 Scene ABI 为 asset::Vertex、Frame set 0、Material set 1、
DrawPushConstants。VS 只声明实际消费的顶点属性；不兼容的描述符、顶点输入、
Push Constants 范围和颜色输出在创建前拒绝。相同快照/材质变体在一次编译中只解析一次。
参数数值不同但状态/Program/布局相同的材质复用 Pipeline。

Recorder 按 opaque → transparent 顺序执行，保留前端各列表内部的排序。
切换 Pipeline 时重新设置 Viewport/Scissor，使用新 PipelineLayout 绑定 Frame 与 Material
描述符；每个 Draw 通过当前布局写入对象索引，然后执行 indexed/non-indexed draw。
相邻 Draw 共用 Pipeline、Mesh 或材质描述符时跳过相应重复绑定。Alpha Blend 使用
SrcAlpha/OneMinusSrcAlpha，Alpha 通道使用 One/OneMinusSrcAlpha。

VulkanDrawList 持有 Pipeline 的共享引用，但 Mesh/Material 是借用指针，必须在下一次
资源发布前消费，不可跨发布持久缓存。Renderer 的 Pipeline 缓存暂不淘汰，等 GPU 空闲后
才清理；资源替换仍由原有帧 Fence 回收机制管理。SceneResourceRequest.materialTemplate
现只用于启动 Pipeline 与场景初始化，各 Mesh 可以依赖其他模板；Scene Ready 现在保证
请求中所有 Mesh 引用的材质，在当前 Scene Pass 下的变体均已预热。

当前 Scene Recorder 只提供动态 Viewport/Scissor；其他动态状态需扩展命令记录协议。
AlphaClip 使用下述固定 Shader 约定；其他按 Program 的 specialization 配置仍需单独扩展，
不把启动 Pipeline 的 specialization 值套用到所有材质。
这不限制底层 GraphicsPipeline/CreateInfo 单独表达这些 Vulkan 状态。

GPU 回归覆盖不同 Shader 与不兼容材质布局切换、物体索引、透明叠加顺序、索引/非索引
绘制的像素回读，以及缓存复用、驻留模板版本隔离、Cull/Depth 变体和无效输入拒绝。


## AlphaClip

材质通过 `renderState.alphaClipEnabled` 开启裁剪，`alphaClipThreshold` 表示 [0, 1]
范围内的阈值。仍使用 opaque/AlphaClip 队列，不启用 Alpha Blend，深度测试/写入遵循
材质状态。glTF 的 MASK/alphaCutoff 和 doubleSided 已贯通到 MaterialRenderState；
BLEND 对应透明材质，OPAQUE 保持普通不透明状态。

默认 PBR Fragment Shader 以 `贴图 alpha × baseColorFactor.a × 顶点色 alpha` 得到
最终 alpha；小于阈值时 discard，等于阈值时保留。被丢弃的片元不写颜色和深度。

Shader 约定：Fragment specialization constant ID **1000** 为 bool AlphaClip 开关；
DrawPushConstants 在 cameraIndex、objectIndex 后增加 float alphaClipThreshold，
偏移 **8**，CPU 结构大小 **12** 字节。Recorder 每个 Draw 从驻留 GpuMaterial 取阈值。
开关进入 PipelineKey，阈值不进入，所以阈值不同的同类材质共用 Pipeline。
Vulkan bool specialization 用 VkBool32 的 4 字节数据；未开启的材质显式传 false。

SPIRV-Cross 反射 specialization ID/type 和 push constant 成员布局，Compiler 验证此约定。
自定义 Shader 必须声明这一开关并实际执行 discard；不能仅声明开关而省略裁剪逻辑。
缺少能力的 Shader 仍可绘制普通材质，但开启 AlphaClip 会在录制前报错。ID 1000 是
Scene Fragment Shader 的保留语义，不能另作他用。通用 ShaderInterface 只保存反射数据，
不含 Vulkan 类型或具体材质策略。更改 ABI 后需重新运行 `Assets/shaders/compile_shaders.ps1`；
对应默认 Shader SPIR-V 已同步更新。

GPU 回读测试覆盖关闭裁剪、低于/高于/等于阈值、0/1 边界、不同阈值复用 PSO，以及
后绘制的远处背景穿过裁剪孔洞；也保留不支持 AlphaClip 的 Shader 拒绝测试。


## 加载阶段与增量 Pipeline 预热

加载阶段顺序：资源及依赖驻留 → 创建 Scene Pass/帧资源和启动/Present Pipeline →
收集所有请求 Mesh 的 Submesh 材质（不依赖相机可见性）→ 排队预热 → Scene Ready →
前端激活场景。`ScenePreparationStatus.pipelinesTotal/pipelinesCompleted` 单独描述预热进度；
原有 total/completed 仍描述根资源准备。失败或取消不激活半完成场景，清理未执行预热任务。

`VulkanPipelinePreparation` 接收值拥有的 CreateInfo，保留其 Program、Pass、布局引用。
接收请求时只检查、计算完整 Key、查缓存和去重，不创建 PSO。跨请求的相同 Key 共享
同一任务，但每个请求拥有独立的 PipelinePreparationTicket；取消一个订阅不取消其他订阅。
终态必须 release。Ready 是本次快照的历史结果，后续材质/Shader/Pass 变化需要新请求。
取消不会驱逐其他 Draw 可能正在使用的已缓存 PSO。最后一个订阅取消时移除未执行任务，
立即释放描述和引用。已经派发的任务标记取消，尚未开始创建时工作线程跳过它；如果已进入
Vulkan 创建调用，则等待它结束并丢弃结果。场景重置取消订阅并清缓存，旧任务持有自己的
Program/Pass/布局快照，完成后不会发布，也不会移除相同 Key 的新任务。

每次 `advanceResourcePreparation()` 的顺序为 Upload tick → Asset Preparation 发布 →
Scene Preparation 协调 → Pipeline Preparation 收回结果并派发任务。专用线程串行创建 PSO，
每次 advance 最多派发 2 个任务，派发后尚未收回的任务总数也最多为 2；渲染线程不等待创建。
Queued 表示还在渲染线程队列，Preparing 表示已派发或部分完成，Ready 表示全部发布到缓存。
单次 Vulkan 创建无法抢占。启动/Present 的原有同步创建和绘制兜底不走此工作线程。
创建异常由工作线程捕获，在后续 advance 中进入 Failed/error；无效输入在接收阶段拒绝。
派发前 Draw 若已填充缓存，直接复用；派发后 Draw 与工作线程可能各创建一个原生 PSO，
发布时保留先入缓存的对象，丢弃重复结果。缓存 creations 统计唯一发布次数。

增量接口仍不暴露 Vulkan。先通过原有 prepare() 取得 Resource Ready，再请求当前
Scene Pass 的材质变体；Mesh 请求方可从其 CPU Submesh 收集 Material handles：

```cpp
auto& preparation = app.resourcePreparation();
// materialHandle 已经通过 prepare(assets, materialHandle) 达到 Resource Ready。
auto result = preparation.prewarmPipelines({materialHandle});
if (result.accepted())
{
    // 保存 result.ticket；主循环继续调用 preparation.advance()。
    auto status = preparation.pipelineStatus(result.ticket); // 后续帧查询
    if (render::pipelinePreparationFinished(status.state))
    {
        // 仅 Ready 才启用依赖本次预热的绘制内容；Failed/Cancelled 按业务处理。
        preparation.releasePipelines(result.ticket);
    }
}
```

无 Scene Pass、空请求、材质未驻留、Shader 不支持所需特性时拒绝请求，不隐式加载资源。
QueueFull 不生成 ticket，调用方后续重试。ResourcePreparationTicket 的 Ready 语义保持不变，
PipelinePreparationTicket 才表达 PSO 预热完成；`cancelPipelines` 独立取消，桥接层的
`cancelAll` 同时收回其资源和 Pipeline 订阅。

预热与 Compiler 统一调用 `describeMaterialPipeline()`。工作线程只使用值拥有的 CreateInfo
创建 GraphicsPipeline，通过受 mutex 保护的完成结果交回渲染线程；它不访问请求状态、
RenderAssetCache 或 GraphicsPipelineCache。渲染线程经缓存私有 publish 入口处理完成结果，
绘制兜底继续使用 getOrCreate，两条路径共用同一个应用缓存。

取消及场景重置不等待驱动调用；服务析构停止派发、丢弃待执行任务并 join 正在执行的线程。
Renderer 必须先于 Device 销毁，服务先于应用缓存销毁。当前原生创建使用 VK_NULL_HANDLE
作为驱动 PipelineCache，未共享 VkPipelineCache；未来接入该对象时需另行处理外部同步。
这不是通用 Job System：PSO 和通用资源各有一个独立工作线程，资源上传及请求发布仍由渲染线程推进。

回归覆盖加载后首次 Draw 无新 PSO、增量 Material Ready → 预热 → Draw 命中、重复请求
共享/取消、缓存命中、派发及在途数量限制、队列容量、无效批次无部分接收、在途取消后
同 Key 重试、派发前后绘制兜底竞争和服务退出 join。

## 两类 Preparation 的创建线程边界

两者的 request/prepare、advance、status、cancel、release 都在所属主/渲染线程调用。
VulkanResourcePreparation 与 VulkanPipelinePreparation 各有一个自己的创建线程，互不共用队列。
主线程 advance 不等待创建完成；各自默认每次最多派发 2 个任务，限制尚未收回的创建工作数量。

通用资源路径：

```text
主线程：接收 CPU 快照 → 版本/缓存/共享检查 → 等待依赖 → captureDependencies
资源线程：createGpuResources → buildUploadRequest → 返回创建结果或异常
主线程：advance 接收 → UploadService.tryEnqueue（容量不足下次重试）
GPU：执行上传
主线程：查询上传完成 → 再验目标和依赖版本 → publish → Ready
```

Shader/ShaderProgram/MaterialTemplate 与 HostVisible 材质等无 transfer 的任务跳过上传阶段。
ResourcePreparationState::Preparing 同时涵盖等待依赖、等待创建和等待上传接收；并不表示
工作一定还没执行。Accepted 只保证请求接收成功，设备分配、资源内容或上传构造错误可能在
后续 status 中成为 Failed/error。准备 ticket 容量不足仍在接收阶段返回 QueueFull；上传服务
暂时已满则保留创建结果重试，不要求调用方重新提交准备请求。totalBytes 在上传接收后可见。

captureDependencies 保存 Shader/Program/Template 强引用和 GpuTexture::snapshot()。
后者固定 Image、ImageView、Sampler 的同一次分配；缓存替换、移动或 reset 不会改变已捕获的
快照。工作线程不保存缓存 vector 元素的裸指针。未发布材质各用自己的 descriptor pool。

纹理替换也分阶段：主线程捕获受影响的材质参数和纹理绑定，资源线程预建新 descriptor，
上传完成后主线程核对原纹理发布序号、材质绑定及依赖版本，再整体交换并退休旧资源。
如果创建期间另一更新改变了这些绑定，本请求 Failed，旧缓存保持可用；调用方用最新快照重试。
主线程上的通用发布不再创建这些 Vulkan 对象。启动/Present Pipeline 和 Draw 缓存缺失兜底
仍保持显式同步路径；文件重导入已经迁移到同一异步准备路径。

取消只撤销自己的订阅。最后一个订阅取消后，未开始的工作跳过，正在执行的创建保留其私有输入
直到返回并丢弃结果；不会发布到已重置缓存，也不会碰新请求的记录。GPU 已提交的上传继续由
UploadService 持有引用直到安全完成。服务析构会 join 创建线程，然后释放所有未发布结果。

回归覆盖创建/捕获/发布的线程身份、阻塞创建期间取消及同目标重试、缓存 reset、创建异常、
退出 join、无上传资源、材质两种内存路径、纹理快照在旧缓存资源退休后仍可用于材质创建，
以及加载、版本替换和 PSO 预热的完整链路。

## Reimport 发布事务

```text
CPU 导入线程：生成临时 KTX2 和候选 TextureAsset
主线程：校验导入时的基准版本 → stageTextureReplacement 预留 revision
        → prepare(candidate snapshot) → 给独占 ticket 绑定 publication transaction
Resource 工作线程：创建纹理及新的材质绑定，生成上传请求
主线程 / GPU：正常 UploadService 提交和完成查询
主线程发布：transaction.begin → GPU cache publish → transaction.commit → Ready
```

begin 检查 CPU 资产未被修改并安装文件（保留备份）；commit 不抛异常，交换已验证的 CPU 候选
并确认文件提交；begin 或 GPU 发布异常时调用 rollback 恢复文件，CPU 不变。事务回调不执行
GPU 创建，不重入 Preparation。事务只允许附着到尚未创建且只有一个订阅的任务，该任务不再
接受共享订阅，避免一个调用方的文件提交影响另一订阅。常规准备不带事务，原有共享规则不变。

预留版本不会提前发布 CPU 内容，也不会在取消后重用。同一资产后续编辑、重试使用更高的
revision，因此 revision 可能跳号。CPU 资产域、handle 或基准版本改变会阻止旧候选提交。
这些事务保证运行过程中的提交/失败边界，不是跨进程崩溃恢复日志。

GUI 持续显示 reimporting，直到准备 ticket 终态；成功后按缓存 publication 序号刷新预览。
准备容量不足时保留候选重试；失败/取消保留旧内容并清理临时文件。退出时取消准备订阅，
未完成的 GPU 上传由 UploadService 保活。主线程最终仍执行文件 rename 与元数据交换，
不等待 GPU idle 或 drain。

回归覆盖真实 KTX2 cook → 异步准备 → CPU/GPU 同版本发布、准备期间取消、文件安装失败、
以及文件安装成功后 GPU 绑定版本冲突触发的磁盘回滚。版本单测验证候选不提前改变 CPU 内容、
取消版本不重用、普通编辑越过已预留 revision，以及并发编辑导致候选提交失效。


## 编辑器外部资产与材质工作流

以 `VulkanApp --editor` 启动。编辑器先准备默认 Shader、材质模板与空场景渲染资源，
不自动加载 Demo 模型。默认资源准备完成后，外部导入使用当前 AssetManager 做增量注册。

### 使用

1. 在 **Assets** 输入任意本地目录，点击 **Open Directory**；通过文件夹双击、Up、Refresh 浏览。
   支持 GLB/glTF、HLSL/SPIR-V、PNG/JPG/JPEG/TGA/BMP/KTX2。目录扫描在后台执行。
2. 把 GLB/glTF 文件拖到 **Scene Hierarchy** 的场景根或空白处，导入并实例化。
   拖到已有 SceneNode 上会成为其子节点。也可先双击导入，再从 Models 列表拖入；
   相同路径、导入设置与文件时间/大小的重复请求复用已导入资产，创建独立场景实例。
3. 双击 HLSL 前设置 **Shader stage** 和 **Entry point**。VS/PS 可以来自不同文件，
   也可以是同一个文件的两个入口。HLSL 每次显式导入都会重新编译，以包含 include 的修改。
   DXC 从 PATH 或 VULKAN_SDK/Bin 查找；后台直接启动进程，不使用 shell。
   编译输出位于临时目录，结束后清理，不覆盖源目录中的文件。
4. 双击导入贴图；颜色贴图选择 sRGB，法线和数据贴图关闭 sRGB。
   KTX2 保留容器声明的色彩空间。
5. 展开 **Create Material**，选 VS/PS → **Build Program**，为每个反射出的 image 选择 sampler
   → **Generate Material Fields**。参数布局来自反射；编辑参数并通过下拉框或拖放设置每个贴图槽
   → **Create Material Asset**。Shader 资源布局或 Pipeline 不兼容时显示错误。
6. 将 Materials 中的材质拖到 SceneNode，或者在 Inspector 的 **Instance material** 中选择。
   覆盖只影响该实例，不修改共享 Model/Mesh 与原始 GLB 材质。选 **GLB materials** 恢复原材质。
   Inspector 的 Position 可移动实例。新模型加入后自动取景，Viewport 的 **Frame Scene** 可重新取景。

最小示例：`assets/shaders/editor_unlit.hlsl` 与 `RenderData.hlsli` 放在同一目录。
分别以 Vertex / VSMain、Pixel / PSMain 导入前者；材质将生成一个 tint 参数和一个 colorTexture 槽，
把 colorTexture 配对到 colorSampler，选择任意已导入的颜色纹理，然后创建并分配材质。

### 调度和发布边界

```text
Assets / Hierarchy / Inspector：只入队编辑器命令
  ↓ 下一次主循环，ApplicationGui::update / EditorLayer::update
EditorAssetController
  ├─ CPU 导入 worker：文件读取、GLB 解析、贴图解码、DXC 编译、SPIR-V 反射
  ├─ 主线程：注册 CPU 资产、展开模型的 Mesh 依赖
  └─ ResourcePreparation::prepare
       ↓ Resource worker 创建 → UploadService 提交与完成 → 主线程发布 GPU 资源
     ResourcePreparation::prewarmPipelines
       ↓ Pipeline worker 创建 → 主线程发布 Pipeline
     插入 SceneNode / 设置实例材质覆盖
```

Controller 只接触 CPU 资产与 `render::ResourcePreparation` 接口，无 Vulkan 类型。
每次主循环最多提交四个根资源请求；QueueFull 保留队列重试。Ready、失败和取消路径均释放 ticket。
SceneNode 插入、材质覆盖都在资源与 Pipeline 完成后执行。失败保留原场景；已注册的 CPU 资产保留在
资产窗口供检查，并非整套导入的数据库回滚。退出/GUI detach 取消订阅并回收导入 worker。
窗口 resize 仅重建 GUI 渲染桥，不 detach 编辑器业务层，已提交的导入和准备任务继续推进。

### 当前支持范围

- 可选择任意路径和 HLSL 文件，但 shader 仍需符合现有绘制接口：set 0 为 Camera/Object，
  set 1 为一个材质 UBO 与独立 image/sampler，顶点输入兼容 asset::Vertex，PushConstants 遵守 Draw ABI。
  参数支持现有 scalar/vector/matrix 类型，纹理数组、多个材质 UBO 等继续由模板验证拒绝。
- GLB 导入默认保留 glTF PBR 材质。自定义材质目前是整个 SceneNode 实例的覆盖；未增加逐 submesh 编辑。
- 路径和资产引用仅保留在本次编辑器会话；尚无场景/材质文件保存、目录挂载持久化和自动文件监听。
  GLB/glTF 外部依赖的变化需要重新导入；主文件时间/大小缓存不是完整的依赖内容哈希。
- GLB 解析和图片解码在 worker，最终 CPU Mesh/Material/Model 注册和数据整理仍在主线程；
  尚未实现面向大文件的分帧注册。当前场景上限为 1024 个 submesh draw，超限拒绝新增实例。
- DXC 进程集成目前为 Windows 实现，支持 60 秒超时并回收子进程；GLB 的动画/蒙皮渲染能力未改变。

`editor-assets-test` 从空场景出发，在含中文和空格的临时外部目录验证浏览、HLSL 多入口编译与反射、
独立贴图上传、Unlit 材质/PSO、GLB 重复实例化、实例覆盖、取消及编译失败，并实际记录和提交绘制命令。
