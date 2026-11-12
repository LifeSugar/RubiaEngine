# Unified Slang shader path

All source shaders (`.slang` and compatible `.hlsl`) use Slang. Built-in PBR,
presentation shaders and editor imports follow the same path:

```
source -> slangc -> target code (currently SPIR-V 1.3)
       -> Slang SDK target layout -> ShaderInterface -> MaterialTemplateBuilder
```

SPIR-V is still the Vulkan backend's bytecode format. It is not the source
reflection provider. DXIL/Metal code generation and backend layouts are not
implemented yet. No Slang SDK types escape the importer.

## Dependency and execution

Install Slang 2026.1 or newer, including its SDK headers, import/shared library,
compiler executable and matching runtime files. Configure with
`-DSlangCompiler_ROOT=<distribution-root>` (or that environment variable).
Without an explicit root, discovery also checks `VULKAN_SDK` and PATH.
Clear `SlangCompiler_EXECUTABLE` from the CMake cache when switching roots.
Libraries/headers are selected from the compiler's distribution, not another
installation. Windows app/test builds copy `slang-compiler.dll` beside the exe.
Keep the configured compiler installation available at runtime.

Code generation remains in a shell-free slangc process with a 60-second timeout.
Target layout and used-resource metadata come from the SDK. The SDK stage is
serialized around a shared global session; each import has a fresh local session.
It has no subprocess timeout. This split avoids an observed SDK 2026.1 codegen
failure for a resource-free presentation vertex shader. That shader needs no
resource-use metadata; its interface is obtained directly from the SDK layout.
Both paths select SPIR-V 1.3, column-major source matrices and preserved entry names.

## Editor usage

Import `assets/shaders/editor_unlit.slang` twice: Vertex / `VSMain`, then Pixel /
`PSMain`. Build the program, pair `colorTexture` with `colorSampler`, set `tint`
and create/assign a material. Source reimports always recompile includes/modules.
Errors leave the current scene/material intact. Shader hot replacement is not
implemented. The compiler API also accepts compute, but the material UI is VS/PS.

The existing scene ABI and template restrictions still apply: frame/material
binding groups, one material UBO, scalar/vector/float4x4 fields and separate
texture/sampler bindings. Unsupported parameter blocks, descriptor arrays,
nested material structures and resource kinds are rejected. Layouts are queried
for the actual target, not inferred from SPIR-V matrix decorations.

## Legacy SPIR-V compatibility

`SpirvShaderImporter` and `SpirvReflection` are **legacy**. They only service
explicit precompiled `.spv` imports and legacy GPU fixtures; SPIRV-Cross remains
for those callers. The Assets browser labels these files `Legacy SPIR-V`.
`compile_shaders.ps1` is a legacy binary export helper using Slang, not DXC.
`HlslShaderCompiler` is a compatibility spelling that delegates to Slang.

Validation: `shader-compiler-test`, `startup-smoke-test`,
`preparation-gallery-test`, `editor-assets-test`, and `render-boundary-test`.
GPU tests require `RUBIA_ENABLE_GPU_TESTS=ON`.
