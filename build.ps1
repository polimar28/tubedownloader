# Builds Tube Downloader with Qt 6 + MinGW (no Visual Studio) and creates a
# self-contained folder in dist\TubeDownloader.
#
# Usage:  powershell -ExecutionPolicy Bypass -File build.ps1 [-QtDir C:\Qt\6.8.3\mingw_64]
param(
    [string]$QtDir = "C:\Qt\6.8.3\mingw_64",
    [string]$ToolsRoot = "C:\Qt\Tools"
)
$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$build = Join-Path $root "build"
$dist = Join-Path $root "dist\TubeDownloader"

# Toolchain on PATH for this session only.
$env:PATH = "$QtDir\bin;$ToolsRoot\mingw1310_64\bin;$ToolsRoot\CMake_64\bin;$ToolsRoot\Ninja;$env:PATH"

foreach ($t in "yt-dlp.exe", "ffmpeg.exe") {
    if (-not (Test-Path (Join-Path $root "tools\$t"))) { throw "Missing tools\$t" }
}

cmake -S $root -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=$QtDir"
if ($LASTEXITCODE) { throw "CMake configure failed" }
cmake --build $build --parallel
if ($LASTEXITCODE) { throw "Build failed" }

# Assemble the distributable folder.
if (Test-Path $dist) { Remove-Item -Recurse -Force $dist }
New-Item -ItemType Directory -Force $dist | Out-Null
Copy-Item (Join-Path $build "TubeDownloader.exe") $dist
windeployqt --release --no-translations --no-system-d3d-compiler --no-opengl-sw --no-network `
    --skip-plugin-types imageformats,iconengines,generic,networkinformation,tls `
    (Join-Path $dist "TubeDownloader.exe")
if ($LASTEXITCODE) { throw "windeployqt failed" }
Copy-Item -Recurse (Join-Path $root "tools") (Join-Path $dist "tools")

Write-Host "`nDone: $dist\TubeDownloader.exe" -ForegroundColor Green
