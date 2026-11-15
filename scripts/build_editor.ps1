# Build the Course Editor (src\editor\courseedit.c) -> analysis\build\editor\CourseEdit.exe (+ SDL3.dll).  A Windows program of its own: it edits the
# courses the game races (road\course_edit.h) and shows them in 3D with the True 3D builder / renderer (src\platform\world3d).
param([string]$Out)   # -Out <folder>: build there (a test copy while the one in the default folder is open)
. "$PSScriptRoot\common.ps1"
$sdl = Get-RrSdl; $out = if ($Out) { $Out } else { "$Build\editor" }
Enter-RrVcVars
New-Item -ItemType Directory -Force $out | Out-Null; Push-Location $out
cl /nologo /O2 /W3 "/I$sdl\include" "$Src\editor\courseedit.c" /Fe:CourseEdit.exe /link /SUBSYSTEM:WINDOWS "/LIBPATH:$sdl\lib\x86" SDL3.lib user32.lib gdi32.lib comctl32.lib ole32.lib uuid.lib dxguid.lib 2>&1 | Where-Object { $_ -match "error|LNK" }
Copy-Item "$sdl\lib\x86\SDL3.dll" $out -Force; Pop-Location; Get-Item "$out\CourseEdit.exe" -ErrorAction SilentlyContinue | Select-Object Name,Length
