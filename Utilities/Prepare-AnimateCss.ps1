param(
    [Parameter(Mandatory = $true)][string] $InputPath,
    [Parameter(Mandatory = $true)][string] $OutputPath
)

$ErrorActionPreference = 'Stop'

function Remove-AtRuleBlocks {
    param([string] $Text, [string] $Marker)

    while ($true) {
        $start = $Text.IndexOf($Marker, [StringComparison]::Ordinal)
        if ($start -lt 0) { return $Text }
        $opening = $Text.IndexOf('{', $start)
        if ($opening -lt 0) { return $Text.Substring(0, $start) }

        $depth = 1
        $index = $opening + 1
        while ($index -lt $Text.Length -and $depth -gt 0) {
            if ($Text[$index] -eq '{') { $depth++ }
            elseif ($Text[$index] -eq '}') { $depth-- }
            $index++
        }
        $Text = $Text.Substring(0, $start) + $Text.Substring($index)
    }
}

$css = [IO.File]::ReadAllText((Resolve-Path -LiteralPath $InputPath))
$css = [Regex]::Replace($css, '(?m)^\s*@charset[^;]+;\s*', '')
$css = Remove-AtRuleBlocks $css '@-webkit-keyframes'
$css = Remove-AtRuleBlocks $css '@media print'

$keptLines = foreach ($line in ($css -split "`r?`n")) {
    $declaration = $line.Trim()
    if ($declaration.StartsWith('-webkit-')) { continue }
    if ($declaration.StartsWith('transition-duration:') -or
        $declaration.StartsWith('transition-timing-function:') -or
        $declaration.StartsWith('backface-visibility:')) { continue }
    $line.Replace(' !important', '')
}

$css = ($keptLines -join "`n").Trim()
$css = [Regex]::Replace($css, '(?m)^:root\s*\{', 'body {')
$header = @'
/* Generated for KinuUI by Utilities/Prepare-AnimateCss.ps1.
 * All 97 Animate.css 4.1.1 named keyframes are preserved.
 * The upstream copyright and Hippocratic License notice follows. */
'@
$result = $header + "`n" + $css + "`n"
$encoding = New-Object Text.UTF8Encoding($false)
[IO.File]::WriteAllText([IO.Path]::GetFullPath($OutputPath), $result, $encoding)
