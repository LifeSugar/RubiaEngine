$galleryRepo = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$galleryExecutable = Join-Path $galleryRepo 'build/debug-vs/apps/editor/Debug/VulkanApp.exe'
if (-not (Test-Path -LiteralPath $galleryExecutable)) {
    throw 'Build the debug-vs preset before opening the preparation gallery.'
}
# This is the interactive editor window requested by the caller.
Start-Process -FilePath $galleryExecutable -ArgumentList '--preparation-gallery' `
    -WorkingDirectory (Split-Path -Parent $galleryExecutable)
