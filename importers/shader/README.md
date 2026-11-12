# Shader compiler dependencies

With `RUBIA_BUILD_IMPORTERS=ON`, CMake requires a runnable Slang compiler
(`slangc`) version **2026.1 or newer**. This is a host tool: Engine, Renderer
and RHI do not link against Slang. Builds with importers disabled do not need it.

Download a binary distribution for the **host** OS/architecture from the
[official Slang releases](https://github.com/shader-slang/slang/releases).
Keep the distribution's runtime libraries and supporting files together;
copying only `slangc.exe` is insufficient. Pin the distribution version in your
development/CI environment instead of mixing files from different releases.

Configure an independent installation with:

```sh
cmake --preset debug-vs -DSlangCompiler_ROOT="C:/SDKs/slang"
```

Use `debug` instead of `debug-vs` with the Ninja preset. The root can also be
provided through the `SlangCompiler_ROOT` environment variable. Alternatively,
set `SlangCompiler_EXECUTABLE` to the absolute path of the host compiler.
An explicit root limits discovery to that installation. Without one, discovery
checks the Vulkan SDK (`VULKAN_SDK`) and normal CMake/PATH search locations;
Slang itself does not require the Vulkan SDK.

CMake caches `SlangCompiler_EXECUTABLE`. When switching installations, clear
the cached path, for example:

```sh
cmake --preset debug-vs -USlangCompiler_EXECUTABLE -DSlangCompiler_ROOT="C:/SDKs/slang"
```

The dependency exposes the imported executable target `Slang::slangc` and
the detected `SlangCompiler_VERSION`. Future shader build commands can use
`$<TARGET_FILE:Slang::slangc>`. Run the compiler from its installation so it
loads the matching runtime libraries. No compiler DLLs are copied to the app.
CMake checks `slangc -version` and rejects missing, unrunnable or older tools.

This step only establishes the external dependency. `.hlsl` imports still use
DXC and `.spv` imports still use the existing SPIRV-Cross reflection path;
`.slang` imports and the Slang SDK API are not wired into the application yet.
Local verification used Vulkan SDK 1.4.341.1's Slang
`2026.1-52-gc8ddf20bb` on Windows x64.
