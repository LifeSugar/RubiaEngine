# Renderer 资源退休边界

`rhi::PresentationSurface` 配置窗口输出并获取短期 `AcquiredFrame`；帧只公开
`PassTarget` 和 `OutputDescription`，不要求可采样纹理、原生 image index 或同步句柄。
因此 OpenGL 默认 framebuffer 也能实现同一契约。当前只实现 Vulkan 后端。

`rhi::FrameExecutor` 管理 CPU 帧槽、命令录制、提交和呈现。Vulkan 的
`VulkanPresentation` 实现这两个接口，内部持有 `SwapchainResources` 和 `FrameContext`。
CPU 帧槽数量独立于交换链 image count；图像索引、binary semaphore 和 fence 不进入通用 Renderer。
现有 Vulkan pipeline / ImGui 适配仍通过专用接口取得 render pass、command buffer 和 image count。

`PresentationConfig` 当前协商 SDR format 和 FIFO / Mailbox / Immediate 呈现偏好；
不支持的呈现模式退回 FIFO，实际配置由 `OutputDescription` 返回。默认 BGRA8 UNorm 保持现有
GUI 色彩行为。零尺寸配置暂停 acquire；非零配置恢复。HDR 协商不在当前范围。

`render::executePresentationFrame()` 负责通用帧顺序，`FrameScheduler` 独立记录提交完成与资源退休。
`VulkanRenderer` 提供场景录制回调，不再直接 acquire、操作 fence/semaphore 或提交队列。

## 所有权与提交

- `RenderAssetStore` 保存当前驻留版本，发布替换时将旧对象移交给调用方提供的退休容器。
- `ResourcePreparationService` 借用 Renderer 的 `FrameScheduler::pending()`，不管理帧槽或 fence。
- `RetirementBatch` 只持有对象，不等待 GPU；入队分配失败不会移动源对象。
- `FrameScheduler::pending()` 尚未关联成功提交。每个帧槽的 `retired` 由该槽最近一次成功提交保护。
- 材质参数 buffer、descriptor pool、模板引用和绑定元数据作为一个整体退休。

一次 `executePresentationFrame()` 的相关顺序：

1. 等待当前帧槽的上一次提交完成。
2. 重置旧 command pool，清空该槽上一轮的 `retired`。
3. 获取交换链图像，录制并提交新命令。
4. 仅在 `FrameExecutor::submit()` 成功后，立即用无分配、无异常的 `commitSuccess()` 将待退休对象转交给该槽，然后调用 `present()`。
5. 下次复用该槽时，等待这次提交的 fence 后才能释放这一批对象。

不能在步骤 2 释放 `FrameScheduler::pending()`：其他帧槽可能仍使用其中的资源，当前槽的旧 fence
并不覆盖这些提交。获取图像失败、录制异常或提交失败时，待退休批次继续保留；提交成功后
即使 presentation 要求 resize，批次也已经正确关联到提交。不能等待已 reset 却没有成功提交的 fence 来恢复渲染。

取图后丢弃帧、录制或提交异常会使 Vulkan 呈现会话失效，后端拒绝重用同步资源。
恢复时先 device idle、reset commands、收集退休对象，再调用 `configure()` 重建交换链和帧槽。
`Unavailable` 跳过本帧，`NeedsReconfigure` / `Suboptimal` 请求重配；`SurfaceLost` 需要宿主重建
平台 surface / presentation session，不能仅当作普通 resize。

无更新时，两个容器都为空，正常帧不遍历全部资源、不收集每次 draw 的所有权引用。
Resize、场景释放和 Renderer 关闭在 device idle 后重置旧命令并清理待提交及帧槽两类批次。
单独调用 `waitIdle()` 只同步，不改变容器所有权；`drainRetiredResources()` 则等待并清理两类批次。

## 当前适用范围

- Mesh、Shader、ShaderProgram、MaterialTemplate、Material 替换通过退休容器交接，
  Cache 的这些发布入口不再调用 `waitIdle()`。
- 通用 Texture 发布创建全部受影响材质的新 descriptor set，再集中切换纹理与绑定；
  旧纹理与旧绑定作为一个整体退休，不调用 `waitIdle()`。所有可能失败的分配在切换前完成。
- `GpuMaterial` 用 `shared_ptr<const Buffer>` 持有已发布参数；纹理绑定版本共享该 buffer，
  `withTextures()` 只写新 descriptor，拒绝原来的 set。真正的参数创建仍分配新 buffer。
- 纹理发布同步更新受影响材质的驻留依赖版本。准备调度器在依赖完成后再次检查驻留结果，
  避免 Mesh 依赖刷新把已经更新的材质重建一遍、重新分配参数。
