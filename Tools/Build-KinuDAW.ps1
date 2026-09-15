# Copyright (c) 2026 KinuDAW contributors. MIT License.
param([string]$BuildDirectory = "$PSScriptRoot\..\build-daw", [string]$Generator = 'Visual Studio 17 2022')
$ErrorActionPreference='Stop'
$root=(Resolve-Path -LiteralPath "$PSScriptRoot\..").Path
$cmake=Get-Command cmake -ErrorAction SilentlyContinue
if($cmake) { $cmake=$cmake.Source }
else {
    $vswhere="${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if(-not (Test-Path -LiteralPath $vswhere)) { throw 'Install Visual Studio C++ desktop development and CMake.' }
    $installation=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    $cmake=Join-Path $installation 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
}
& $cmake -S $root -B $BuildDirectory -G $Generator -A x64 -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DRMLUI_SAMPLES=ON -DRMLUI_BACKEND=Win32_DX11 -DRMLUI_TRACY_PROFILING=OFF -DMI_DEBUG=OFF -DMI_DEBUG_INTERNAL=OFF
if($LASTEXITCODE) { throw 'CMake configuration failed. Initialize recursive git submodules.' }
& $cmake --build $BuildDirectory --config Release --target rmlui_sample_daw_timeline kinu_audio_tests -- /clp:ErrorsOnly
if($LASTEXITCODE) { throw 'Build failed' }
& (Join-Path $BuildDirectory 'Release\kinu_audio_tests.exe')
if($LASTEXITCODE) { throw 'Audio tests failed' }
