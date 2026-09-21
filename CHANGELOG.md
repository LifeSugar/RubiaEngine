# Changelog

记录 RubiaEngine 在 `dev` 分支上的功能、架构演进与修复，按版本倒序排列。


## 0.24.0 — 资源准备前端与 GPU 资源延迟释放


- 新增后端无关的 `render::ResourcePreparation` 接口，统一准备请求、状态查询、取消与释放；通过 Vulkan Bridge 对接后端，在主循环中独立推进增量资源准备。
- 将 Model 拆分为 Mesh 的工作移至 CPU 渲染前端；场景请求携带具体资源的不可变快照，后端不再接收模型层级或缓存 Model。
- 材质参数默认使用 Device Local Buffer，经共享 UploadService 上传；允许在 Renderer 创建时配置 Host Visible 等内存属性，并为非 coherent 内存写入补充 flush。
- Renderer 组合 FrameContext 与帧槽退休容器：Mesh、Material 及共享准备资源替换后延迟释放，成功提交后绑定 fence；通用纹理更新创建新 descriptor，并共享不可变参数 buffer。
- ImGui 预览同步接入资源退休；原有文件重导入事务仍保留同步等待。
- 补充前端资源准备、版本替换、材质上传和生命周期相关回归覆盖。

相关设计：[资源退休](renderer/vulkan/RESOURCE_RETIREMENT.md)、[内容加载与准备](apps/editor/content/README.md)。

## 0.23.0 — 2026-09-20 — Asset Preparation 与版本缓存

- 为资产引入内容版本、管理器 domain 和不可变 `AssetSnapshot`，在准备期间保活 CPU 数据及依赖快照。
- 为 Texture、Mesh、Shader、ShaderProgram、MaterialTemplate、Material 和 Model 提供统一准备流程及 ticket/status；本阶段的 Model 准备保存层级快照与依赖就绪记录。
- 支持同版本缓存命中、准备任务共享和新版本替换；根资源与传递依赖版本共同参与匹配，取消单个订阅不会中断其他订阅者的共享任务。
- 按依赖关系准备资源，全部就绪后发布；失败或取消时保留旧驻留版本。该阶段替换 GPU 资源仍使用 `waitIdle` 保守同步。
- 删除 Demo 独立 Scene Upload 路径，场景协调器组合通用准备请求，并使用已准备的 ShaderProgram 和材质模板创建 Pipeline、激活场景。
- 补充资产版本、快照、依赖准备、共享取消、缓存替换与真实场景接入测试。

关键提交：`e1c224c`。

## 0.22.0 — 2026-09-20 — 编辑器外观与偏好设置

- 新增 `EditorTheme` 与外观偏好设置，统一编辑器配色、控件样式和布局细节。
- 接入 Fira Code 字体，调整 Inspector、Asset Browser、Console 与 Scene Hierarchy 的显示。
- 补充编辑器主题测试。

关键提交：`614026d`。

## 0.21.0 — 2026-09-17 — Image 上传构建与校验

- 抽出 `TextureUploadBuilder`，将纹理上传请求构建与 GPU 纹理对象分离。
- 新增统一 `UploadValidation`，校验 Buffer/Image 复制区域、数据范围与布局约束。
- 整理 Image 上传链路，覆盖已有 Image 局部更新、多 mip、数组层、padding 与 BC 压缩块边缘复制。
- 扩充 Vulkan 上传回归测试，检查未覆盖区域的数据保留。

关键提交：`cda1b81`。

## 0.20.1 — 2026-09-16 — 统一上传入口与生命周期维护

- 移除 Mesh、GpuTexture 和 RenderAssetCache 中重复的旧上传入口，统一交由 UploadService 管理。
- 精简 UploadContext 的职责，整理纹理重导入、独立资源上传与场景加载的调用链。
- 补充上传生命周期注释及启动、取消与资源上传回归覆盖。

关键提交：`52a7205`、`d5dd900`。

## 0.20.0 — 2026-09-14 — VulkanUploadService

- 引入独立 Vulkan Upload 层，集中接收 Buffer/Image 上传需求，按预算选择本帧执行的操作。
- 添加 Upload Request、Ticket 与 Status，支持上传状态查询、批次组织、提交与取消。
- 将上传源数据、staging 和目标资源保活至 GPU 操作完成，使用 fence 管理在途批次。
- 将 Mesh、Texture 和场景资源上传接入统一服务，并添加上传专项测试。

关键提交：`300e84e`。

## 0.19.0 — 2026-09-11 — 场景资源准备会话

- 添加 `SceneResourcePreparation` 请求与状态模型，以及 Vulkan 场景准备实现。
- 将 CPU 内容准备、GPU 资源准备和场景启用分为明确阶段，集中处理进度、取消与失败。
- 调整 App、Runtime GUI 和 Editor 的加载状态接入，补充场景启动与准备测试。

关键提交：`8272cff`。

## 0.18.0 — 2026-09-10 — ShaderProgram 与反射材质模板

