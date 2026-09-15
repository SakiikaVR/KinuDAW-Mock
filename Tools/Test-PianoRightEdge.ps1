# Copyright (c) 2026 KinuDAW contributors. MIT License.
# Run against --ui-test at 100% density and the default piano scroll position.
param([int]$OwnerId,[int]$PianoId)
$ErrorActionPreference='Stop'
$mapping=[IO.MemoryMappedFiles.MemoryMappedFile]::OpenExisting("Local\KinuPiano-v2-$OwnerId")
$view=$mapping.CreateViewAccessor()
try {
    foreach($test in @(@{y=397;pitch=60},@{y=374;pitch=62},@{y=383;pitch=60})) {
        & "$PSScriptRoot\Test-WindowInput.ps1" -ProcessId $PianoId -Action hold -X 66 -Y $test.y
        Start-Sleep -Milliseconds 150
        $actual=$view.ReadInt32(8)
        if($actual -ne $test.pitch) { throw "Right edge at y=$($test.y): expected pitch $($test.pitch), received $actual" }
        & "$PSScriptRoot\Test-WindowInput.ps1" -ProcessId $PianoId -Action release -X 66 -Y $test.y
        Start-Sleep -Milliseconds 100
        if($view.ReadInt32(8) -ne -1) { throw 'Audition did not release after mouse up and further movement' }
    }
    Write-Output 'Piano right edge: natural key, upper/lower accidental neighbours and Note Off passed'
} finally { $view.Dispose(); $mapping.Dispose() }