- ImGui 预览按 publication 创建新 registration；旧 registration 交给 Renderer 退休。
  GUI detach 在 ImGui backend 销毁前调用 `drainRetiredResources()`，确保旧回调仍能访问池。
  GUI token 应在每帧通过 preview 获取，发布不允许与 GUI/场景命令构建交错。
- 编辑器文件重导入的旧事务仍显式等待，保留 CPU/文件回滚和返回旧纹理的契约；它也创建
  新材质绑定并共享参数，但不会将返回给调用方的旧纹理交给异步退休队列。显式 GUI
  invalidatePreview 同样要求调用方已完成 GPU 使用；普通预览刷新不需要该同步入口。
- 当前相关渲染和上传都在同一 graphics queue，发布发生在 `renderFrame()` 外且不与录制交错。
  被替换对象不允许被后续命令重新引用。Cache 的直接 `reset()` 仍要求调用方先完成所有 GPU 使用。
- 增加其他资源使用队列、可重放旧命令或原地纹理更新时，必须扩展同步及依赖失效规则。
- 持续发布但不提交帧会积累待退休资源；本次不包含流式显存预算、背压和多批次上传。

独立使用 `ResourcePreparationService` 的调用方也必须提供退休容器，并负责在 GPU 使用完成后
清理；容器、Cache 和 Device 的生命周期需要覆盖相应使用期。

## GUI 后端边界

`ImGuiLayer` 只持有 ImGui context 与注入的 `GuiPlatformBackend` / `GuiRendererBackend`。
平台接口处理输入、帧事件和 DPI；渲染接口处理初始化、NewFrame、绘制数据准备、录制、
重配与纹理注册。应用入口目前选择 GLFW 平台适配和 `VulkanGuiRenderer`。

`ApplicationGuiRenderBridge` 通过 `GuiRenderHost` 取得视口输出、CPU 帧槽和退休服务，
通过统一渲染后端注册 `rhi::TextureViewRef`；不直接依赖 Vulkan 或调用 `ImGui_ImplVulkan_*`。
`VulkanGuiRenderHost` 负责把现有 VulkanRenderer 接到该接口。

`GuiTextureRef` 同时持有 GUI 注册和原始 texture view，token 只供 UI 借用。
纹理发布或视口输出替换时，旧注册随 GPU 提交延迟退休；新注册分配失败保留原注册。
关闭或重配 GUI 时，先等待 GPU 并 detach bridge、清空退休注册，再操作后端。
Vulkan 注册释放会切换到所属 ImGui context；后端关闭后，残留 CPU 注册的 token 失效，
析构也不会访问已销毁的 descriptor pool。后端关闭仍要求调用方先完成所有 GPU 使用。

图像数量不变时重建 Vulkan GUI pipeline；图像数量改变时重新初始化后端，以更新官方
ImGui 的绘制缓冲环。所有字体图集维护仍在 prepare 阶段执行，不在已开启的 render pass 内上传。

`Rubia::GuiIntegration` 与 `Rubia::EditorGui` 不链接 Vulkan/GLFW；平台和 Vulkan 适配分开构建。
OpenGL3、D3D12、Metal 官方源码已按现有 ImGui 提交补齐，尚未接入引擎运行路径。

## 应用后端入口与运行接口

`App::initBackend()` 根据 `RunConfig::graphicsApi` 选择具体实现，当前创建
`VulkanApplicationBackend`；Direct3D12 / Metal / OpenGL 选择会明确报未实现，不自动回退。
窗口使用平台层 `platform::glfw::Window`，其尺寸为 `platform::Extent2D`。

`ApplicationBackend` 是应用会话接口：提供帧渲染、呈现重配、场景准备、资源准备宿主、
GUI 宿主和 GUI 适配器创建。`App.hpp`、`AppFrameLoop.cpp`、`AppResize.cpp` 及内容加载、
纹理重导入流程不引用原生 Vulkan 类型。帧结果统一使用 `render::RenderResult`。
后端专用诊断测试显式取得 Vulkan 实现，不通过应用运行接口暴露设备或 command buffer。

`VulkanApplicationBackend` 持有 context、renderer 和 GUI host；尺寸转换发生在该实现内。
GUI 适配器和资源准备 session 借用后端，会在后端之前销毁。关闭顺序为：取消任务、等待 GPU、
释放 GUI 注册和 GUI 后端、释放资源准备 session、shutdown renderer、释放驻留资源，
最后销毁后端设备与窗口。`shutdown()` 停止 renderer，但保留 device 供驻留对象析构。

边界检查禁止运行层重新包含 Vulkan 头文件或使用 `Vk*` 类型；替身后端测试直接运行
真实 App 帧循环，覆盖 scene / GUI-only 分支、跳帧、重配请求、关闭顺序和未实现 API 拒绝。
