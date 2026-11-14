# Build the SDL3-backed ddraw.dll (src\platform\ddraw\rrddraw.c) -> analysis\build\sdl\ddraw.dll (+ SDL3.dll).
# Every build counts up the build number in scripts\build_number.txt: version 0.1.<build number>, shown on the splash screen (splash.inc).
. "$PSScriptRoot\common.ps1"
$sdl = Get-RrSdl; $out = "$Build\sdl"; $src = "$Src\platform\ddraw"; $gen = "$Build\gen"
$numFile = "$PSScriptRoot\build_number.txt"; $num = 0
if (Test-Path $numFile) { $num = [int]((Get-Content $numFile -TotalCount 1).Trim()) }
$num++; [IO.File]::WriteAllText($numFile, "$num`r`n")
New-Item -ItemType Directory -Force $gen | Out-Null
[IO.File]::WriteAllText("$gen\rr_version.h", "#define RR_VERSION `"0.1.$num`"`r`n")
Enter-RrVcVars
New-Item -ItemType Directory -Force $out | Out-Null; Push-Location $out
cl /nologo /O2 /W3 /LD "/I$sdl\include" "/I$gen" "$src\rrddraw.c" "$src\jpeg.c" /Fe:ddraw.dll /link "/DEF:$src\rrddraw.def" "/LIBPATH:$sdl\lib\x86" SDL3.lib user32.lib gdi32.lib winmm.lib ole32.lib uuid.lib mfplat.lib mfreadwrite.lib mfuuid.lib 2>&1 | Where-Object { $_ -match "error|fatal|LNK" }
Copy-Item "$sdl\lib\x86\SDL3.dll" $out -Force; Pop-Location; Get-Item "$out\ddraw.dll" | Select-Object Name,Length
"version 0.1.$num"
