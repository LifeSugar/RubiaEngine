# Shader Program 与材质模板

CPU 资产依赖顺序：`ShaderAsset → ShaderProgramAsset → MaterialTemplateAsset → MaterialAsset`。
`AssetManager` 拥有各类型注册表，资产间只保存带 generation 的句柄。

## 资产身份与内容版本

`AssetHandle<T>{index, generation}` 是同一个 AssetManager 内的稳定身份。
删除、reset 和槽位复用会使旧 handle 失效；替换内容不会改变 handle，材质与模型中的引用继续有效。

各类资产统一通过 AssetRegistry 维护 `AssetContentRevision`（uint64_t）：

- `0` 表示没有已发布内容；每个新资产从 `1` 开始。
- 成功 `replace` 后递增；验证失败、失效 handle 等拒绝不改变版本或原内容。
- 恢复旧内容、提交相同字节也算一次新发布，版本继续递增，不按内容哈希去重。
- 删除/reset 清除内容版本；复用槽位的新资产从 `1` 开始，通过新 generation 区分旧身份。
- 版本达到 uint64_t 上限后拒绝替换，不回绕。

```cpp
const auto captured = assets.version(textureHandle); // AssetVersion<TextureAsset>
const auto revision = assets.contentRevision(textureHandle);
// 在同一 AssetManager 中检查准备结果是否仍对应当前内容：
if (!assets.isCurrent(captured))
{
    // 丢弃过期结果，或者重新请求当前版本。
}
```

这些查询适用于 Texture、Mesh、Shader、ShaderProgram、MaterialTemplate、Material 和 Model。
`version()` / `contentRevision()` 对失效 handle 抛出 out_of_range；`isCurrent()` 返回 false。
AssetVersion 是 `{handle, contentRevision}` 元数据快照，不复制或持有内容，不保证线程安全。
准备期间仍须保证源数据的生命周期与不可变性；不能一边替换资产一边读取上传指针。
不同 AssetManager 可以产生相同数值的 handle/version，跨管理器的缓存键必须额外区分所属域。

版本只描述资产本身，不自动汇总依赖。更新 Texture 不递增引用它的 Material 的版本；
后续准备层需要分别记录并验证依赖版本。已有 shader 签名用于内容/接口兼容性判断，
contentRevision 用于识别内容发布次数，两者用途不同。
AssetManager 公开 Texture / Mesh 内容替换；Mesh 替换同样校验材质所属管理器。
其他类型的替换需要各自的依赖校验后再开放。

`AssetSnapshot<T>` 保存 domain、AssetVersion、不可变 CPU 数据和依赖快照。七类 Asset 均可通过
`assets.snapshot(handle)` 获取。一次捕获中每个资源只复制一次；同一个快照可重复提交，避免反复复制。
快照不受 registry 扩容、replace 或 reset 影响；它不提供 AssetManager 的并发读写同步。
domain 是保活的身份 token，管理器移动后保留，移动后的源对象再次使用时生成新 token。
不同管理器中数值相同的 handle/revision 不属于同一资产。

Vulkan 独立准备接口现在接收快照，支持同版本 GPU 缓存命中、准备任务共享和新版本替换。
快照与版本匹配不等于自动热重载；需要调用方明确提交新版本，并在 Ready 后启用相应业务状态。

## 创建

```cpp
const auto program = assets.createShaderProgram({
    "PBR", {vertexShader, fragmentShader}
});

asset::MaterialTemplateAsset::CreateInfo info;
info.name = "PBR Material";
info.program = program;
info.textureSlots = {
    {"baseColorTexture", "baseColorTexture", "baseColorSampler"}
};
const auto materialTemplate = assets.createMaterialTemplate(info);
```

示例中的纹理元数据依次是材质槽名称、shader image 名称、shader sampler 名称。
其余反射出的 image/sampler 也须提供配对；不存在的名称和重复绑定都会报错。
纹理槽索引按元数据顺序生成，保持导入器的语义顺序。

模板的 `CreateInfo` 不接受参数类型、offset、buffer 大小或 binding 数字。
`MaterialTemplateBuilder` 从 Program 的 `materialSet`（默认 1）生成这些字段，
保留反射的完整 buffer 大小，包括 padding。默认所有参数必填，
`info.parameters = {{"roughnessFactor", false}}` 可将指定参数改为可选。
未赋值的可选参数初始化为零。纹理用途、默认贴图和颜色空间仍由导入逻辑提供。

## 反射、合并与校验

- importer 中的 SPIRV-Cross 为单阶段生成 `ShaderInterface`，不创建 GPU 对象。
- `ShaderProgramBuilder` 仅依赖 CPU 资产，校验 VS/PS 阶段组合和阶段间输入输出。
- 资源按 `(set, binding)` 合并，保留阶段 mask 和资源名称别名。
- 同一绑定要求资源类型、数组数量和 buffer 物理布局一致；共享 buffer 成员名称也必须一致。
- Buffer 物理签名包含嵌套结构、数组/矩阵步长。Push constant 保留 offset、大小和阶段。
- 材质模板只提取材质 set；相机和对象等其他 set 的资源保留在 Program 中。

当前支持 VS + PS 图形 Program。材质布局支持一个 UBO、标量/向量、列主序且
步长为 16 的 float4x4，以及一对一的独立 image/sampler 槽。材质参数数组、
嵌套结构、描述符数组、共享 sampler 和多个材质 UBO 会被明确拒绝。
Program 可以描述其他 set 中的 storage buffer，但尚未提供 Compute Program。

Vulkan 默认 pipeline 工厂消费准备完成的 GpuShaderProgram，复用阶段模块；材质描述符布局使用生成的
绑定和阶段 mask。顶点 buffer 布局、帧数据接口和 draw push constant 的录制约定
仍由当前 renderer 提供。材质 GPU 后端目前固定使用 set 1。

## 签名与生命周期

- `codeSignature`：各阶段字节码与入口点。
- `layoutSignature`：描述符布局与 push constant 范围，供后端布局复用判断。
- `interfaceSignature`：布局、资源名称、buffer 结构、阶段输入输出。
- 模板 `schemaSignature`：生成后的材质参数、纹理槽和必填规则。

Program 和 Shader 经 AssetManager 发布后只读。新 shader 内容须创建新 Shader 和
Program，构建失败不会修改旧资产。当前没有自动热重载或旧材质数据迁移；模板更换
后需要重新创建材质，按参数名称赋值。`reset()` 按依赖逆序释放并使旧句柄失效。

## 验证

`asset-revision-test` 覆盖稳定身份、内容替换与恢复、失败不变、删除/reset/槽位复用以及发布失败；
`shader-program-test` 同时覆盖程序与材质类的版本查询，并覆盖接口合并、冲突、签名、失效句柄、反射布局变更后的
材质打包和纹理映射。`asset-smoke-test` 使用真实 PBR SPIR-V 验证完整资产导入；
Runtime/Editor smoke tests 验证 GPU 消费路径。


## 全类型准备快照

`AssetManager::snapshot(handle)` 覆盖 Texture、Mesh、Shader、ShaderProgram、
MaterialTemplate、Material、Model。快照包含 domain、根版本、不可变数据及直接依赖快照；
一次捕获内重复依赖共享数据，整个依赖图独立于 AssetManager 后续替换、扩容与销毁。
例如 Model 快照包含 Mesh → Material → Template → Program → Shader，以及 Material 的 Texture。
准备层比较完整依赖版本，因此只修改 Texture 也会使父 Material/Model 的准备结果需要刷新。
快照捕获会复制数据，宜保存并复用；调用时仍需遵守 AssetManager 的单线程访问约束。
