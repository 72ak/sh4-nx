# Copies the NRO and the SH4 data folders to /switch/sh4-nx on the Switch, one after the other, logging to
# build/mtp-copy.log. Safe to rerun: files already on the card with the right size are skipped.
$root = Split-Path -Parent $PSScriptRoot
$log = Join-Path $root 'build\mtp-copy.log'
$cp = Join-Path $PSScriptRoot 'mtp_copy.ps1'
"start $(Get-Date -Format 'HH:mm:ss')" | Out-File -FilePath $log -Encoding utf8
$jobs = @(
    @{ src = (Join-Path $root 'build\switch\sh4-nx.nro') },
    @{ src = 'A:\Games\Silent Hill 4\sound' },
    @{ src = 'A:\Games\Silent Hill 4\data' },
    @{ src = 'A:\Games\Silent Hill 4\demo' }
)
foreach ($j in $jobs) {
    "== $($j.src) $(Get-Date -Format 'HH:mm:ss')" | Out-File -FilePath $log -Append -Encoding utf8
    & powershell -NoProfile -ExecutionPolicy Bypass -File $cp -Source $j.src -Dest 'SD Card/switch/sh4-nx' 2>&1 |
        Out-File -FilePath $log -Append -Encoding utf8
}
"finished $(Get-Date -Format 'HH:mm:ss')" | Out-File -FilePath $log -Append -Encoding utf8
