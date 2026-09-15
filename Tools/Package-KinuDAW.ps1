# Copyright (c) 2026 KinuDAW contributors. MIT License.
param([string]$Bin="$PSScriptRoot\..\build-daw\Release",[string]$Version='0.3.2')
$ErrorActionPreference='Stop'
if($Version -notmatch '^\d+\.\d+\.\d+([-.][a-zA-Z0-9.-]+)?$') { throw 'Invalid package version' }
$root=(Resolve-Path -LiteralPath "$PSScriptRoot\..").Path
$binPath=(Resolve-Path -LiteralPath $Bin).Path
$distribution=Join-Path $root 'Distribution'
New-Item -ItemType Directory -Path $distribution -Force | Out-Null
$staging=Join-Path $root ('build-daw\package-'+[Guid]::NewGuid().ToString('N'))
$package=Join-Path $staging 'KinuDAW'
New-Item -ItemType Directory -Path "$package\Samples\basic\daw_timeline", "$package\licenses", "$package\Tools" -Force | Out-Null
Copy-Item -LiteralPath "$binPath\rmlui_sample_daw_timeline.exe" -Destination "$package\KinuDAW.exe"
Copy-Item -LiteralPath "$binPath\kinu_vst_worker.exe" -Destination $package
Copy-Item -LiteralPath "$root\Samples\basic\daw_timeline\data" -Destination "$package\Samples\basic\daw_timeline" -Recurse
Copy-Item -LiteralPath "$root\Samples\assets" -Destination "$package\Samples" -Recurse
foreach($script in @('Scan-Vst3.ps1','Probe-Vst3.ps1')) { Copy-Item -LiteralPath "$root\Tools\$script" -Destination "$package\Tools" }
foreach($document in @('README.md','LICENSE.txt','THIRD_PARTY_NOTICES.md')) { Copy-Item -LiteralPath "$root\$document" -Destination $package }
Copy-Item -LiteralPath "$root\Dependencies\audio\LICENSE.miniaudio" -Destination "$package\licenses\miniaudio.txt"
Copy-Item -LiteralPath "$root\Dependencies\audio\LICENSE.json" -Destination "$package\licenses\nlohmann-json.txt"
Copy-Item -LiteralPath "$root\Dependencies\vst3sdk\LICENSE.txt" -Destination "$package\licenses\vst3sdk.txt"
Copy-Item -LiteralPath "$root\Dependencies\freetype\docs\FTL.TXT" -Destination "$package\licenses\freetype.txt"
Copy-Item -LiteralPath "$root\Dependencies\freetype\LICENSE.TXT" -Destination "$package\licenses\freetype-components.txt"
Copy-Item -LiteralPath "$root\Dependencies\freetype\src\bdf\README" -Destination "$package\licenses\freetype-bdf.txt"
Copy-Item -LiteralPath "$root\Dependencies\freetype\src\pcf\README" -Destination "$package\licenses\freetype-pcf.txt"
foreach($component in @('src\gzip\zlib.h','src\base\fthash.c','src\autofit\ft-hb-types.h')) {
    $source=[IO.File]::ReadAllText((Join-Path "$root\Dependencies\freetype" $component))
    $notice=[regex]::Match($source,'(?s)^/\*.*?\*/').Value
    if(-not $notice) { throw "Missing component license: $component" }
    [IO.File]::WriteAllText((Join-Path "$package\licenses" ('freetype-'+[IO.Path]::GetFileName($component)+'.txt')),$notice,[Text.UTF8Encoding]::new($false))
}
Copy-Item -LiteralPath "$root\Dependencies\mimalloc\LICENSE" -Destination "$package\licenses\mimalloc.txt"
Copy-Item -LiteralPath "$root\Samples\basic\daw_timeline\data\fonts\OFL.txt" -Destination "$package\licenses\LINESeedJP.txt"
Copy-Item -LiteralPath "$root\Samples\assets\LICENSE.txt" -Destination "$package\licenses\sample-fonts.txt"
$archive=Join-Path $distribution "KinuDAW-v$Version-windows-x64.zip"
Compress-Archive -LiteralPath $package -DestinationPath $archive -Force
Get-FileHash -LiteralPath $archive -Algorithm SHA256 | Format-List
Write-Output $archive
