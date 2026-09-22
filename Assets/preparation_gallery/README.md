# Rubia Material Atelier

BlenderMCP / Blender 4.5.9 LTS 制作的材质展厅：40 个测试几何体、10 种测试材质，另加一个合并的建筑网格和独立环境材质。
120 × 120 的大平面、深色背景墙、倒角展台、立体文字和条纹参照物都是实际 Mesh，没有增加 skybox pass。

[Rubia 全景](rubia-gallery.png) · [玻璃近景](rubia-gallery-glass-closeup.png) · [关闭玻璃的对照](rubia-gallery-without-glass.png) · [纹理更新结果](rubia-gallery-updated.png)

以上图片全部来自 Rubia 的 Vulkan 渲染读回，不是 Blender 渲染或合成效果图。

## 启动

在仓库根目录：

```powershell
./assets/preparation_gallery/run.ps1
```

先完成 `debug-vs` 构建，安装 Vulkan SDK 的 `dxc.exe`。脚本打开已构建的 Debug 编辑器，装配并验证展厅，然后进入正常主循环。
普通编辑器仍从空场景启动。点击 Hierarchy 的分组可以编辑实例位置和材质。

自动回归：

```powershell
ctest --test-dir build/debug-vs -C Debug -R preparation-gallery-test --output-on-failure
```

也可在 `build/debug-vs/apps/editor/Debug` 下运行 `./VulkanApp.exe --preparation-gallery-test`。
GPU 测试注册需要 `RUBIA_ENABLE_GPU_TESTS=ON`。

## 展台矩阵

后排从左到右为 00–04，前排为 05–09；每个展台一个主物体和三个小物体。参数偏移全部由 HLSL 反射生成。

| 编号 | 材质 | VS / PS | 输入、贴图与状态 |
|---|---|---|---|
| 00 | 暖色瓷器 | VSBasic / PSSolid | 仅位置输入，无贴图，不透明 |
| 01 | 顶点颜料 | VSColor / PSColor | 位置和顶点色，无贴图，不透明 |
| 02 | 玉色釉面 | VSFull / PSTextured | 釉面纹理，不透明 |
| 03 | 钴蓝平铺 | VSTiled / PSTextured | 与 02 同 PS、同纹理，不同 VS、UV 平铺 |
| 04 | 拉丝铜 | VSFull / PSDetail | 釉面、法线、ORM、发光共 4 对 image/sampler |
| 05 | 金色镂空 | VSFull / PSTextured | AlphaClip，阈值 0.3 |
| 06 | 玉色镂空 | VSFull / PSTextured | AlphaClip，阈值 0.7，与 05 共用 PSO |
| 07 | 青色玻璃 | VSFull / PSGlass | Alpha 混合，中心 Alpha 0.22，关闭深度写入 |
| 08 | 玫红玻璃 | VSFull / PSGlass | Alpha 混合，中心 Alpha 0.28，与 07 共用 PSO |
| 09 | 双面丝面 | VSFull / PSTextured | 禁用背面剔除，包含翻转平面，独立 PSO |
| 建筑 | 地面、墙、展台与参照物 | VSFull / PSStage | 顶点色，UV0.x 表示地面/文字/普通表面，无贴图 |

## 视觉改进与准确边界

- 将孤立物体的黑色背景改为浅色大平面和深色展墙，并增加展台和标签。
- 两组玻璃放在前景：内部有金色实体，后方有深色条纹，能清楚看到被玻璃染色的背景。
- 玻璃使用直通 Alpha 混合和随视角变化的 Fresnel 边缘透明度/高光；**没有折射、透射光追或屏幕空间反射**。
- 实体材质使用 GGX 高光、粗糙度、金属度、Schlick Fresnel、两方向的解析灯光和柔和环境填充。
- 灯箱反光是解析近似；展台与地面接触暗部也是固定展厅的解析近似，**不是完整 IBL 或阴影贴图**。
- Blender 程序化生成 512 × 512 的釉纹/镂空 Alpha、微细法线、ORM 和金色嵌线发光贴图。
  `checker.png` 保留旧文件名作为共享纹理，但内容已换成釉面纹样。颜色和 emission 用 sRGB，normal/ORM 用 Linear。

## Preparation 覆盖

2026-09-21，RTX 4070 / Debug 完整回归 9/9 通过，无 Vulkan validation error。

- 41 Mesh + 11 Material：52 Started、52 Shared、52 CacheHit；52 个额外订阅取消后，其余订阅仍完成。
- Shader、ShaderProgram、MaterialTemplate、Texture 通过依赖准备或独立纹理导入走同一资源准备链路。
- 4 VS + 6 PS → 7 Program → 11 Material → 9 唯一 PSO。其中建筑额外占 1 Material / 1 PSO。
- 33 个不透明/裁剪 draw（含建筑）、8 个透明 draw；验证阈值/颜色共享、不同 VS/剔除状态分离。
- 预热后的绘制和纹理替换均不新增 PSO；Material 参数默认 DeviceLocal + staging。
- 在持续渲染中替换共享纹理的 contentRevision，再恢复原内容。8 个材质更新描述符，UBO 和 PSO 保持复用。
- GPU 读回检查实际着色像素、更新纹理前后的画面差异，以及开启/关闭玻璃的画面差异。
- 额外保存低角度玻璃近景，用来观察背景条纹和内部实体。

本场景不是透明排序/OIT 的完整专项测试；透明物体相交、折射及每像素多层透明仍需单独设计。

顶点覆盖包括 POSITION、顶点色、NORMAL/UV/TANGENT 等源属性组合，以及不同 VS 输入签名。
导入后 GPU 存储依然是统一的 `asset::Vertex`，没有覆盖不同 stride、多顶点流、压缩顶点或蒙皮布局。

## 文件与产物

- `gallery.blend`：Blender 源文件。新展厅在独立 Scene，旧展台和默认场景保留。
- `gallery.glb`：当前展厅的标准 glTF 预览。
- `group_00.glb` … `group_09.glb`：每组 4 个测试几何体。
- `environment.glb`：背景平面、墙、展台、文字、玻璃参照物，合为一个顶点色网格。
- `gallery.hlsl`：10 个入口，引用 `../shaders/RenderData.hlsli`。
- `tests/PreparationGallery.cpp`：专用场景装配入口，不是通用场景序列化格式。

Blender 节点不会自动转换为任意 Rubia HLSL。拖入完整 GLB 得到标准 glTF 材质预览；
上述专用启动入口负责实例材质覆盖、自定义 VS/PS 和准备流程的测试。

每次运行在工作目录 `gallery-results/` 输出 `report.txt` 和以下 GPU 读回图：

- `gallery.ppm`：默认全景；
- `gallery-without-glass.ppm`：同相机关闭透明 draw 的对照；
- `gallery-glass-closeup.ppm`：玻璃近景；
- `gallery-updated.ppm`：共享纹理更新后的全景。

可用 Pillow 直接转换 PPM 到 PNG，像素内容不变。

## 重建 Blender 资产

通过 BlenderMCP 执行创建脚本，检查 scene 和 viewport 后执行导出脚本：

```python
path = r"F:/LearnVulkan/RubiaEngine/assets/preparation_gallery/create_blender_scene.py"
exec(compile(open(path, encoding="utf-8").read(), path, "exec"), {"__file__": path})
# 然后以同样方式执行 export_blender_scene.py。
```

创建脚本保留其他 Scene；导出脚本只导出当前活动 Scene 的选中对象，并覆盖本目录展厅输出。
建筑材质的表面标识使用 UV0.x，避免 glTF 导出时丢失未连接的顶点 Alpha。
