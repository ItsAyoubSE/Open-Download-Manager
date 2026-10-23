# Test-only HTTP server for ODM resume tests.
# Serves <Root>\testfile.bin (version 1) or <Root>\testfile-v2.bin (version 2)
# with byte-range support. Behaviour is controlled by files in <Root>:
#   mode.txt     normal | malformed | truncated | weak
#   version.txt  1 | 2 (selects the content file; default 1)
#   etag.txt     exact ETag value to send (optional override)
# Every request is appended to server.log.
param(
    [Parameter(Mandatory=$true)][string]$Root,
    [int]$Port = 8765,
    [int]$ChunkDelayMs = 18
)
$ErrorActionPreference = 'Stop'
$listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, $Port)
$listener.Start()
[Console]::Out.WriteLine("LISTENING $Port")
[Console]::Out.Flush()

function Get-Text([string]$name, [string]$def) {
    $p = Join-Path $Root $name
    if (Test-Path $p) { return (Get-Content $p -Raw).Trim() }
    return $def
}

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
        $method = ($reqLine -split ' ')[0]
        $rangeHeader = ''
        foreach ($ln in $lines) { if ($ln -match '^Range:') { $rangeHeader = $ln } }
        Add-Content -Path (Join-Path $Root "server.log") -Value ("REQ $method $rangeHeader")

        $mode = Get-Text "mode.txt" "normal"
        $ver = Get-Text "version.txt" "1"
        $file = if ($ver -eq "2") { Join-Path $Root "testfile-v2.bin" } else { Join-Path $Root "testfile.bin" }
        $bytes = [IO.File]::ReadAllBytes($file)
        $total = $bytes.Length
        $etag = Get-Text "etag.txt" ('"resume-test-v' + $ver + '"')
        if ($mode -eq 'weak') { $etag = 'W/' + ($etag -replace '^W/', '') }

        $rangeStart = -1; $rangeEnd = -1
        if ($rangeHeader -match 'bytes=(\d+)-(\d*)') {
            $rangeStart = [int64]$Matches[1]
            if ($Matches[2] -ne '') { $rangeEnd = [int64]$Matches[2] }
        }

        if ($method -eq 'HEAD') {
            $head = "HTTP/1.1 200 OK`r`nContent-Length: $total`r`nAccept-Ranges: bytes`r`nETag: $etag`r`nConnection: close`r`n`r`n"
            $hb = [Text.Encoding]::ASCII.GetBytes($head)
            $ns.Write($hb, 0, $hb.Length); $ns.Flush()
        } elseif ($rangeStart -ge 0) {
            if ($rangeEnd -lt 0 -or $rangeEnd -ge $total) { $rangeEnd = $total - 1 }
            $len = $rangeEnd - $rangeStart + 1
            $crStart = $rangeStart; $crEnd = $rangeEnd
            if ($mode -eq 'malformed') { $crStart = $rangeStart + 1; $crEnd = $rangeEnd + 1 }
            $head = "HTTP/1.1 206 Partial Content`r`nContent-Length: $len`r`nContent-Range: bytes $crStart-$crEnd/$total`r`nAccept-Ranges: bytes`r`nETag: $etag`r`nConnection: close`r`n`r`n"
            $hb = [Text.Encoding]::ASCII.GetBytes($head)
            $ns.Write($hb, 0, $hb.Length); $ns.Flush()
            $off = $rangeStart
            $sent = 0
            $limit = if ($mode -eq 'truncated') { [int64]($len / 2) } else { $len }
            while ($sent -lt $limit -and $off -le $rangeEnd) {
                $n = [Math]::Min(65536, [Math]::Min($rangeEnd - $off + 1, $limit - $sent))
                $ns.Write($bytes, [int]$off, [int]$n)
                $ns.Flush()
                $off += $n; $sent += $n
                if ($ChunkDelayMs -gt 0) { Start-Sleep -Milliseconds $ChunkDelayMs }
            }
        } else {
            $head = "HTTP/1.1 200 OK`r`nContent-Length: $total`r`nAccept-Ranges: bytes`r`nETag: $etag`r`nConnection: close`r`n`r`n"
            $hb = [Text.Encoding]::ASCII.GetBytes($head)
            $ns.Write($hb, 0, $hb.Length); $ns.Flush()
            $off = 0
            while ($off -lt $total) {
                $n = [Math]::Min(65536, $total - $off)
                $ns.Write($bytes, [int]$off, [int]$n)
                $ns.Flush()
                $off += $n
                if ($ChunkDelayMs -gt 0) { Start-Sleep -Milliseconds $ChunkDelayMs }
            }
        }
    } catch {
        Add-Content -Path (Join-Path $Root "server.log") -Value ("ERR " + $_.Exception.Message)
    } finally {
        try { $client.Close() } catch {}
    }
}
