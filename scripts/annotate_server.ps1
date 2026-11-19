# The annotate server: the Course Editor's notes (its Note tool, src\editor\courseedit.c NoteAt) for Claude.  The editor POSTs each note (JSON: the place - track,
# level, piece, side, cell -, the camera, the text, its screenshot under analysis\annotations\shots) to http://127.0.0.1:8737/note; they are kept one a line in
# analysis\annotations\inbox.jsonl (Claude reads them there).  http://127.0.0.1:8737/ lists them (newest first) with their screenshots; Done / Reopen mark them.
# scripts\run_editor.ps1 starts it (hidden) when it is not running.  -Port: another port (the editor's NOTE_PORT must match).
param([int]$Port = 8737)
$root = (Resolve-Path "$PSScriptRoot\..").Path; $dir = "$root\analysis\annotations"; $inbox = "$dir\inbox.jsonl"
New-Item -ItemType Directory -Force "$dir\shots" | Out-Null
$utf8 = New-Object Text.UTF8Encoding $false
$l = New-Object Net.HttpListener; $l.Prefixes.Add("http://127.0.0.1:$Port/")
try { $l.Start() } catch { "port $Port is in use (the server already running?)"; exit 1 }
"annotate server: http://127.0.0.1:$Port/ - notes in $inbox"

function Send($rs, [int]$code, [string]$type, [byte[]]$data) { $rs.StatusCode = $code; $rs.ContentType = $type; $rs.ContentLength64 = $data.Length; $rs.OutputStream.Write($data, 0, $data.Length); $rs.Close() }
function Notes { if (Test-Path $inbox) { @(Get-Content $inbox -Encoding UTF8 | Where-Object { $_.Trim() } | ForEach-Object { $_ | ConvertFrom-Json }) } else { @() } }
function Enc([string]$t) { [Net.WebUtility]::HtmlEncode($t) }

while ($l.IsListening) {
    $ctx = $l.GetContext(); $rq = $ctx.Request; $rs = $ctx.Response; $path = $rq.Url.AbsolutePath
    try {
        if ($rq.HttpMethod -eq 'POST' -and $path -eq '/note') {   # a note from the editor
            $body = (New-Object IO.StreamReader($rq.InputStream, $utf8)).ReadToEnd()
            $n = $body | ConvertFrom-Json   # (a bad one: an error, not kept)
            [IO.File]::AppendAllText($inbox, (($body -replace "`r?`n", ' ').Trim() + "`n"), $utf8)
            "note $($n.id): track $($n.track) piece $($n.piece) $($n.side) $($n.cell): $($n.text)"
            Send $rs 200 'text/plain' $utf8.GetBytes('ok') }
        elseif ($rq.HttpMethod -eq 'POST' -and $path -match '^/(done|reopen)/([\w.-]+)$') {   # Done / Reopen
            $state = if ($Matches[1] -eq 'done') { 'done' } else { 'new' }; $id = $Matches[2]
            $lines = Notes | ForEach-Object { if ($_.id -eq $id) { $_.state = $state }; $_ | ConvertTo-Json -Compress -Depth 5 }
            [IO.File]::WriteAllText($inbox, (($lines -join "`n") + "`n"), $utf8)
            Send $rs 200 'text/plain' $utf8.GetBytes('ok') }
        elseif ($path -eq '/notes') { Send $rs 200 'application/json' $utf8.GetBytes((ConvertTo-Json @(Notes) -Depth 5)) }
        elseif ($path -match '^/shots/([\w.-]+\.bmp)$' -and (Test-Path "$dir\shots\$($Matches[1])")) { Send $rs 200 'image/bmp' ([IO.File]::ReadAllBytes("$dir\shots\$($Matches[1])")) }
        elseif ($path -eq '/') {   # the list
            $rows = @(Notes); [array]::Reverse($rows)
            $h = "<!doctype html><meta charset=utf-8><title>Course Editor notes</title><style>body{font:14px sans-serif;margin:16px;background:#f4f4f4}" +
                 ".n{background:#fff;border:1px solid #ccc;border-radius:6px;padding:10px;margin:0 0 12px}.done{opacity:.5}img{max-width:100%;max-height:360px;display:block;margin-top:8px}" +
                 ".m{color:#666;font-size:12px}pre{white-space:pre-wrap;font:15px sans-serif;margin:6px 0}</style><h2>Course Editor notes ($($rows.Count))</h2>" +
                 "<script>function mark(i,s){fetch('/'+s+'/'+i,{method:'POST'}).then(()=>location.reload())}</script>"
            foreach ($n in $rows) {
                $h += "<div class='n $(if ($n.state -eq 'done') { 'done' })'><div class=m>$(Enc $n.time) - track $($n.track) level $($n.level), piece $($n.piece) $(Enc $n.side) $(Enc $n.cell) - $(Enc $n.state) " +
                      "<button onclick=""mark('$($n.id)','$(if ($n.state -eq 'done') { 'reopen' } else { 'done' })')"">$(if ($n.state -eq 'done') { 'Reopen' } else { 'Done' })</button></div><pre>$(Enc $n.text)</pre>" +
                      "$(if ($n.shot) { "<img src='/$($n.shot)'>" })</div>" }
            Send $rs 200 'text/html; charset=utf-8' $utf8.GetBytes($h) }
        elseif ($path -eq '/quit') { Send $rs 200 'text/plain' $utf8.GetBytes('bye'); $l.Stop() }
        else { Send $rs 404 'text/plain' $utf8.GetBytes('not found') }
    } catch { try { Send $rs 500 'text/plain' $utf8.GetBytes("$_") } catch {} }
}
