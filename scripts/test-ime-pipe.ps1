#Requires -Version 5.1
<#
.SYNOPSIS
  Test Fcitx5 IME named-pipe IPC (same protocol as PipeImeEngine / Fcitx5ImePipeServer).

  Pipe path: \\.\pipe\<USERNAME>\Fcitx5ImePipe_v1  (imeIpcNamedPipePath in Fcitx5ImeIpcProtocol.cpp)

  By default uses ONE connection for Ping → Clear → AppendLatin ×2 → Clear (matches TSF keeping the pipe open).
  Use -MultiConnection to send each step on a new connection (each session re-inits).

.PARAMETER DeployDir
  With -StartServer: runs DeployDir\bin\Fcitx5ImePipeServer.exe if the pipe is unreachable.

.PARAMETER TryPinyin
  After first Ping, send ActivateProfileInputMethod("pinyin") on the same connection (single-session mode only).

.PARAMETER TryInputMethod
  After first Ping, send ActivateProfileInputMethod("<name>") on the same connection (or on each request in -MultiConnection mode).
  Example: -TryInputMethod wbx

.PARAMETER AppendText
  Latin text to send one codepoint at a time via AppendLatin. Default: ni

.PARAMETER UseRawKeyEvent
  Send \`DeliverFcitxRawKeyEvent\` requests for each Latin character in \`AppendText\` instead of \`AppendLatin\`.
  This better matches the TSF path for real key presses.

.EXAMPLE
  .\scripts\test-ime-pipe.ps1 -DeployDir C:\Fcitx5Portable -StartServer
.EXAMPLE
  .\scripts\test-ime-pipe.ps1 -TryPinyin
.EXAMPLE
  .\scripts\test-ime-pipe.ps1 -TryInputMethod wbx -AppendText wg
.EXAMPLE
  .\scripts\test-ime-pipe.ps1 -TryInputMethod wbx -AppendText wg -UseRawKeyEvent
.EXAMPLE
  .\scripts\test-ime-pipe.ps1 -MultiConnection
#>
param(
    [string] $DeployDir = 'C:\Fcitx5Portable',
    [switch] $StartServer,
    [switch] $TryPinyin,
    [string] $TryInputMethod,
    [string] $AppendText = 'ni',
    [switch] $UseRawKeyEvent,
    [switch] $MultiConnection
)

$ErrorActionPreference = 'Stop'

$activationIm = $null
if ($TryPinyin) {
    $activationIm = 'pinyin'
}
if ($TryInputMethod) {
    if ($activationIm) {
        throw "Use either -TryPinyin or -TryInputMethod, not both."
    }
    $activationIm = $TryInputMethod
}

$kMagic = [uint32]0x31435446
$kVersion = [uint32]1

function New-IpcRequest {
    param([uint32] $Opcode, [byte[]] $Body = @())
    $ms = New-Object System.IO.MemoryStream
    $w = [System.IO.BinaryWriter]::new($ms)
    $w.Write($kMagic)
    $w.Write($kVersion)
    $w.Write($Opcode)
    $w.Write([uint32]$Body.Length)
    if ($Body.Length -gt 0) { $w.Write($Body) }
    $w.Flush()
    return $ms.ToArray()
}

function Read-U32 {
    param([byte[]] $Bytes, [ref] $Offset)
    if ($Offset.Value + 4 -gt $Bytes.Length) { throw "Read-U32 past end" }
    $v = [BitConverter]::ToUInt32($Bytes, $Offset.Value)
    $Offset.Value += 4
    return $v
}

function Read-Utf8Blob {
    param([byte[]] $Bytes, [ref] $Offset)
    $len = Read-U32 -Bytes $Bytes -Offset $Offset
    if ($len -gt 256 * 1024) { throw "Utf8 blob too large: $len" }
    if ($Offset.Value + $len -gt $Bytes.Length) { throw "Read-Utf8Blob past end" }
    $s = [Text.Encoding]::UTF8.GetString($Bytes, $Offset.Value, [int]$len)
    $Offset.Value += $len
    return $s
}

function Decode-IpcSuccessBodyFull {
    param([byte[]] $Body)
    $i = [ref]0
    $hasDrained = Read-U32 -Bytes $Body -Offset $i
    $drained = $null
    if ($hasDrained -ne 0) {
        $drained = Read-Utf8Blob -Bytes $Body -Offset $i
    }
    $flags = Read-U32 -Bytes $Body -Offset $i
    $preU8 = Read-Utf8Blob -Bytes $Body -Offset $i
    $caret = Read-U32 -Bytes $Body -Offset $i
    $hi = Read-U32 -Bytes $Body -Offset $i
    $nCand = Read-U32 -Bytes $Body -Offset $i
    $cands = New-Object System.Collections.Generic.List[string]
    for ($c = 0; $c -lt $nCand; $c++) {
        $cands.Add((Read-Utf8Blob -Bytes $Body -Offset $i))
    }
    $curIm = Read-Utf8Blob -Bytes $Body -Offset $i
    $nProf = Read-U32 -Bytes $Body -Offset $i
    for ($p = 0; $p -lt $nProf; $p++) {
        [void](Read-Utf8Blob -Bytes $Body -Offset $i)
        [void](Read-Utf8Blob -Bytes $Body -Offset $i)
        [void](Read-U32 -Bytes $Body -Offset $i)
    }
    $nTray = Read-U32 -Bytes $Body -Offset $i
    for ($t = 0; $t -lt $nTray; $t++) {
        [void](Read-Utf8Blob -Bytes $Body -Offset $i)
        [void](Read-Utf8Blob -Bytes $Body -Offset $i)
        [void](Read-U32 -Bytes $Body -Offset $i)
    }
    return [PSCustomObject]@{
        DrainedCommitUtf8 = $drained
        Flags             = $flags
        PreeditUtf8       = $preU8
        CaretUtf16        = $caret
        Highlight         = $hi
        CandidatesUtf8    = $cands
        CurrentImUtf8     = $curIm
    }
}

function New-ActivateProfileImBody([string] $UniqueName) {
    $u8 = [Text.Encoding]::UTF8.GetBytes($UniqueName)
    $ms = New-Object System.IO.MemoryStream
    $w = [System.IO.BinaryWriter]::new($ms)
    $w.Write([uint32]$u8.Length)
    $w.Write($u8)
    $w.Flush()
    return $ms.ToArray()
}

function Read-IpcResponseFromStream {
    param([System.IO.Pipes.PipeStream] $Client)
    $hdr = New-Object byte[] 16
    $read = 0
    while ($read -lt 16) {
        $n = $Client.Read($hdr, $read, 16 - $read)
        if ($n -eq 0) { throw "EOF reading header" }
        $read += $n
    }
    $magic = [BitConverter]::ToUInt32($hdr, 0)
    $ver = [BitConverter]::ToUInt32($hdr, 4)
    $opOrSt = [BitConverter]::ToUInt32($hdr, 8)
    $bodySize = [BitConverter]::ToUInt32($hdr, 12)
    if ($magic -ne $kMagic -or $ver -ne $kVersion) {
        throw "Bad response magic=$magic ver=$ver"
    }
    $body = New-Object byte[] $bodySize
    $read = 0
    while ($read -lt $bodySize) {
        $n = $Client.Read($body, $read, [int]($bodySize - $read))
        if ($n -eq 0) { throw "EOF reading body" }
        $read += $n
    }
    return [PSCustomObject]@{ OpcodeOrStatus = $opOrSt; Body = $body }
}

function Invoke-IpcPipeOneShot {
    param([string] $PipePath, [byte[]] $Request)
    $pipeOnly = [regex]::Replace($PipePath, '^\\\\\.\\pipe\\', '')
    $client = New-Object System.IO.Pipes.NamedPipeClientStream(
        '.', $pipeOnly,
        [System.IO.Pipes.PipeDirection]::InOut,
        [System.IO.Pipes.PipeOptions]::None,
        [System.Security.Principal.TokenImpersonationLevel]::Impersonation
    )
    try {
        $client.Connect(5000)
        $client.Write($Request, 0, $Request.Length)
        $client.Flush()
        return (Read-IpcResponseFromStream -Client $client)
    }
    finally {
        if ($client) { $client.Dispose() }
    }
}

function Write-Host-Decoded {
    param([string] $Label, $Resp)
    Write-Host "--- $Label ---"
    if ($Resp.OpcodeOrStatus -ne 0) {
        Write-Host "ERROR status=$($Resp.OpcodeOrStatus)"
        return $false
    }
    $dec = Decode-IpcSuccessBodyFull -Body $Resp.Body
    Write-Host "currentIm: $($dec.CurrentImUtf8)"
    Write-Host "preedit:   $($dec.PreeditUtf8)"
    Write-Host "caret:     $($dec.CaretUtf16)  highlight: $($dec.Highlight)  flags: $($dec.Flags)"
    Write-Host "candidates ($($dec.CandidatesUtf8.Count)):"
    $ix = 0
    foreach ($line in $dec.CandidatesUtf8) {
        Write-Host ("  [{0}] {1}" -f $ix, $line)
        $ix++
    }
    if ($dec.DrainedCommitUtf8) {
        Write-Host "drainedCommit: $($dec.DrainedCommitUtf8)"
    }
    Write-Host "OK"
    return $true
}

function Get-AppendLatinRequests {
    param([string] $Text, [switch] $UseRawKeyEvent)
    $reqs = New-Object System.Collections.Generic.List[object]
    foreach ($ch in $Text.ToCharArray()) {
        if ($UseRawKeyEvent) {
            $vk = [uint32][char]([char]::ToUpperInvariant($ch))
            $ms = New-Object System.IO.MemoryStream
            $w = [System.IO.BinaryWriter]::new($ms)
            $w.Write($vk)
            $w.Write([uint64]0)
            $w.Write([uint32]0)
            $w.Write([uint32]0)
            $w.Flush()
            $reqs.Add([PSCustomObject]@{
                Label   = "DeliverFcitxRawKeyEvent $ch"
                Request = (New-IpcRequest -Opcode 13 -Body $ms.ToArray())
            })
        }
        else {
            $u32 = [uint32][char]$ch
            $reqs.Add([PSCustomObject]@{
                Label   = "AppendLatin $ch"
                Request = (New-IpcRequest -Opcode 3 -Body ([BitConverter]::GetBytes($u32)))
            })
        }
    }
    return $reqs
}

$user = [System.Environment]::UserName
$pipeFull = "\\.\pipe\$user\Fcitx5ImePipe_v1"
Write-Host "Pipe: $pipeFull"
if ($MultiConnection) {
    Write-Host "Mode: MultiConnection (new session per request — not how TSF uses the pipe)"
}
else {
    Write-Host "Mode: single connection (same as PipeImeEngine)"
}
Write-Host ""

$exe = Join-Path $DeployDir 'bin\Fcitx5ImePipeServer.exe'
if ($StartServer -and (Test-Path -LiteralPath $exe)) {
    try {
        $null = Invoke-IpcPipeOneShot -PipePath $pipeFull -Request (New-IpcRequest -Opcode 0)
    }
    catch {
        Write-Host "Starting pipe server: $exe"
        Start-Process -FilePath $exe -WindowStyle Hidden
        Start-Sleep -Seconds 2
    }
}

try {
    $null = Invoke-IpcPipeOneShot -PipePath $pipeFull -Request (New-IpcRequest -Opcode 0)
}
catch {
    Write-Host "Connect failed: $($_.Exception.Message)"
    if (-not $StartServer -and (Test-Path -LiteralPath $exe)) {
        Write-Host "Tip: add -StartServer or run $exe once."
    }
    exit 1
}

if ($MultiConnection) {
    [void](Write-Host-Decoded -Label 'Ping' -Resp (Invoke-IpcPipeOneShot -PipePath $pipeFull -Request (New-IpcRequest -Opcode 0)))
    if ($activationIm) {
        $actBody = New-ActivateProfileImBody -UniqueName $activationIm
        [void](Write-Host-Decoded -Label "ActivateProfileInputMethod $activationIm" -Resp (Invoke-IpcPipeOneShot -PipePath $pipeFull -Request (New-IpcRequest -Opcode 14 -Body $actBody)))
    }
    [void](Write-Host-Decoded -Label 'Clear' -Resp (Invoke-IpcPipeOneShot -PipePath $pipeFull -Request (New-IpcRequest -Opcode 2)))
    foreach ($step in (Get-AppendLatinRequests -Text $AppendText -UseRawKeyEvent:$UseRawKeyEvent)) {
        [void](Write-Host-Decoded -Label $step.Label -Resp (Invoke-IpcPipeOneShot -PipePath $pipeFull -Request $step.Request))
    }
    [void](Write-Host-Decoded -Label 'Clear (cleanup)' -Resp (Invoke-IpcPipeOneShot -PipePath $pipeFull -Request (New-IpcRequest -Opcode 2)))
}
else {
    $pipeOnly = [regex]::Replace($pipeFull, '^\\\\\.\\pipe\\', '')
    $client = New-Object System.IO.Pipes.NamedPipeClientStream(
        '.', $pipeOnly,
        [System.IO.Pipes.PipeDirection]::InOut,
        [System.IO.Pipes.PipeOptions]::None,
        [System.Security.Principal.TokenImpersonationLevel]::Impersonation
    )
    try {
        $client.Connect(8000)
        function Send([byte[]]$req) {
            $client.Write($req, 0, $req.Length)
            $client.Flush()
            return (Read-IpcResponseFromStream -Client $client)
        }
        [void](Write-Host-Decoded -Label 'Ping' -Resp (Send (New-IpcRequest -Opcode 0)))
        if ($activationIm) {
            $actBody = New-ActivateProfileImBody -UniqueName $activationIm
            [void](Write-Host-Decoded -Label "ActivateProfileInputMethod $activationIm" -Resp (Send (New-IpcRequest -Opcode 14 -Body $actBody)))
        }
        [void](Write-Host-Decoded -Label 'Clear' -Resp (Send (New-IpcRequest -Opcode 2)))
        foreach ($step in (Get-AppendLatinRequests -Text $AppendText -UseRawKeyEvent:$UseRawKeyEvent)) {
            [void](Write-Host-Decoded -Label $step.Label -Resp (Send $step.Request))
        }
        [void](Write-Host-Decoded -Label 'Clear (cleanup)' -Resp (Send (New-IpcRequest -Opcode 2)))
    }
    finally {
        if ($client) { $client.Dispose() }
    }
}

Write-Host ""
Write-Host "Interpretation:"
Write-Host "  OK + decoded fields: binary protocol matches the server."
Write-Host "  currentIm empty and preedit stays empty after AppendLatin: IME not active for this session (profile/group/addons) — same root cause as TSF 'no typing'."
Write-Host "  Compare -MultiConnection vs default if behaviour differs (normally use single connection)."
Write-Host "  -TryInputMethod <name> activates a specific IM on the pipe session (for example: wbx, pinyin)."
Write-Host "  -AppendText controls which Latin sequence is sent through AppendLatin."
Write-Host "  -UseRawKeyEvent sends vk/lParam-style requests, closer to the TSF key path."
