# SPDX-License-Identifier: MIT
# Test-only HTTP server for the ODM update-check tests.
# Serves <Root>\update.json with the status code from <Root>\status.txt (default
# 200). Every request is appended to <Root>\server.log.
param(
    [Parameter(Mandatory=$true)][string]$Root,
    [int]$Port = 0
)
$ErrorActionPreference = 'Stop'
$listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, $Port)
$listener.Start()
$actual = ([System.Net.IPEndPoint]$listener.LocalEndpoint).Port
[Console]::Out.WriteLine("LISTENING $actual")
[Console]::Out.Flush()

while ($true) {
    $client = $listener.AcceptTcpClient()
    try {
        $ns = $client.GetStream()
        $sb = New-Object System.Text.StringBuilder
        while ($true) {
            $b = $ns.ReadByte()
            if ($b -lt 0) { break }
            [void]$sb.Append([char]$b)
            if ($sb.Length -ge 4 -and $sb.ToString().EndsWith("`r`n`r`n")) { break }
            if ($sb.Length -gt 16384) { break }
        }
        $header = $sb.ToString()
        $lines = $header -split "`r`n"
        $reqLine = $lines[0]
        $ua = ($lines | Where-Object { $_ -match '^User-Agent:' } | Select-Object -First 1)
        Add-Content -Path (Join-Path $Root 'server.log') -Value ("REQ $reqLine | $ua")

        $status = 200
        $statusFile = Join-Path $Root 'status.txt'
        if (Test-Path $statusFile) {
            $status = [int]((Get-Content $statusFile -Raw).Trim())
        }
        $reason = switch ($status) {
            200 { 'OK' }
            500 { 'Internal Server Error' }
            default { 'Error' }
        }
        $bodyFile = Join-Path $Root 'update.json'
        $body = if (Test-Path $bodyFile) {
            [IO.File]::ReadAllBytes($bodyFile)
        } else {
            [Text.Encoding]::UTF8.GetBytes('{}')
        }
        $head = "HTTP/1.1 $status $reason`r`n" +
                "Content-Type: application/json; charset=utf-8`r`n" +
                "Content-Length: $($body.Length)`r`n" +
                "Connection: close`r`n`r`n"
        $hb = [Text.Encoding]::ASCII.GetBytes($head)
        $ns.Write($hb, 0, $hb.Length)
        $ns.Write($body, 0, $body.Length)
        $ns.Flush()
    } catch {
        # A test client may disconnect early; keep serving.
    } finally {
        $client.Close()
    }
}
