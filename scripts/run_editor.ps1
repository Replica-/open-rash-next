# Run the Course Editor (src\editor\courseedit.c): -Rebuild = build it first (build_editor.ps1); -Track / -Level = the course it opens (1..5);
# -GameRenderer = "Game renderer" on from the start (the game itself draws the course in the 3D panel).
# The editor is built again by itself whenever its source (src\editor, the True 3D headers it uses, course_edit.h, viewer_api.h) is newer than the exe -
# so every launch entry runs the latest.  An editor already open keeps its exe: then nothing is started (close it first).
# The editor starts the game (a course's first extract, Test drive F5, the game renderer), so the latest game build is staged into analysis\build\play
# first, as run_new.ps1 does; files of a game still running from there are renamed aside (*.old<time>) - Windows lets a loaded exe / DLL be renamed.
# Edited courses are saved to data\mods\courses\t<track>.rrc (one road per track, raced at every level); the game races them from then on.
param([switch]$Rebuild, [int]$Track = 1, [int]$Level = 1, [switch]$GameRenderer)
$root = (Resolve-Path "$PSScriptRoot\..").Path; $a = "$root\analysis"; $play = "$a\build\play"; $data = "$root\data"; $ed = "$a\build\editor\CourseEdit.exe"; $src = "$root\src"
if (Get-Process CourseEdit -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $ed }) {
    "The Course Editor is already open (analysis\build\editor\CourseEdit.exe) - close it first, then run this again (it is built anew from the latest source)."
    exit 1 }
$srcs = @(Get-ChildItem "$src\editor\*.c", "$src\platform\world3d\*.h", "$src\road\course_edit.h", "$src\platform\viewer_api.h" -ErrorAction SilentlyContinue)
$newest = ($srcs | Sort-Object LastWriteTime -Descending | Select-Object -First 1).LastWriteTime
if ($Rebuild -or -not (Test-Path $ed) -or ($newest -and $newest -gt (Get-Item $ed).LastWriteTime)) {
    "building the editor (source newer than the exe, or -Rebuild) ..."
    & "$PSScriptRoot\build_editor.ps1"
    if (-not (Test-Path $ed) -or ($newest -and $newest -gt (Get-Item $ed).LastWriteTime)) { "editor build failed - not starting"; exit 1 } }
if (Test-Path "$a\build\OpenRash.exe") {
    New-Item -ItemType Directory -Force $play | Out-Null
    $files = @(@{ From = "$a\build\OpenRash.exe"; Name = "OpenRash.exe" }) + @("rrddraw.dll", "SDL3.dll", "rrdsound.dll" | Where-Object { Test-Path "$a\build\sdl\$_" } | ForEach-Object { @{ From = "$a\build\sdl\$_"; Name = $_ } })
    foreach ($f in $files) {
        $to = "$play\$($f.Name)"
        if ((Test-Path $to) -and (Get-Item $to).LastWriteTime -eq (Get-Item $f.From).LastWriteTime) { continue }   # (the same build already there)
        try { Copy-Item $f.From $to -Force -ErrorAction Stop }
        catch { $old = "$($f.Name).old" + (Get-Date -Format HHmmss); Rename-Item $to $old; Copy-Item $f.From $to -Force; "staged $($f.Name) (the running game's copy renamed to $old)" } }
    Get-ChildItem "$play\*.old*" -ErrorAction SilentlyContinue | ForEach-Object { try { Remove-Item $_.FullName -Force -ErrorAction Stop } catch {} }   # (old copies no game holds any more)
} else { "no game build (analysis\build\OpenRash.exe): the editor can open courses already extracted, but cannot extract new ones, test drive or show the game renderer" }
Start-Process $ed -ArgumentList "-data `"$data`" -game `"$play\OpenRash.exe`" -track $Track -level $Level$(if ($GameRenderer) { ' -gamerenderer 1' })" -WorkingDirectory $data
