# 测试

- `unit/`：资产、着色器编译、渲染调度、资源准备和 GUI 接口检查。
- `integration/`：应用生命周期、后端接口、编辑器工作流及展厅回归。
- `vulkan/`：Vulkan 上传、管线、绘制列表和呈现检查。
- `shaders/`：Vulkan 管线测试专用 GLSL fixture，不属于正式应用的 Slang 资源路径。

正式程序 `VulkanApp` 仅提供编辑器、Runtime 和交互展厅入口。集成测试由
`RubiaIntegrationTests` 执行，不被正式应用链接。交互展厅与展厅测试共享
`apps/editor/gallery/GalleryContent` 的材质和场景构建，GPU 读回、断言和报告留在测试中。

## 构建与运行

```powershell
cmake --preset debug-vs -DBUILD_TESTING=ON -DRUBIA_ENABLE_GPU_TESTS=ON
cmake --build build/debug-vs --config Debug --parallel 6
ctest --test-dir build/debug-vs -C Debug --output-on-failure
```

`RUBIA_ENABLE_GPU_TESTS` 默认关闭；开启后注册隐藏窗口 Vulkan 回归，需要可用的
Vulkan 设备和 Debug 构建使用的 validation layer。测试名称与原有 CTest 入口保持一致。

直接运行集成测试时，以测试程序所在目录为工作目录，例如：

```powershell
Push-Location build/debug-vs/tests/Debug
./RubiaIntegrationTests.exe --preparation-gallery-test
Pop-Location
```

支持参数：`--asset-test`、`--startup-test`、`--render-test`、`--editor-test`、
`--editor-assets-test`、`--preparation-gallery-test`。资源和 Windows 运行库由构建自动复制。
展厅回归输出位于测试工作目录的 `gallery-results/`。

纯应用构建使用 `-DBUILD_TESTING=OFF`，不会创建测试库、测试可执行文件或 fixture 编译目标。
