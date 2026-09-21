# Renderer 资源退休边界

`FrameContext` 继续只持有 command pool、command buffer、image-available semaphore 和
in-flight fence。`VulkanRenderer::RenderFrameSlot` 将它与 `RetiredResources` 组合；
`renderFrame()` 的调用边界保持不变。

## 所有权与提交

- `RenderAssetCache` 保存当前驻留版本，发布替换时将旧对象移交给调用方提供的退休容器。
- `VulkanResourcePreparation` 借用 Renderer 的 `pendingRetired_`，不管理帧槽或 fence。
- `RetiredResources` 只持有对象，不等待 GPU；入队分配失败不会移动源对象。
- `pendingRetired_` 尚未关联成功提交。每个帧槽的 `retired` 由该槽最近一次成功提交保护。
- 材质参数 buffer、descriptor pool、模板引用和绑定元数据作为一个整体退休。

一次 `renderFrame()` 的相关顺序：

1. 等待当前帧槽的上一次提交完成。
2. 重置旧 command pool，清空该槽上一轮的 `retired`。
3. 获取交换链图像，录制并提交新命令。
4. 仅在 `vkQueueSubmit` 成功后，用无分配、无异常的 swap 把 `pendingRetired_` 转交给该槽。
5. 下次复用该槽时，等待这次提交的 fence 后才能释放这一批对象。

不能在步骤 2 释放 `pendingRetired_`：其他帧槽可能仍使用其中的资源，当前槽的旧 fence
并不覆盖这些提交。获取图像失败、录制异常或提交失败时，待退休批次继续保留；提交成功后
即使 presentation 要求 resize，批次也已经正确关联到提交。提交失败仍按现有错误路径退出，
不能等待已 reset 却没有成功提交的 fence 来恢复渲染。

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

独立使用 `VulkanResourcePreparation` 的调用方也必须提供退休容器，并负责在 GPU 使用完成后
清理；容器、Cache 和 Device 的生命周期需要覆盖相应使用期。
