<#
  udp_send.ps1 - fire-and-collect UDP helper for TestMode.hta.

  mshta.exe hosts JScript inside Trident/MSHTML, which has no socket API
  at all - this script is what actually puts a packet on the wire. net.js
  launches it completely hidden via WshShell.Run(cmd, 0, false) (no
  console flash - WshShell.Exec always shows one, which is why this
  doesn't use that instead) and polls the filesystem for -OutFile to
  appear rather than reading a stdout pipe.

  Mirrors TestMode_APP/.res/net.py's send_udp() exactly: sends one
  payload, then collects every reply datagram that arrives before the
  IDLE window elapses. An enveloped "#SS<cmd>" can draw two packets (the
  command's own reply, then the "#SSR" ack); a diffuser-relay command can
  draw an ack then a second-hop reply seconds later - so every time a
  reply actually arrives, the idle window resets for another -TimeoutMs
  of waiting, bounded by -MaxTimeoutMs so a device that just keeps
  talking can't hang the caller forever.

  Result (written to -OutFile, ASCII only, hex-encodes every reply's raw
  bytes so a binary packet - the LED colour-sync 'LK' reply - can't be
  corrupted or mis-split by any text-based delimiter):
    OK
    <replyCount>
    <hex bytes of reply 1>
    <hex bytes of reply 2>
    ...
  or:
    TIMEOUT
    no reply within <n> ms
  or:
    ERROR
    <message>

  Written to "<OutFile>.part" first, then renamed to "<OutFile>" as the
  very last step - so the caller polling for -OutFile to exist never sees
  a half-written result.
#>
param(
    [Parameter(Mandatory = $true)][string]$IP,
    [Parameter(Mandatory = $true)][int]$Port,
    # Hex-encoded payload bytes, not raw text - a raw binary payload (any
    # byte 0-255, including 0x00) embedded directly in a Win32 command line
    # gets silently truncated at the first NUL byte (confirmed live: the
    # whole child process invocation fails, no output file, no error - see
    # net.js's toHex()/q() for the encode side). Any opcode whose payload
    # happens to contain a zero byte anywhere - extremely common: a colour
    # channel of 0, id/value of 0, a disabled flag, mode 0, etc. - silently
    # never reached the wire under the old raw-string param. Hex is
    # command-line-safe by construction (only 0-9A-F), so this sidesteps
    # the whole class of bug instead of trying to escape around it.
    [Parameter(Mandatory = $true)][string]$PayloadHex,
    [Parameter(Mandatory = $true)][string]$OutFile,
    [int]$TimeoutMs = 1200,
    [int]$MaxTimeoutMs = 0
)

$ErrorActionPreference = 'Stop'
if ($MaxTimeoutMs -le 0) { $MaxTimeoutMs = [Math]::Max($TimeoutMs * 4, 6000) }
function Decode-Hex([string]$hex) {
    if ($hex.Length -eq 0) { return [byte[]]@() }
    $out = New-Object byte[] ($hex.Length / 2)
    for ($i = 0; $i -lt $out.Length; $i++) { $out[$i] = [Convert]::ToByte($hex.Substring($i * 2, 2), 16) }
    return $out
}
$partFile = "$OutFile.part"

function Write-Result([string[]]$lines) {
    [System.IO.File]::WriteAllText($partFile, (($lines -join "`r`n") + "`r`n"), [System.Text.Encoding]::ASCII)
    Move-Item -LiteralPath $partFile -Destination $OutFile -Force
}

try {
    # Both firmwares reply to the FIXED port they themselves listen on, not
    # to our ephemeral source port (see net.py's send_udp() docstring for
    # the full reasoning) - bind here so replies land somewhere we're
    # actually listening.
    $udp = New-Object System.Net.Sockets.UdpClient($Port)
} catch {
    Write-Result @('ERROR', "socket create failed: $($_.Exception.Message)")
    exit 0
}

try {
    # @() forces array context - a single-element [byte[]] can otherwise
    # get unwrapped to a plain scalar by PowerShell's return-value handling
    # (confirmed live in tcp_send.ps1's case; applied here defensively too).
    $bytes = @(Decode-Hex $PayloadHex)
    $udp.Send($bytes, $bytes.Length, $IP, $Port) | Out-Null
} catch {
    Write-Result @('ERROR', "send failed: $($_.Exception.Message)")
    $udp.Close()
    exit 0
}

$replies = New-Object System.Collections.Generic.List[byte[]]
$hardDeadline = (Get-Date).AddMilliseconds($MaxTimeoutMs)
$idleDeadline = (Get-Date).AddMilliseconds($TimeoutMs)

while ($true) {
    $now = Get-Date
    $remain = [Math]::Min(($idleDeadline - $now).TotalMilliseconds, ($hardDeadline - $now).TotalMilliseconds)
    if ($remain -le 0) { break }
    $udp.Client.ReceiveTimeout = [Math]::Max([int]$remain, 1)
    $remoteEP = New-Object System.Net.IPEndPoint([System.Net.IPAddress]::Any, 0)
    try {
        $recv = $udp.Receive([ref]$remoteEP)
        $replies.Add($recv)
        $idleDeadline = (Get-Date).AddMilliseconds($TimeoutMs)  # more may still be coming - extend
    } catch {
        break
    }
}
$udp.Close()

if ($replies.Count -eq 0) {
    Write-Result @('TIMEOUT', "no reply within $TimeoutMs ms")
} else {
    $lines = New-Object System.Collections.Generic.List[string]
    $lines.Add('OK')
    $lines.Add([string]$replies.Count)
    foreach ($r in $replies) {
        $sb = New-Object System.Text.StringBuilder
        foreach ($b in $r) { [void]$sb.Append($b.ToString('x2')) }
        $lines.Add($sb.ToString())
    }
    Write-Result $lines.ToArray()
}
