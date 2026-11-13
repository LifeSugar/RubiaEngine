# Legacy SPIR-V export helper. Runtime source imports use Slang + SDK reflection.
$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$slang = Get-Command slangc -ErrorAction SilentlyContinue
if (-not $slang) {
    throw "slangc not found. Open a terminal with Vulkan SDK or add slangc to PATH."
}

$shaders = @(
    @{ Source = "triangle.vert.hlsl"; Target = "vertex" },
    @{ Source = "triangle.frag.hlsl"; Target = "fragment" },
    @{ Source = "present.vert.hlsl";  Target = "vertex" },
    @{ Source = "present.frag.hlsl";  Target = "fragment" }
)

foreach ($shader in $shaders) {
    $source = Join-Path $scriptDir $shader.Source
    $output = [System.IO.Path]::ChangeExtension($source, ".spv")
    & $slang.Source $source -target spirv -profile spirv_1_3 -stage $shader.Target -entry main -matrix-layout-column-major -fvk-use-entrypoint-name -o $output
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to compile $($shader.Source)."
    }
    Write-Host "[OK] $($shader.Source) -> $(Split-Path -Leaf $output)"
}
