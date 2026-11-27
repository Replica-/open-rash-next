# Bike / rider mask editor: which pixels of each rider sprite (Data\Bikers\HIBOB.DAT) are the rider and which the bike, checked by eye.
# A small local web server (localhost only) for mask_editor.html: the page gets the sprites from your game copy (data\) at start-up and every edit
# is written straight back to bike-rider-mask.json (next to this script) - no export step.  Stop it with the debugger's Stop button or Ctrl+C.
# usage: tools\mask_editor\mask_editor.ps1 [-Port 8765] [-MaskFile <json>] [-NoBrowser]
# Mask file: { "format": "rr-bike-rider-mask-1", "sprites": { "<sprite>": { "w", "h", "state": auto|edited|checked, "m": "0/1/2 per pixel" } } }
#   1 = rider, 2 = bike, 0 = transparent; one character per sprite pixel, rows top to bottom.  "auto" = the editor's guess, not looked at yet.
param([int]$Port = 8765, [string]$MaskFile = "$PSScriptRoot\bike-rider-mask.json", [switch]$NoBrowser)
$ErrorActionPreference = "Stop"
$Root = (Resolve-Path "$PSScriptRoot\..\..").Path
$MaskFile = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($MaskFile)

# ---- the sprites, their own colours (the first rider's colour row) ----
$spriteFile = "$Root\data\Data\Bikers\HIBOB.DAT"
if (-not (Test-Path $spriteFile)) { throw "rider sprites not found: $spriteFile (your copy of the game goes in data\)" }
$dat = [IO.File]::ReadAllBytes($spriteFile)
$remap = New-Object byte[] 3840; $k = -1   # tbl_riderColourRemap (src\data\data_race.c): 15 rows of 256
foreach ($l in [IO.File]::ReadAllLines("$Root\src\data\data_race.c")) {
    if ($k -lt 0) { if ($l -match 'unsigned char tbl_riderColourRemap\[') { $k = 0 }; continue }
    if ($l -eq '};') { break }
    foreach ($v in $l.Split(',', [StringSplitOptions]::RemoveEmptyEntries)) { if ($k -lt 3840) { $remap[$k] = [byte]$v.Trim() }; $k++ }
}
# the race palette (captured from a race by analysis\race_palette.ps1) when you have it, else the menu palette Data\Palette.RAW (colours a little off)
$racePal = "$Root\analysis\tools\race_palette.pal"; $rgb = New-Object int[] 256
if (Test-Path $racePal) { $p = [IO.File]::ReadAllBytes($racePal); for ($i = 0; $i -lt 256; $i++) { $rgb[$i] = ([int]$p[$i * 3] -shl 16) -bor ([int]$p[$i * 3 + 1] -shl 8) -bor [int]$p[$i * 3 + 2] }; $palName = "race" }
else { $p = [IO.File]::ReadAllBytes("$Root\data\Data\Palette.RAW"); for ($i = 0; $i -lt 256; $i++) { $rgb[$i] = ([int]$p[$i * 4 + 2] -shl 16) -bor ([int]$p[$i * 4 + 1] -shl 8) -bor [int]$p[$i * 4] }; $palName = "menu" }   # (B, G, R, x; [int]: a byte -shl overflows)
$lut = for ($t = 0; $t -lt 256; $t++) { $rgb[$remap[$t]] }
$sp = New-Object Collections.Generic.List[string]; $o = 0; $n = 0
while ($o + 2 -le $dat.Length) { $w = [int]$dat[$o]; $h = [int]$dat[$o + 1]
    if ($w -gt 0 -and $h -gt 0) { $b = New-Object byte[] ($w * $h); [Array]::Copy($dat, $o + 2, $b, 0, $b.Length); $sp.Add("[$n,$w,$h,`"$([Convert]::ToBase64String($b))`"]") }
    $o += 2 + $w * $h; $n++ }
$dataJson = [Text.Encoding]::UTF8.GetBytes("{`"palette`":`"$palName`",`"maskFile`":$(ConvertTo-Json $MaskFile),`"lut`":[$($lut -join ',')],`"sprites`":[$($sp -join ',')]}")

# ---- the server ----
$http = New-Object Net.HttpListener; $http.Prefixes.Add("http://localhost:$Port/")
try { $http.Start() } catch { throw "port $Port is in use - is the editor already running? (or pass -Port)" }
$url = "http://localhost:$Port/"
Write-Host "Bike / rider mask editor: $url   ($($sp.Count) sprites, $palName palette)"
Write-Host "Masks: $MaskFile  (saved as you edit)   Stop: the Stop button / Ctrl+C"
if (-not $NoBrowser) { Start-Process $url }
function Reply($ctx, [int]$code, [string]$type, [byte[]]$body) {
    $r = $ctx.Response; $r.StatusCode = $code; $r.ContentType = $type; $r.Headers["Cache-Control"] = "no-store"
    $r.ContentLength64 = $body.Length; $r.OutputStream.Write($body, 0, $body.Length); $r.Close() }
function Text([string]$s) { [Text.Encoding]::UTF8.GetBytes($s) }
try {
    while ($http.IsListening) {
        $wait = $http.GetContextAsync(); while (-not $wait.Wait(250)) { }   # (polled, so Ctrl+C / Stop get through)
        $ctx = $wait.Result; $req = $ctx.Request; $path = $req.Url.AbsolutePath
        try {
            if ($req.HttpMethod -eq "GET" -and $path -eq "/") { Reply $ctx 200 "text/html; charset=utf-8" ([IO.File]::ReadAllBytes("$PSScriptRoot\mask_editor.html")) }
            elseif ($req.HttpMethod -eq "GET" -and $path -eq "/data") { Reply $ctx 200 "application/json" $dataJson }
            elseif ($req.HttpMethod -eq "GET" -and $path -eq "/masks") { Reply $ctx 200 "application/json" $(if (Test-Path $MaskFile) { [IO.File]::ReadAllBytes($MaskFile) } else { Text "{}" }) }
            elseif ($req.HttpMethod -eq "PUT" -and $path -eq "/masks") {
                $body = (New-Object IO.StreamReader($req.InputStream, [Text.Encoding]::UTF8)).ReadToEnd()
                if (-not $body.StartsWith('{"format":"rr-bike-rider-mask-1"')) { Reply $ctx 400 "text/plain" (Text "not a bike-rider mask file"); continue }
                $tmp = "$MaskFile.tmp"; [IO.File]::WriteAllText($tmp, $body, (New-Object Text.UTF8Encoding $false))
                if (Test-Path $MaskFile) { [IO.File]::Replace($tmp, $MaskFile, [NullString]::Value) } else { [IO.File]::Move($tmp, $MaskFile) }   # whole file or nothing ([NullString]: no backup - a plain $null arrives as "")
                Reply $ctx 200 "application/json" (Text "{`"saved`":`"$((Get-Date).ToString('HH:mm:ss'))`"}") }
            elseif ($req.HttpMethod -eq "POST" -and $path -eq "/quit") { Reply $ctx 200 "text/plain" (Text "bye"); break }
            else { Reply $ctx 404 "text/plain" (Text "not found") }
        } catch { Write-Warning "$($req.HttpMethod) $path : $_"; try { Reply $ctx 500 "text/plain" (Text "$_") } catch { } }
    }
} finally { $http.Stop(); $http.Close(); Write-Host "Mask editor stopped." }
