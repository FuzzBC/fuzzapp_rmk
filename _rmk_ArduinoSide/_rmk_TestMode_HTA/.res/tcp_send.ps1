<#
  tcp_send.ps1 - one-shot Telnet/TCP line sender for TestMode.hta.

  Opens a TCP connection (the diffuser's Serial/Telnet console, default
  port 23), writes -PayloadHex (decoded) as one CRLF-terminated line,
  collects whatever text arrives within -TimeoutMs, then closes. Console replies
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
    # Hex-encoded, not raw text - see udp_send.ps1's -PayloadHex comment.
    # Console commands here are normally plain ASCII, but the raw-send box
    # can contain anything, so this gets the same safe treatment.
    [Parameter(Mandatory = $true)][string]$PayloadHex,
    [Parameter(Mandatory = $true)][string]$OutFile,
    [int]$TimeoutMs = 1500
)

$ErrorActionPreference = 'Stop'
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
    # A single-element [byte[]] can get unwrapped to a plain scalar by
    # PowerShell's return-value handling (confirmed live: "S" -> a 1-byte
    # payload - broke the `+` concatenation below with a bizarre
    # op_Addition error) - build the CRLF-terminated line explicitly
    # instead of relying on array-plus-array to always stay array-shaped.
    $payloadBytes = @(Decode-Hex $PayloadHex)
    $lineBytes = New-Object byte[] ($payloadBytes.Length + 2)
    if ($payloadBytes.Length -gt 0) { [System.Array]::Copy($payloadBytes, $lineBytes, $payloadBytes.Length) }
    $lineBytes[$payloadBytes.Length] = 13
    $lineBytes[$payloadBytes.Length + 1] = 10
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
