# Run one Ripes CLI simulation and report its peak host memory and telemetry.
# Usage: .\measure.ps1 -Src store.s -Proc RV32_ISS
param(
    [Parameter(Mandatory)] [string] $Src,
    [string] $Proc = 'RV32_ISS',
    [string] $Ripes = 'C:\tools\Ripes\Ripes.exe'
)

$src = (Resolve-Path $Src).Path
$out = [IO.Path]::GetTempFileName()
$p = Start-Process -FilePath $Ripes -PassThru -NoNewWindow `
    -ArgumentList '--mode', 'cli', '--src', "`"$src`"", '-t', 'asm', '--proc', $Proc, '--iret', '--exectime' `
    -RedirectStandardOutput $out

$peak = 0
while (-not $p.HasExited) {
    try { $p.Refresh(); if ($p.PeakWorkingSet64 -gt $peak) { $peak = $p.PeakWorkingSet64 } } catch {}
    Start-Sleep -Milliseconds 50
}

Get-Content $out
'===== peak working set (bytes)'
$peak
Remove-Item $out
