# Copies a file or a folder to the Switch's SD card over MTP (the "Nintendo Switch" portable device Windows shows
# while DBI's or hekate's MTP responder runs), through the Shell COM API: the only way to write a WPD device
# without a drive letter. Files go one at a time, each waited for, with the size checked afterwards; files already
# on the device with the right size are skipped, so a run can be repeated after an interruption. The responder
# sometimes answers an enumeration with nothing while it is busy, so every lookup is retried.
#
#   mtp_copy.ps1 -List "SD Card/switch"
#   mtp_copy.ps1 -Source "A:\Games\sh4-nx\build\switch\sh4-nx.nro" -Dest "SD Card/switch/sh4-nx"
#   mtp_copy.ps1 -Source "A:\Games\Silent Hill 4\data" -Dest "SD Card/switch/sh4-nx"   # -> sh4-nx/data/
param(
    [string]$Device = 'Nintendo Switch',
    [string]$List,
    [string]$Source,
    [string]$Dest,
    [int]$FileTimeoutMin = 30
)
$ErrorActionPreference = 'Stop'
$shell = New-Object -ComObject Shell.Application

function Get-MtpFolder([string]$path) {
    for ($try = 0; $try -lt 12; $try++) {
        try {
            $dev = $shell.NameSpace(17).Items() | Where-Object { $_.Name -eq $Device }
            if (-not $dev) { throw "device '$Device' not found (is the Switch in MTP mode?)" }
            $folder = $dev.GetFolder
            foreach ($part in ($path -split '[/\\]' | Where-Object { $_ })) {
                $item = $folder.Items() | Where-Object { $_.Name -eq $part }
                if (-not $item) { throw "'$part' not found in '$path'" }
                $folder = $item.GetFolder
            }
            return $folder
        } catch {
            if ($try -eq 11) { throw }
            Start-Sleep -Seconds 5
        }
    }
}

function Get-RemoteSize($folder, [string]$name) {
    # -1 when missing. System.Size is the exact byte count; GetDetailsOf would give "1.23 MB".
    $item = $folder.Items() | Where-Object { $_.Name -eq $name }
    if (-not $item) { return -1 }
    if ($item.IsFolder) { return -2 }
    $s = $item.ExtendedProperty('System.Size')
    if ($null -eq $s) { return 0 }
    return [int64]$s
}

if ($List) {
    $f = Get-MtpFolder $List
    foreach ($i in $f.Items()) {
        if ($i.IsFolder) { "{0}/" -f $i.Name } else { "{0}  {1}" -f $i.Name, $i.ExtendedProperty('System.Size') }
    }
    exit 0
}

if (-not $Source -or -not $Dest) { throw 'give -List <path> or -Source <local> -Dest <mtp folder>' }
$src = Get-Item -LiteralPath $Source

function Copy-OneFile([System.IO.FileInfo]$file, [string]$destPath) {
    $dst = Get-MtpFolder $destPath
    $have = Get-RemoteSize $dst $file.Name
    if ($have -eq $file.Length) { return 'skip' }
    $item = $shell.NameSpace($file.DirectoryName).ParseName($file.Name)
    if (-not $item) { throw "cannot open $($file.FullName)" }
    # 4 no progress UI, 16 yes to all (overwrite), 512 no directory confirm, 1024 no UI on error
    $dst.CopyHere($item, 4 + 16 + 512 + 1024)
    # Enumerating the device while a big file is in flight stalls the transfer (the responder answers one
    # request at a time), so the bigger the file, the rarer the look: about once per 10 MB, 0.8 s to 30 s.
    $pollMs = [math]::Max(800, [math]::Min(30000, [int]($file.Length / 10MB * 1000)))
    $deadline = (Get-Date).AddMinutes($FileTimeoutMin)
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds $pollMs
        try { $dst = Get-MtpFolder $destPath; $have = Get-RemoteSize $dst $file.Name } catch { continue }
        if ($have -eq $file.Length) { return 'copied' }
    }
    throw "timed out copying $($file.Name) ($have of $($file.Length) bytes)"
}

if (-not $src.PSIsContainer) {
    $r = Copy-OneFile $src $Dest
    Write-Output ("{0}: {1}" -f $r, $src.Name)
    exit 0
}

# A folder: make sure it exists on the device (copying an empty local folder creates it), then each file.
$dst = Get-MtpFolder $Dest
$sub = $dst.Items() | Where-Object { $_.Name -eq $src.Name }
if (-not $sub) {
    $tmp = Join-Path $env:TEMP ("mtp_mkdir_" + [guid]::NewGuid().ToString('N'))
    $null = New-Item -ItemType Directory -Path (Join-Path $tmp $src.Name)
    $dst.CopyHere($shell.NameSpace($tmp).ParseName($src.Name), 4 + 16 + 512 + 1024)
    for ($i = 0; $i -lt 60; $i++) {
        Start-Sleep -Seconds 1
        $dst = Get-MtpFolder $Dest
        if ($dst.Items() | Where-Object { $_.Name -eq $src.Name }) { break }
    }
    Remove-Item -Recurse -Force $tmp
}
$files = Get-ChildItem -LiteralPath $src.FullName -File
$n = 0; $copied = 0; $skipped = 0
$total = ($files | Measure-Object -Property Length -Sum).Sum
$doneBytes = [int64]0
foreach ($f in $files) {
    $n++
    $r = Copy-OneFile $f "$Dest/$($src.Name)"
    $doneBytes += $f.Length
    if ($r -eq 'copied') { $copied++ } else { $skipped++ }
    Write-Output ("{0}/{1} {2} {3} ({4:N0} MB of {5:N0} MB)  {6:HH:mm:ss}" -f $n, $files.Count, $r, $f.Name,
        ($doneBytes / 1MB), ($total / 1MB), (Get-Date))
}
Write-Output ("done: {0} copied, {1} already there" -f $copied, $skipped)
