<#
  udp_send.ps1 - fire-and-collect UDP helper for TestMode.hta
  Sends one ASCII payload to IP:Port, then collects every reply datagram
  that arrives before TimeoutMs elapses (some commands - e.g. an enveloped
  "#SS<cmd>" - answer with TWO packets: the command's own reply, then the
  "#SSR" ack). Prints a single pipe-delimited line to stdout:
    OK|reply1<US>reply2<US>...      (US = \x1f, used as a safe separator)
    TIMEOUT|no reply within <n> ms
    ERROR|<message>
#>
param(
    [Parameter(Mandatory = $true)][string]$IP,
    [Parameter(Mandatory = $true)][int]$Port,
    [Parameter(Mandatory = $true)][string]$Payload,
    [int]$TimeoutMs = 1200
)

$ErrorActionPreference = 'Stop'
$US = [char]0x1F

try {
    # Both firmwares reply to a FIXED port (whatever they themselves listen
    # on), never to the actual source port of the incoming request - no
    # remotePort() tracking, just beginPacket(senderIP, UDP_PORT). The real
    # phone app works because it binds its own local socket to that same
    # fixed port; a UdpClient bound to an ephemeral port instead would have
    # every reply sent to a local port nothing is listening on and silently
    # dropped - indistinguishable from every command timing out.
    $udp = New-Object System.Net.Sockets.UdpClient($Port)
} catch {
    Write-Output "ERROR|socket create failed: $($_.Exception.Message)"
    exit 1
}

try {
    $bytes = [System.Text.Encoding]::UTF8.GetBytes($Payload)
    $udp.Send($bytes, $bytes.Length, $IP, $Port) | Out-Null
} catch {
    Write-Output "ERROR|send failed: $($_.Exception.Message)"
    $udp.Close()
    exit 1
}

$replies  = New-Object System.Collections.Generic.List[string]
$deadline = (Get-Date).AddMilliseconds($TimeoutMs)

while ($true) {
    $remainMs = [int](($deadline - (Get-Date)).TotalMilliseconds)
    if ($remainMs -le 0) { break }
    $udp.Client.ReceiveTimeout = $remainMs
    $remoteEP = New-Object System.Net.IPEndPoint([System.Net.IPAddress]::Any, 0)
    try {
        $recv = $udp.Receive([ref]$remoteEP)
        $text = [System.Text.Encoding]::UTF8.GetString($recv)
        $replies.Add($text)
    } catch {
        break
    }
}

$udp.Close()

if ($replies.Count -eq 0) {
    Write-Output "TIMEOUT|no reply within $TimeoutMs ms"
} else {
    Write-Output ("OK|" + ($replies -join $US))
}
