# Copyright (c) 2026 KinuDAW contributors. MIT License.
param([string]$Bin = "$PSScriptRoot\..\build-daw\Release", [int]$TimeoutSeconds = 30)
$ErrorActionPreference = 'Stop'
$worker = Join-Path (Resolve-Path -LiteralPath $Bin) 'kinu_vst_worker.exe'
$catalog = Get-Content -LiteralPath (Join-Path $Bin 'plugins.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$results = Join-Path $Bin 'vst-probe-results'
New-Item -ItemType Directory -Path $results -Force | Out-Null
$report = [System.Collections.Generic.List[object]]::new()
$index = 0
foreach ($plugin in $catalog.plugins) {
    $config = Join-Path $results ("plugin-{0}.json" -f $index)
    $output = Join-Path $results ("result-{0}.json" -f $index++)
    if (Test-Path -LiteralPath $output) { Remove-Item -LiteralPath $output }
    $stderr = "$output.stderr.txt"
    $plugin | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $config -Encoding UTF8
    $process = Start-Process -FilePath $worker -ArgumentList ('--probe "{0}" "{1}"' -f $config,$output) -PassThru -WindowStyle Hidden -RedirectStandardError $stderr
    $status = 'failed'
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) { $process.Kill(); $status='timeout' }
    elseif (Test-Path -LiteralPath $output) {
        $verification = Get-Content -LiteralPath $output -Raw -Encoding UTF8 | ConvertFrom-Json
        if ($verification.processedBlocks -eq 512 -and $verification.restored) { $status='passed' }
    }
    $detail = if (Test-Path -LiteralPath $output) { Get-Content -LiteralPath $output -Raw -Encoding UTF8 | ConvertFrom-Json } else { Get-Content -LiteralPath $stderr -Raw -ErrorAction SilentlyContinue }
    $report.Add([pscustomobject]@{name=$plugin.name; path=$plugin.path; uid=$plugin.uid; status=$status; detail=$detail})
    Write-Output ("{0}/{1} {2}: {3}" -f $index,$catalog.plugins.Count,$plugin.name,$status)
}
$report.ToArray() | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $Bin 'compatibility.json') -Encoding UTF8
