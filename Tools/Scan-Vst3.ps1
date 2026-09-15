# Copyright (c) 2026 KinuDAW contributors. MIT License.
param([string]$Bin = "$PSScriptRoot\..\build-daw\Release", [int]$TimeoutSeconds = 20)
$ErrorActionPreference = 'Stop'
$worker = Join-Path (Resolve-Path -LiteralPath $Bin) 'kinu_vst_worker.exe'
$paths = (& $worker --paths | ConvertFrom-Json)
$plugins = [System.Collections.Generic.List[object]]::new()
$failures = [System.Collections.Generic.List[object]]::new()
$results = Join-Path $Bin 'vst-scan-results'
New-Item -ItemType Directory -Force -Path $results | Out-Null
$index = 0
foreach ($module in $paths) {
    $output = Join-Path $results ("scan-{0}.json" -f $index++)
    if (Test-Path -LiteralPath $output) { Remove-Item -LiteralPath $output }
    $stderr = "$output.stderr.txt"
    $argsText = '--scan "{0}" "{1}"' -f $module, $output
    $process = Start-Process -FilePath $worker -ArgumentList $argsText -PassThru -WindowStyle Hidden -RedirectStandardError $stderr
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        $process.Kill()
        $failures.Add([pscustomobject]@{path=$module; error='Scan timed out; worker terminated'})
    } elseif (Test-Path -LiteralPath $output) {
        $result = Get-Content -LiteralPath $output -Raw -Encoding UTF8 | ConvertFrom-Json
        foreach ($plugin in $result.plugins) { $plugins.Add($plugin) }
        if ($result.error) { $failures.Add([pscustomobject]@{path=$module; error=$result.error}) }
    } else {
        $message = Get-Content -LiteralPath $stderr -Raw -ErrorAction SilentlyContinue
        $failures.Add([pscustomobject]@{path=$module; error="Worker exited $($process.ExitCode): $message"})
    }
    Write-Output ("{0}/{1}: {2}" -f $index, $paths.Count, $module)
}
$catalog = [pscustomobject]@{version=1; scannedAt=(Get-Date).ToString('o'); plugins=@($plugins.ToArray()); failures=@($failures.ToArray())}
$catalog | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $Bin 'plugins.json.tmp') -Encoding UTF8
Move-Item -LiteralPath (Join-Path $Bin 'plugins.json.tmp') -Destination (Join-Path $Bin 'plugins.json') -Force
Write-Output ("Catalog: {0} classes, {1} failures" -f $plugins.Count, $failures.Count)
