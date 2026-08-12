<#
  tcp_send.ps1 - one-shot Telnet/TCP line sender for TestMode.hta.

  Opens a TCP connection (the diffuser's Serial/Telnet console, default
  port 23), writes -Payload as one CRLF-terminated line, collects
  whatever text arrives within -TimeoutMs, then closes. Console replies
  (S/D/?/banner) can span several lines, so this reads for the whole
  window instead of stopping at the first chunk - mirrors
  TestMode_APP/.res/net.py's send_tcp().

  Launched hidden via WshShell.Run() and writes its result to -OutFile
  (see udp_send.ps1's header for the full reasoning and the
  "<OutFile>.part" -> "<OutFile>" rename-on-completion protocol).

  Result:
    OK
    1
    <hex bytes of the whole reply, trimmed>
  or:
    TIMEOUT
    no reply within <n> ms
  or:
    ERROR
    <message>
#>
param(
    [Parameter(Mandatory = $true)][string]$IP,
    [Parameter(Mandatory = $true)][int]$Port,
    [Parameter(Mandatory = $true)][string]$Payload,
    [Parameter(Mandatory = $true)][string]$OutFile,
    [int]$TimeoutMs = 1500
)

$ErrorActionPreference = 'Stop'
$enc = [System.Text.Encoding]::GetEncoding(28591)  # ISO-8859-1/Latin-1 - see udp_send.ps1
$partFile = "$OutFile.part"

function Write-Result([string[]]$lines) {
    [System.IO.File]::WriteAllText($partFile, (($lines -join "`r`n") + "`r`n"), [System.Text.Encoding]::ASCII)
    Move-Item -LiteralPath $partFile -Destination $OutFile -Force
}

try {
    $client = New-Object System.Net.Sockets.TcpClient
    $connectTask = $client.ConnectAsync($IP, $Port)
    if (-not $connectTask.Wait($TimeoutMs)) {
        Write-Result @('TIMEOUT', "connect timed out after $TimeoutMs ms")
        exit 0
    }
} catch {
    Write-Result @('ERROR', "connect failed: $($_.Exception.Message)")
    exit 0
}

try {
    $stream = $client.GetStream()
    $lineBytes = $enc.GetBytes($Payload + "`r`n")
    $stream.Write($lineBytes, 0, $lineBytes.Length)
    $stream.Flush()

    $buf = New-Object byte[] 4096
    $ms = New-Object System.IO.MemoryStream
    $deadline = (Get-Date).AddMilliseconds($TimeoutMs)
    while ((Get-Date) -lt $deadline) {
        if ($stream.DataAvailable) {
            $n = $stream.Read($buf, 0, $buf.Length)
            if ($n -gt 0) { $ms.Write($buf, 0, $n) }
        } else {
            Start-Sleep -Milliseconds 60
        }
    }
    $client.Close()
} catch {
    Write-Result @('ERROR', "$($_.Exception.Message)")
    exit 0
}

$allBytes = $ms.ToArray()
# Trim leading/trailing whitespace/CRLF the same way net.py's .strip() does,
# without disturbing anything in the middle of a multi-line console reply.
$start = 0
$end = $allBytes.Length - 1
while ($start -le $end -and ($allBytes[$start] -eq 13 -or $allBytes[$start] -eq 10 -or $allBytes[$start] -eq 32 -or $allBytes[$start] -eq 9)) { $start++ }
while ($end -ge $start -and ($allBytes[$end] -eq 13 -or $allBytes[$end] -eq 10 -or $allBytes[$end] -eq 32 -or $allBytes[$end] -eq 9)) { $end-- }

if ($end -lt $start) {
    Write-Result @('TIMEOUT', "no reply within $TimeoutMs ms")
} else {
    $trimmed = $allBytes[$start..$end]
    $sb = New-Object System.Text.StringBuilder
    foreach ($b in $trimmed) { [void]$sb.Append($b.ToString('x2')) }
    Write-Result @('OK', '1', $sb.ToString())
}
