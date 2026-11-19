# Watches the annotate server's inbox (analysis\annotations\inbox.jsonl, scripts\annotate_server.ps1) and prints one line for each new note - run by Claude Code's
# Monitor, each line reaches the Claude session as an event (user, 2026-11-18: "it should send a prompt to you in the background that goes to this window").
# Notes already there when it starts are not printed again.
$root = (Resolve-Path "$PSScriptRoot\..").Path; $inbox = "$root\analysis\annotations\inbox.jsonl"
$seen = @{}
function Read-Notes { if (Test-Path $inbox) { Get-Content $inbox -Encoding UTF8 | Where-Object { $_.Trim() } | ForEach-Object { try { $_ | ConvertFrom-Json } catch {} } } }
Read-Notes | ForEach-Object { $seen[$_.id] = 1 }
while ($true) {
    foreach ($n in @(Read-Notes)) {
        if ($seen.ContainsKey($n.id)) { continue }
        $seen[$n.id] = 1
        if ($n.state -ne 'new') { continue }
        $text = ($n.text -replace '\s+', ' ')
        [Console]::Out.WriteLine("EDITOR NOTE $($n.id): track $($n.track) piece $($n.piece) $($n.side) $($n.cell) | shot analysis\annotations\$($n.shot -replace '/', '\') | $text")
        [Console]::Out.Flush() }
    Start-Sleep -Milliseconds 1000 }
