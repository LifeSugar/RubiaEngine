# 编辑器启动与内容加载

启动以 GUI 可以独立运行作为边界。`App::initVulkan()` 只创建
`VulkanContext` 和基础呈现资源，不读取模型或 shader，不上传场景资源。
相机设置也独立于 Vulkan 初始化。

## 执行流程

1. 创建窗口、设备、交换链、帧命令与同步资源。
2. 初始化 ImGui 和 GUI bridge，进入主循环。
3. 提交第一个 GUI 帧后，根据 `RunConfig::autoLoadDemo` 发起内容加载。
4. 工作线程在独立的 `PreparedContent` 中读取 shader、导入模型和解码贴图。
5. 主线程取得 CPU 准备结果，向 renderer 提交 `SceneResourceRequest`，每帧推进准备会话。
6. Vulkan 后端完成上传和 pipeline 准备后返回 Ready，App 在帧边界启用场景并移交完整的
   AssetManager、Scene、TextureImportRegistry 和 DemoContent。

GUI 只访问主线程的资源。在完成移交之前，资源面板和场景保持为空。
加载阶段、准备进度、完成信息和错误统一写入现有 Console，不创建加载面板。
准备进度按约 10% 的里程碑记录，避免每帧刷屏。加载失败不会退出编辑器。
当前只由主循环在首帧后自动发起一次加载，GUI 不提供加载或重试入口。

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
- `VulkanResourcePreparation`：统一管理全部七类 Asset 的准备记录、依赖、状态、取消和完成发布。
  `IResourcePreparation` 的具体实现只负责创建 GPU 对象、构建上传请求、发布到 cache。
  准备 ticket 独立于上传 ticket；空上传请求跳过 UploadService，仍通过 advance 完成发布。
  Renderer 保留外部接口并转发；组件借用 Device/UploadService，不推进共享服务的 tick。
  同步重导入适配器 `uploadTextureAndWait()` 也由该组件实现，显式 drain 后返回未发布纹理。
  析构只取消并释放自己的 ticket；已提交资源继续由上传服务保活，不清空或关闭共享服务。
- `VulkanScenePreparation`：仅组合通用 Model / MaterialTemplate / ShaderProgram 准备 ticket，
  等待 Ready 后创建 pipeline，协调场景启用与取消；不持有 UploadService 或 UploadTicket。
  状态为 PreparingResources → PreparingPipelines → Ready → Activated；失败/取消保持 GUI 可用。
  进度按根请求计数，一个 Model Ready 表示其所有依赖已就绪，不表示单个 GPU copy。
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
- `AppContentLoading.cpp`：管理 Preparing、Uploading、Finalizing、Ready、Failed
  应用状态，负责 CPU 工作线程、提交准备请求、展示状态、结果移交和请求取消。
  不创建命令池、不操作上传批次、不查询 fence、不构造 Vulkan pipeline。

当前一个 renderer 同时管理一个场景准备会话，只接受空场景和空 RenderAssetCache；
失败或取消后可以重新开始，已有场景不会被新请求覆盖。cache 仍由应用持有，
必须比 renderer 活得更久；本次边界调整尚未包含 cache 所有权迁移、场景热切换、
App 整体的 RHI 抽象。纹理重导入的传输已接入共享服务，但替换事务仍同步执行。独立贴图可以在空编辑器或已激活场景中准备，
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

纹理重导入调用 `VulkanRenderer::uploadTextureAndWait()`，通过同一个服务入队并 drain，
返回尚未发布的新纹理，内部 ticket 自动释放。它可能推进其他已接收上传，但不提前发布
独立纹理。App 不再创建上传 CommandPool / UploadContext；缓存替换、descriptor 更新及
CPU/磁盘事务仍由原提交流程完成，替换前等待已有渲染结束。

独立版本更新先上传新 GPU 对象，旧缓存继续可用。上传成功后在帧边界等待 GPU 空闲并提交替换；
材质引用由 cache 更新，GUI preview 按缓存发布序号刷新 descriptor。当前替换提交可能阻塞一帧，
并非无等待的资源退休队列。advanceResourcePreparation 必须位于录制/绘制本帧 GUI 和场景之前。
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

预算在完整 operation 之间检查；分配 GPU 对象、单次 staging 拷贝和 pipeline 创建仍可能
使主线程超时。此版本不提供传输线程、staging 页池或大资源分块。
普通上传取消不等待 GPU，在途批次继续持有所需对象；场景整体取消仍保留原有阻塞清理。
独立纹理还单独记录准备取消状态：即便传输已被 drain 完成，尚未发布的纹理仍可取消，
后续 pump 不会访问被释放的纹理，也不会把它重新发布。

关闭窗口时先请求 CPU 取消并 join，再等待尚未完成的上传 fence，然后释放
目标资源、GUI 和 Vulkan。取消检查位于导入阶段之间及贴图解码前；单次文件
解析或图片解码可以先完成。工作线程从不 detach，也不持有 App 引用。
EditorApp 保证 App 的清理先于 EditorLayer 日志捕获器的销毁。

## 使用与验证

默认启动保持自动加载 demo；`VulkanApp.exe --editor --empty` 打开空编辑器，
禁用自动加载并保持空场景。

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

Renderer 的 `prepareTexture / prepareMesh / prepareShader / prepareShaderProgram /
prepareMaterialTemplate / prepareMaterial / prepareModel` 均接收 `assets.snapshot(handle)`，
返回可查询、取消、释放的 PreparationTicket。prepareModel 自动准备传递依赖，
所有类型共用 CacheHit / Shared / Started 规则，并将依赖版本纳入比较。

Shader、Program、Template、Material、Model 自身不产生 transfer 请求；Material 的参数
当前写入 host-visible buffer。它们仍需创建设备对象或等待 Texture/Mesh 依赖上传。
父请求在依赖就绪前保持 Preparing，字节数仅表示自身传输。取消父请求释放依赖订阅，
不会取消仍被其他调用方等待的共享任务，也不清除已发布缓存。

ShaderProgram Ready 只保证模块与布局可用；完整 graphics pipeline 仍需渲染上下文。
`GraphicsPipeline::CreateInfo::program` 可复用已准备模块。
Demo ScenePreparation 仅组合这些通用准备并协调 pipeline/场景激活；独立 Scene Upload 路径已删除。
