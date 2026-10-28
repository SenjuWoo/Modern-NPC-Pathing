[CmdletBinding()]
param(
    [string]$Preset = "",
    [string]$CommonLibTag = "v14.1.0"
)

$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

function Resolve-Preset {
    if ($Preset) { return $Preset }
    $help = (& cmake --help | Out-String)
    if ($help -match "Visual Studio 18 2026") { return "windows-msvc" }
    if ($help -match "Visual Studio 17 2022") { return "windows-msvc-2022" }
    throw "Visual Studio 2022 or 2026 C++ build tools were not found."
}

$vcpkgRoot = $env:VCPKG_ROOT
if (-not $vcpkgRoot) { $vcpkgRoot = $env:VCPKG_INSTALLATION_ROOT }
if (-not $vcpkgRoot -or -not (Test-Path "$vcpkgRoot/scripts/buildsystems/vcpkg.cmake")) {
    throw "VCPKG_ROOT or VCPKG_INSTALLATION_ROOT must point to a working vcpkg checkout."
}
$env:VCPKG_ROOT = $vcpkgRoot

$cmakePreset = Resolve-Preset
Write-Host "Using preset $cmakePreset"

python generate_esp.py
python -m unittest discover -s tests -v
if ($LASTEXITCODE -ne 0) { throw "release tests failed" }

$commonLib = Join-Path $PSScriptRoot "extern/CommonLibSSE"
if (-not (Test-Path (Join-Path $commonLib "CMakeLists.txt"))) {
    git clone https://github.com/alandtse/CommonLibSSE-NG.git $commonLib
}
if (-not (Test-Path (Join-Path $commonLib ".git"))) {
    throw "extern/CommonLibSSE exists but is not a Git checkout. Remove it and rerun."
}
git -C $commonLib fetch --quiet --tags origin
git -C $commonLib checkout --quiet $CommonLibTag
git -C $commonLib submodule update --init --depth 1

cmake --preset $cmakePreset
if ($LASTEXITCODE -ne 0) { throw "configure failed" }
cmake --build build --config RelWithDebInfo --target NPCPathingNG StepUpOntoSKSE --parallel
if ($LASTEXITCODE -ne 0) { throw "build failed" }

cmd /c "tools\run_unit_tests.bat"
if ($LASTEXITCODE -ne 0) { throw "arc unit tests failed" }
python -m unittest discover -s tests -v
if ($LASTEXITCODE -ne 0) { throw "release tests failed" }

python tools/package_fomod.py
if ($LASTEXITCODE -ne 0) { throw "FOMOD packaging failed" }
Write-Host "Release created in dist/."
