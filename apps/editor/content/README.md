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
加载阶段、上传进度、完成信息和错误统一写入现有 Console，不创建加载面板。
上传进度按约 10% 的里程碑记录，避免每帧刷屏。加载失败不会退出编辑器。
当前只由主循环在首帧后自动发起一次加载，GUI 不提供加载或重试入口。

## 职责与接口

- `VulkanRenderer::createPresentation()`：创建无需场景 shader 的基础呈现。
  `operator bool()` 表示基础呈现可用，`sceneReady()` 单独表示场景渲染可用。
- `renderGui()`：只清理交换链颜色并绘制 GUI；不会执行场景 pass，也不会读取
  RenderAssetCache。没有场景时，GUI bridge 不注册视口纹理。
- `beginScenePreparation()` / `advanceScenePreparation()` / `scenePreparationStatus()`：
  接收 CPU 资源请求、推进后端准备并返回阶段、已提交数、已完成数和错误。
  请求与状态定义在 `engine/render/SceneResourcePreparation.hpp`，不包含 Vulkan 类型。
- `activatePreparedScene()`：在 Ready 后启用场景，再由 App 移交 CPU 内容。
  Ready 阶段仍允许 GUI 绘制和 resize，`sceneReady()` 直到启用后才为 true。
- `cancelScenePreparation()`：取消尚未启用的准备，清理上传和部分场景资源，
  保留基础呈现。取消已启用的准备不会销毁正在使用的场景。
- `VulkanUploadService`：由 renderer 随基础呈现创建并长期持有，管理上传命令池、
  UploadContext、请求队列、全局预算、批次提交、fence 查询和传输资源回收。
- `VulkanScenePreparation`：协调 Demo 的资源准备和 pipeline 创建；纹理和 mesh
  请求共享上传服务，材质参数仍使用现有 host-visible buffer 路径。
- `advanceResourcePreparation()`：每帧推进共享上传与结果发布；App 即使处于 Idle / Ready
  也调用它。`advanceScenePreparation()` 仅保留为兼容入口，两者每帧选一个调用。
- `prepareTexture()` / `texturePreparationStatus()` / `cancelTexturePreparation()` /
  `releaseTexturePreparation()`：独立贴图准备，不需要模型或场景 pipeline；
  上传完成后才增量发布到 cache。取消未完成请求不会破坏已有贴图。
- `DemoContentLoader::loadBuiltins()` / `loadModel()`：将默认纹理、材质模板和
  shader 的创建与具体模型导入分开；`load()` 保留同步组合入口供 CPU 测试使用。
- `RenderAssetCache::initialize()`：根据材质模板建立布局，不要求存在模型。
  `beginUpload()` 准备 Demo 的资源列表和描述符；`prepareNext()` 通过共享服务准备资源。
  旧 `create()` / `uploadNext()` 同步路径已删除。
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
由 `submitBatch()` 提交、`pollBatch()` 查询 fence，staging 与命令缓冲保留到批次完成。
服务在整个执行期间持有目标引用；源数据在录制阶段复制到 staging。

纹理重导入调用 `VulkanRenderer::uploadTextureAndWait()`，通过同一个服务入队并 drain，
返回尚未发布的新纹理，内部 ticket 自动释放。它可能推进其他已接收上传，但不提前发布
独立纹理。App 不再创建上传 CommandPool / UploadContext；缓存替换、descriptor 更新及
CPU/磁盘事务仍由原提交流程完成，替换前等待已有渲染结束。

共享服务的请求持有不可变 CPU 来源和目标 GPU 对象的引用，入队只接管数据，
实际 staging 拷贝在 tick 中执行。独立贴图和 mesh 上传完成前不对 cache 查询可见。
服务的 submittedBytes / completedBytes 按提交与 fence 完成分别更新；
场景状态仍按已准备好的资源数报告，包含不经 transfer 的材质创建。

每批默认按 4 ms、16 MiB 或 16 个传输 operation 的软阈值停止添加，
最多保留一个在途批次。硬限制默认为 256 MiB staging、512 MiB 活跃请求的逻辑源数据量、
1024 个未释放 ticket。单个 operation 超出硬上限会明确拒绝；暂时排队空间不足返回 QueueFull。
请求可以跨批次，多个请求可以合入一批。Demo 适配器当前逐资源推进，后续可扩大准备窗口。

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

- `--startup-test`：设备本地 buffer 上传回读、多操作请求、批次合并与取消、容量与重试、
  独立贴图上传和 ImGui 预览，以及空 GUI、空场景 resize、CPU 取消、缺失模型、重复请求拒绝、
  上传取消与重启、Ready 时 resize 与取消、CPU 来源生命周期和上传中关闭。
- `--render-test` / `--editor-test`：缺失 shader 后的 GUI 和 resize、测试代码发起重试、上传中
  持续绘制 GUI、完整场景绘制；保留原有贴图替换、重导入及 resize 验证。
- `--asset-test`：原有 CPU 资源导入与校验。