- 添加 `ShaderProgramAsset`，在 CPU 侧组合 Shader 阶段、合并反射接口并校验阶段间兼容性。
- 添加 `MaterialTemplateBuilder`，从 Program 的材质 descriptor set 生成参数布局、纹理映射和校验规则。
- 分离 Shader 字节码、Program 接口与材质模板职责，使用签名描述代码、布局及接口。
- 调整 Vulkan Pipeline 与材质缓存消费路径，补充 ShaderProgram 和材质反射测试。

关键提交：`f998b03`。

## 0.17.0 — 2026-09-10 — GUI 优先启动与异步内容加载

- 让 GUI 启动独立于 Demo 资源，后台执行 CPU 内容准备，并将 GPU 上传分帧推进。
- 拆分基础呈现与场景渲染初始化、内置资源与模型导入；在界面展示加载状态并支持失败重试。
- 添加 `--editor --empty` 空编辑器入口、`--startup-test` 启动与退出测试，以及可选 GPU CTest 注册。
- 上传批次通过 fence 管理 staging 生命周期，移除上传过程中的队列空闲等待。

关键提交：`2b2c18e`。

## 0.16.1 — 2026-08-30 — 仓库忽略规则维护

- 更新 `.gitignore`，移除不应跟踪的本地信息文件。

关键提交：`22ca601`。

## 0.16.0 — 2026-08-27 — RubiaEngine 命名与模块化工程

- 将原 `head` / `src` 结构重组为 `engine`、`renderer/vulkan`、`importers`、`apps/editor` 和 `tests` 等模块。
- 拆分各模块 CMake 配置与依赖配置，调整头文件路径并统一使用 `.hpp`。
- 将项目命名调整为 RubiaEngine，整理 `rubia` 命名空间及 App、Renderer 的类型边界。

关键提交：`95cce0a`、`de80b54`、`6edaf85`。

## 0.15.0 — 2026-08-26 — 纹理导入设置与编辑器资源工具

- 添加纹理导入设置、导入记录与 KTX2 Cook/写入链路，在 Texture Inspector 中编辑设置并触发重导入。
- 串联磁盘产物、CPU Texture Asset 与 GPU 纹理的更新，补充重导入失败处理和回归覆盖。
- 完善 Demo 资源，扩展 Asset Browser、Console 和 Material Inspector 的资源浏览、日志与编辑能力。

关键提交：`b3e5771`、`2c67263`。

## 0.14.0 — 2026-08-25 — 纹理格式、VMA 与 GUI 后端解耦

- 将 WIC 图片解码替换为 stb_image。
- 添加 KTX 纹理导入、BCn 压缩格式和多 mip 数据支持，统一 Texture Asset 到 Vulkan 格式的映射及上传处理。
- 接入 Vulkan Memory Allocator（VMA），调整 Buffer/Image 内存分配与释放。
- 通过 GUI Render Bridge 分离编辑器界面与 Vulkan 后端实现。
- 整理第三方依赖许可与过时文档。

关键提交：`7b0c96c`、`495bc97`、`0c6dbb3`、`18cfefc`。

## 0.13.0 — 2026-08-24 — 编辑器模式与 CPU 渲染前端

- 引入 Editor 模式，拆分 App、Runtime GUI 与 Editor 职责；添加场景选择和 Inspector，并按资源类型拆分检查器。
- 分离 Render Frontend 与 Vulkan Backend，让 CPU 侧 `buildRenderFrame()` 无需 Window、Device 或 GPU Cache 即可执行。
- `RenderItem` 改为仅保存 Asset Handle、Pipeline Key 和 Draw 元数据，移除 GPU Mesh/Material 指针。
- 将 RenderAssetCache 改为按 Handle 槽位索引，提供 generation 校验与 `tryMesh` / `tryMaterial` / `tryTexture` 查询。
- 添加 CPU Shader 反射与材质接口校验，提前发现布局与资源绑定不匹配。

关键提交：`0db269c`、`304b76b`、`71adf3a`、`6942956`、`b6b0f45`、`46ec5e8`、`7f15d5f`。

## 0.12.0 — 2026-08-21 — ImGui 与 AssetID

- 集成 ImGui，建立应用内调试与编辑器 GUI 的基础。
- 添加 AssetID，为资源身份管理提供基础设施。
- 补充编辑器阶段规划。

关键提交：`60f5038`、`d4abec9`、`322cb27`。

## 0.11.0 — 2026-08-19 — 相机剔除与有序 RenderList

- 添加 `RenderLayer`、`LayerMask` 与 Camera Culling Mask，支持按视图过滤场景对象。
- 添加 Mesh Local AABB、Candidate World AABB、Vulkan Zero-to-One Frustum 与 `CullingSystem`，提供可见性结果和剔除统计。
- 添加后端无关的材质渲染状态、`MaterialKey` 与 `PipelineVariantKey`，描述透明度、裁剪、双面和深度设置。
- 将场景提取、剔除、列表分类和排序拆分为独立步骤；建立 Opaque、AlphaClip、Transparent 队列。
- 不透明对象采用深度分桶和状态排序，透明对象按远到近排序，并提供确定性的平局规则。
- 使用 RenderView 身份与数据版本更新逐帧相机数据；通过 Object 索引及连续状态缓存减少数据搬动和重复绑定。

