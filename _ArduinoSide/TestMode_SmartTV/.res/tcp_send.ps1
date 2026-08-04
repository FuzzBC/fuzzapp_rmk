<#
  tcp_send.ps1 - one-shot Telnet/TCP line sender for TestMode.hta
  Opens a TCP connection (Serial/Telnet console port, default 23), writes
  Payload as one CRLF-terminated line, collects whatever text arrives
  within TimeoutMs, then closes. Console replies (S/D/?/banner) can span
  several lines, so this reads for the whole window rather than stopping
  at the first chunk.
    OK|<reply text, newlines replaced with \n literal>
    TIMEOUT|no reply within <n> ms
    ERROR|<message>
#>
param(
    [Parameter(Mandatory = $true)][string]$IP,
    [Parameter(Mandatory = $true)][int]$Port,
    [Parameter(Mandatory = $true)][string]$Payload,
    [int]$TimeoutMs = 1500
)

$ErrorActionPreference = 'Stop'

try {
    $client = New-Object System.Net.Sockets.TcpClient
    $connectTask = $client.ConnectAsync($IP, $Port)
    if (-not $connectTask.Wait($TimeoutMs)) {
        Write-Output "TIMEOUT|connect timed out after $TimeoutMs ms"
        exit 0
    }
} catch {
    Write-Output "ERROR|connect failed: $($_.Exception.Message)"
    exit 1
}

try {
    $stream = $client.GetStream()
    $noBom  = New-Object System.Text.UTF8Encoding($false)
    $writer = New-Object System.IO.StreamWriter($stream, $noBom)
    $writer.NewLine  = "`r`n"
    $writer.AutoFlush = $true
    $writer.WriteLine($Payload)

    $buf = New-Object byte[] 4096
    $sb  = New-Object System.Text.StringBuilder
    $deadline = (Get-Date).AddMilliseconds($TimeoutMs)

    while ((Get-Date) -lt $deadline) {
        if ($stream.DataAvailable) {
            $n = $stream.Read($buf, 0, $buf.Length)
            if ($n -gt 0) {
                $sb.Append([System.Text.Encoding]::UTF8.GetString($buf, 0, $n)) | Out-Null
            }
        } else {
            Start-Sleep -Milliseconds 60
        }
    }

    $client.Close()
    $text = $sb.ToString().Trim()
    if ($text -eq '') {
        Write-Output "TIMEOUT|no reply within $TimeoutMs ms"
    } else {
        $flat = $text -replace "`r`n", "\n" -replace "`n", "\n"
        Write-Output "OK|$flat"
    }
} catch {
    Write-Output "ERROR|$($_.Exception.Message)"
}
