# Copyright (c) 2026 KinuDAW contributors. MIT License.
param([string]$Path="$PSScriptRoot\..\build-daw\Release\ui-tone.wav")
$stream=[System.IO.File]::Create($Path)
$writer=[System.IO.BinaryWriter]::new($stream)
$count=96000; $bytes=$count*4
$writer.Write([System.Text.Encoding]::ASCII.GetBytes('RIFF')); $writer.Write([uint32]($bytes+36))
$writer.Write([System.Text.Encoding]::ASCII.GetBytes('WAVEfmt ')); $writer.Write([uint32]16)
$writer.Write([uint16]1); $writer.Write([uint16]2); $writer.Write([uint32]48000); $writer.Write([uint32]192000)
$writer.Write([uint16]4); $writer.Write([uint16]16); $writer.Write([System.Text.Encoding]::ASCII.GetBytes('data')); $writer.Write([uint32]$bytes)
for($i=0;$i -lt $count;$i++) { $value=[int16](3000*[Math]::Sin($i*2*[Math]::PI*220/48000)); $writer.Write($value); $writer.Write($value) }
$writer.Dispose()