关键提交：`03540ae`。

## 0.10.0 — 2026-08-18 — Offscreen Scene Pass 与 Present Pass

- 添加 `RenderTarget`，为每个 Frame Slot 创建线性 HDR Scene Color 与 Depth Attachment。
- 添加全屏三角形 Present Pipeline、独立采样 Descriptor 与通用 Sampler RAII 封装。
- 渲染改为先执行 Scene Pass，再采样场景颜色执行 Present Pass；集中处理曝光、ACES Tone Mapping 与 sRGB 输出编码。
- 扩展 GraphicsPipeline 的拓扑、深度、裁剪和采样配置；Swapchain 保存完整 Surface Format 与 Color Space 信息。

关键提交：`a54d017`。

## 0.9.1 — 2026-08-14 — GPU 选择修复

- 修复 `preferIntegratedGPU` 配置未被正确遵守的问题。

关键提交：`06a6fbf`。

## 0.9.0 — 2026-08-06 — Camera UBO 与 Draw Push Constants

- 将 Camera 数据从 Storage Buffer 调整为 Uniform Buffer。
- 使用 Push Constants 传递每次 Draw 的 Camera/Object 索引，明确 CPU 与 Shader 的数据布局及容量约束。

关键提交：`13da101`。

## 0.8.0 — 2026-08-04 — Asset、Scene 与 Rendering 解耦

- 添加与来源格式无关的 Texture、Material、Mesh、Shader 和 Model Asset，以及 AssetManager、类型安全 Handle 与 Registry。
- 添加 GLB Texture/Material/Mesh/Model Importer 和 WIC 图片解码路径。
- 添加 RenderAssetCache，负责从 CPU Asset 创建并缓存 GPU Mesh、Material 与 Texture。
- 建立独立 Scene 层与 SceneRenderExtractor，将场景提取为 RenderFrame；Renderer 不再直接依赖 GLB 数据结构。
- 引入 `ABeautifulGame.glb` 作为材质和场景渲染验证资源。

关键提交：`c4d044e`。

## 0.7.0 — 2026-07-27 — Vulkan 资源 RAII

- 添加 Fence、Semaphore、FrameContext、Framebuffer、RenderPass 和 SwapchainResources RAII 封装。
- 将 Swapchain 相关创建、同步和销毁逻辑迁移至资源类，加强失败清理与参数校验。
- 补充 Apple 平台所需扩展配置。

关键提交：`fd244dc`、`fcc598a`。

## 0.6.0 — 2026-07-23 — Frame Resources 与 Pipeline 拆分

- 按 Frame Slot 管理帧资源，重构 Acquire、Submit、Present 与 In-flight Fence 同步流程。
- 整理逐帧数据及 Descriptor 管理，进一步拆分 VulkanRenderer 的职责。
- 抽出独立 GraphicsPipeline 封装。

关键提交：`d7b4ebc`、`ab14176`、`302918f`、`b1ad5a4`。

## 0.5.1 — 2026-07-17 — Swapchain 修复

- 修正拆分后的 Swapchain 实现。

关键提交：`cac9246`。

## 0.5.0 — 2026-07-13 — Buffer 与 Swapchain 抽象

- 开始拆分单体 VulkanApp，将 Buffer 和 Swapchain 提取为独立组件。
- 调整渲染初始化与资源管理的代码组织，为后续帧资源和 RAII 重构建立边界。

关键提交：`36af788`、`2c2eba2`。

## 0.4.0 — 2026-07-07 — 模型绘制、相机与深度缓冲

- 实现 Buffer 创建与内存管理，完善模型加载及绘制所需缓冲区。
- 添加 Camera 配置、变换和投影矩阵更新，调整默认相机位置以改善场景可见性。
- 接入深度缓冲与 Uniform Buffer，更新基础顶点和片元 Shader。

关键提交：`684d787`、`da34f9f`、`01c480a`、`0882f11`、`20219d0`、`c697c1f`、`12180e5`。

## 0.3.0 — 2026-06-17 — GLB Loader

- 添加 GLB 模型加载基础能力。
- 补充从 Hello Triangle 演进至模型渲染器的路线图与 Vulkan 学习资料。

关键提交：`5f5d40d`、`03ec320`、`baa0ad1`。

## 0.2.0 — 2026-06-12 — Vulkan 初始化与基础绘制管线

- 建立 Vulkan Instance、Debug Messenger、Surface、物理设备筛选、逻辑设备与 Swapchain 初始化流程。
- 添加集成/独立 GPU 偏好与设备评分，完善设备能力检查和日志。
- 添加 Shader 编译脚本、基础顶点/片元 Shader 与 Graphics Pipeline 创建。
- 接入 Command Buffer 与同步对象，形成基础绘制流程。

关键提交：`181435f`、`45cdb19`、`0fe2561`、`1c6ff4a`、`50f5e85`、`7a5ae8e`、`bd22c58`、`d185c8f`。

## 0.1.0 — 2026-05-19 — 项目初始化

- 初始化 LearnVulkan 仓库，建立项目起点。

关键提交：`3d39a37`。
