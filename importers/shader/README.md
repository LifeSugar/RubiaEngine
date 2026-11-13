# Unified Slang shader path

All source shaders (`.slang` and compatible `.hlsl`) use Slang. Built-in PBR,
presentation shaders and editor imports follow the same path:

```
source -> slangc -> target artifact (backend, format, profile, bytes)
       -> Slang SDK target layout -> ShaderInterface -> MaterialTemplateBuilder
```

SPIR-V is still the Vulkan backend's bytecode format. It is not the source
reflection provider. No Slang SDK types escape the importer.

`compile(source, stage, entryPoint, ShaderTarget{backend, profile})` selects both
CLI code generation and SDK reflection through the same target configuration.
An empty profile selects the following defaults:

| Backend | Artifact format | Default profile |
| --- | --- | --- |
| Vulkan | SPIR-V binary | `spirv_1_3` |
| Direct3D12 | DXIL binary | `sm_6_0` |
| Metal | Metal source | `metallib_2_3` |
| OpenGL | GLSL source | `glsl_460` |

DXIL generation requires DXC available to the Slang installation. Metal source
and GLSL emission do not require a native graphics device. Native Metal library
compilation and D3D12/Metal/OpenGL rendering adapters are not part of this change.
The existing scene binding/ABI restrictions below still apply; target codegen
support does not imply every shader feature or binding layout has a renderer.

`ShaderAsset` and RHI `ShaderCode` own a common `ShaderArtifact` from the header-only
`Rubia::ShaderTypes` target. Generic layers treat bytes as opaque. Vulkan validates
the backend, format, SPIR-V byte size and magic before creating a shader module,
copying bytes into aligned words only at that boundary. Program and pipeline cache
keys include backend, format, profile and payload; programs reject mixed targets.

The active application backend supplies the compile target to built-in content
and GUI import requests. It is copied into import workers along with the request.

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
Both paths use the selected target/profile and column-major source matrices.
Vulkan-only entry-name and direct-SPIR-V flags are applied only to the Vulkan target.

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
for those callers. The Assets browser labels these files `Legacy SPIR-V` and
rejects them for non-Vulkan targets; binary version metadata supplies their profile.
`compile_shaders.ps1` is a legacy binary export helper using Slang, not DXC.
`HlslShaderCompiler` is a compatibility spelling that delegates to Slang.

Validation: `shader-compiler-test`, `startup-smoke-test`,
`preparation-gallery-test`, `editor-assets-test`, and `render-boundary-test`.
GPU tests require `RUBIA_ENABLE_GPU_TESTS=ON`.

`shader-compiler-test` covers Vulkan, GLSL 4.60 and Metal source artifacts and
material-block reflection. Set `RUBIA_TEST_DXIL=1` to include DXIL when DXC is
installed. These are CPU compilation checks; GPU regression still runs Vulkan.
