# Open Rash Next on the Steam Deck

Open Rash Next is a Windows program, so the Steam Deck runs it through **Proton** (Steam's Windows compatibility layer). It is
built on a Windows PC and the finished folder is copied to the Deck. Nothing needs to be installed on the Deck itself.

You still need your own copy of *Road Rash* for Windows 95 (see [readme.md](readme.md)); its content is not part of this repository.

## What was changed in the code for the Steam Deck

The game ran on Windows but not under Proton. These are the changes that fixed it, with the problem each one solved.

### 1. The DLLs renamed: `ddraw.dll` / `dsound.dll` became `rrddraw.dll` / `rrdsound.dll`

**Problem:** the game's graphics and sound layers were DLLs named after the system ones, `ddraw.dll` and `dsound.dll`, so that
the old exe would load them in place of DirectDraw and DirectSound. Proton (Wine) has its own built-in `ddraw.dll` and `dsound.dll`
and loads those first, ignoring the copies next to the exe. The game then ran on Wine's DirectDraw: menu pictures broke, the
3D world was missing, and Windows-only behaviour the shims had removed came back (for example the native "You have a game in
progress that has not yet been saved" message box). The `WINEDLLOVERRIDES="ddraw,dsound=n,b"` launch option was the workaround,
and it did not take effect on the Deck.

**Change:** names that Windows and Wine do not have, so the game's own DLLs are always loaded and no launch options are needed:

- `scripts\build_ddraw.ps1` / `scripts\build_dsound.ps1`: build `rrddraw.dll` / `rrdsound.dll` (`/Fe:`), and the `.def` files say
  `LIBRARY rrddraw` / `LIBRARY rrdsound`.
- `scripts\link.ps1`: the game no longer links against the system `ddraw.lib` / `dsound.lib`. It imports `DirectDrawCreate` /
  `DirectSoundCreate` through the import libraries the shim builds produce (`analysis\build\sdl\rrddraw.lib` / `rrdsound.lib`).
  An import library made from the `.def` alone gave the decorated name `_DirectDrawCreate@12`, which does not link. If the import
  libraries are missing, `link.ps1` builds the shims first, and `scripts\build_quick.ps1` now builds the shims before the game.
- Every `GetModuleHandleA("ddraw.dll")` / `GetModuleHandleA("dsound.dll")` in the game and the shims (music, jukebox, sound mix,
  splash sounds, weather sounds, video playback, the picture loader) now names `rrddraw.dll` / `rrdsound.dll`.

### 2. The shader compiler under its own name: `rr_d3dcompiler_47.dll`

**Problem:** the True 3D renderer compiles its HLSL shaders at start-up with `d3dcompiler_47.dll`. Proton has a built-in copy of
that DLL too and loads it ahead of one next to the exe. It is Wine's own, separate compiler, not Microsoft's, so the shaders would not
be compiled by the same compiler they are tested with on Windows.

**Change:** `src\platform\world3d\render3d.h` first loads `rr_d3dcompiler_47.dll`: Windows' own 32-bit compiler
(`C:\Windows\SysWOW64\d3dcompiler_47.dll`), shipped next to the exe under a name Proton has no built-in copy of. If that file is
missing it falls back to `d3dcompiler_47.dll`.

### 3. One JPEG decoder on every platform: stb_image instead of Windows' WIC

**Problem:** the menu, story and postcard pictures (the game's `.RRI` files) are JPEGs. They were decoded with Windows Imaging
Component (WIC). Under Proton, WIC decoded some of them to black, so the menus showed their text on black backgrounds.

**Change:** `src\platform\ddraw\jpeg.c` (it replaces `wicjpeg.c`) decodes with
[stb_image](https://github.com/nothings/stb) (`stb_image.h` v2.30, public domain / MIT, JPEG only), the same code on Windows and
on the Deck. The shim exports `RR_DecodeJpeg(data, size, &w, &h)` (24-bit BGR pixels, rows top to bottom) and `RR_FreeImage`.
`src\assets\imgload.c` (the game's `RriLoad`, which replaced the original IJG libjpeg decoder) finds them with
`GetProcAddress(GetModuleHandleA("rrddraw.dll"), ...)`. When a picture fails to load it now says why in the log (`RR_LOG`): no
decoder export, cannot open the file (with the error code), cannot read it, cannot decode it.

The menu test's golden screens were recorded again: the screens are the same, but stb_image's pixels differ slightly from WIC's,
so the picture hashes changed.

### 4. Fonts shipped with the game

**Problem:** the menus and the HUD asked Windows for Impact, Arial and Bahnschrift. The Deck has none of them, so the text came out
in a fallback font with different sizes.

**Change:** `RrFont()` in `src\platform\ddraw\rrddraw.c` loads open-licence look-alikes from `fonts\` (in the game folder, or next to the
exe) with `AddFontResourceExA(..., FR_PRIVATE, 0)`, for the game's process only and nothing installed: **Anton** for Impact,
**Arimo** (regular and bold, Arial's metrics) for Arial, **Barlow Condensed** Medium / SemiBold for Bahnschrift. The same fonts are used
on Windows, so both look alike. If a file is missing, the original Windows font is used.

### 5. The controller under Gamescope

**Problem:** on the Deck the keyboard and trackpad worked but the controller did nothing. The controller code only reads the pad
while the game's window is in the foreground. Under Gamescope / Proton `GetForegroundWindow()` usually returns `NULL`, or a window
of the game other than the main one, so the pad was always ignored.

**Change:** `RrOurFocus()` in `rrddraw.c`, used by `src\platform\ddraw\controller.inc`: no foreground window, or any window that
belongs to the game's process, counts as focused. (On Windows "no foreground window" only happens for a moment.) The pad thread
also logs what it sees every 5 seconds (pad connected, buttons held, the foreground window and its process, read or ignored).

### 6. No settings in the registry

The game keeps all its settings and saves in files in its folder (`video.ini`, `controller.ini`, `RoadRash.cfg`, `*.RRS`). It does not
read or write the registry, so a fresh Proton prefix behaves the same as a Windows PC. The leftover `data\Road Rash.reg` was
deleted.

### 7. Steam Deck detection and the graphics presets

**Change:** `VidOnDeck()` in `src\platform\ddraw\winscale.inc` detects a Deck: the `SteamDeck=1` environment variable that Steam
sets, or the DMI product name `Z:\sys\devices\virtual\dmi\id\product_name` (Proton maps the Linux root to `Z:`) containing
`Jupiter` (LCD Deck) or `Galileo` (OLED Deck). On a Deck the defaults are the **Steam Deck** preset (`VidGfxPreset(1)`): near draw
distance and every optional True 3D effect off. Each True 3D effect has its own row under Video Options > True 3D detail, so a player
can turn back on what the Deck can afford. `video.ini` stores `gfx_device` (0 = PC, 1 = Steam Deck). If a `video.ini` made on the
other kind of machine is copied over, its detail settings are ignored and the local preset is used.

### 8. Speed (the Deck's CPU)

The game is capped at 60 fps (its logic ticks at 60 Hz). On the Deck it ran at ~50 fps whatever the GPU clock or TDP limit, so
the limit was the CPU: one core runs the game and the shim's per-frame work. Changes:

- **Grass:** the tufts' ground triangles are indexed by road piece (`g_w3grStart`), so each frame only looks at the pieces in view
  (it was ~3 ms a frame walking all 85,000 triangles in the City, now ~0.3 ms).
- **Frame copy** (`GpuCompose` in `src\platform\ddraw\gpuscene.inc`, the game's 8-bit frame turned into the background and
  overlay pictures): two buffers made once instead of 2 x 1.2 MB `malloc`'ed every frame, and each row copied in three runs
  (left of the 3D view, the view, right of it) instead of a test per pixel: ~1.9 ms down to ~0.7 ms a frame on a desktop CPU.
- **Live stretch rebuild** (`W3Live` in `src\platform\ddraw\world3d.inc`, the 3D mesh of the road ring round the camera): rebuilt
  every 8 road pieces instead of every piece. It covers 7 pieces more behind the camera, so it always holds the ring. Average
  ~0.9 ms down to ~0.35 ms a frame. (`RR_W3LIVEQ=1` brings back the old behaviour, for comparison.)
- **Building shadows** (a sun shadow map: one more render of the world each frame) were removed.
- **Profiler:** `RR_PROFILE=1` logs, every 300 frames, the average and worst time of each stage of a frame (CPU) and of the 3D
  passes (GPU timestamp queries), to find what costs the most on the Deck.

### Testing under Proton without the Deck

The fixes above were checked under Proton on a Windows PC: WSL2 (Ubuntu 24.04), [umu-launcher](https://github.com/Open-Wine-Components/umu-launcher)
1.4.4 with GE-Proton and the Steam Linux Runtime, software Vulkan (lavapipe) and a virtual X display (Xvfb). The game folder has to be
copied into the Linux file system: Proton cannot use a `/mnt/c/...` path as the game's drive. With no `WINEDLLOVERRIDES` set it
loads the game's own DLLs, renders True 3D and decodes the pictures, the same as on the Deck.

---

# Playing it on the Deck

## 1. Make the game folder (on the Windows PC)

Build the game (see "Building" in [readme.md](readme.md)):

```powershell
scripts\build_quick.ps1
```

Then make one folder (for example `C:\Games\roadrashnext`) that holds:

| From | Into the folder |
|---|---|
| your game folder `data\` (`AUDIO\`, `DATA\`, `IMAGES\`, `TEXT\`, `VIDEO_UP\`, ..., plus `fonts\` and `mod\` if you have them) | as it is |
| `analysis\build\OpenRash.exe` | `OpenRash.exe` |
| `analysis\build\sdl\rrddraw.dll`, `rrdsound.dll`, `SDL3.dll` | as they are |
| `C:\Windows\SysWOW64\d3dcompiler_47.dll` (the 32-bit shader compiler) | renamed to **`rr_d3dcompiler_47.dll`** |

Leave these out:

- `RoadRash.exe` / `RashMe.exe` (the original game; Open Rash Next does not use them).
- `video.ini` and `RoadRash.cfg` from your PC: they hold your PC's settings. Without them the Deck starts with its own (see section 4).
- Crash dumps, logs (`crash.*`, `rr_log.txt`), test saves and the `screenshots` folder.
- The old `ddraw.dll` / `dsound.dll` / `d3dcompiler_47.dll` from before version 0.1.105, if your folder still has them. Under Proton
  those names load Wine's own DLLs instead of the game's, which gives a black or broken picture.

Why the DLL names matter: Proton puts its own built-in `ddraw.dll`, `dsound.dll` and `d3dcompiler_47.dll` ahead of any copy next
to the exe. The game's are named `rrddraw.dll` / `rrdsound.dll` / `rr_d3dcompiler_47.dll` so Proton loads them as they are, with
no `WINEDLLOVERRIDES` launch option.

### Fonts

The menus use Anton, Arimo and Barlow Condensed. Windows usually has look-alikes; the Deck does not. Put these files in
`data\fonts\` (they are free fonts from [Google Fonts](https://fonts.google.com), under the SIL Open Font License / Apache 2.0):

```
Anton-Regular.ttf  Arimo-Regular.ttf  Arimo-Bold.ttf  BarlowCondensed-Medium.ttf  BarlowCondensed-SemiBold.ttf
```

The game loads them from there at start-up, for itself only. Nothing is installed.

## 2. Copy it to the Deck

Any of these works:

- **USB stick / microSD card:** copy the folder over, then in Desktop Mode move it to, for example, `/home/deck/Games/roadrashnext`.
- **Network:** a shared folder on the PC, opened in Desktop Mode's file manager (Dolphin), or a tool such as Warpinator or KDE Connect.

The folder is about the size of your game data plus ~10 MB. Copy the whole folder again for each new build, or just the four
files from `analysis\build` if only the code changed.

## 3. Add it to Steam

In **Desktop Mode**:

1. Open Steam, then **Games > Add a Non-Steam Game to My Library > Browse**, and pick `OpenRash.exe` in the folder.
2. In the library, right-click it, then **Properties**:
   - **Start In:** the folder that holds `OpenRash.exe`, for example `"/home/deck/Games/roadrashnext/"`. The game looks for its data,
     settings and saves in the folder it starts in.
   - **Launch Options:** leave this empty. (`RR_LOG=rr_log.txt %command%` writes a log next to the exe, which helps if something goes wrong.)
3. **Compatibility:** tick "Force the use of a specific Steam Play compatibility tool" and choose **Proton Experimental**
   (GE-Proton works too).

Go back to **Gaming Mode** and start it from the library (under Non-Steam).

## 4. Settings on the Deck

- **Graphics preset:** the game detects a Steam Deck and picks the **Steam Deck** preset under Video Options > True 3D detail
  (the near draw distance, every optional effect off, the 3D picture at full resolution). Change any row there to turn effects back
  on (they apply from the next race). The **High (RTX 3080)** preset is for desktop PCs.
- `video.ini` stores the machine its settings were made on (`gfx_device`). If you copy a PC's `video.ini` to the Deck anyway,
  the Deck ignores its detail settings and uses the Steam Deck preset.
- Settings and saves stay in the game folder: `video.ini`, `controller.ini`, `RoadRash.cfg` and the `*.RRS` saved games.
  The game does not use the Windows registry.

## 5. Controls

The Deck's controls appear to the game as an Xbox controller (through Steam Input), so the default **Gamepad** controller layout
works:

- **Menus:** D-pad / left stick to move, **A** or **Start** to choose, **B** or **Back** to go back.
- **Races:** set each action (accelerate, brake, turn, lean, nitro, swing, kick, backhand, dismount) to a button under
  **Game Options > Configure Input**. The D-pad also steers, accelerates and brakes; **Start** pauses. The bindings are saved in `controller.ini`.
- The game only reads the controller while its window has focus. In Gaming Mode it always has focus.

## 6. Frame rate

The game runs at **60 frames a second at most**, as the original did (its race logic ticks 60 times a second). If the Deck
shows less than 60:

- Stay on the Steam Deck preset. **Draw distance** costs the most, then **Grass**, **Window interiors** and **Puddle reflections**.
- Changing the GPU clock or the TDP limit in the quick-access menu changes little: the limit is the CPU (one core runs the game
  and builds the 3D world).
- To see where the time goes, set Launch Options to `RR_PROFILE=1 RR_LOG=rr_log.txt %command%`, play a race, then read
  `rr_log.txt`: every 300 frames it logs the frame rate and the time each stage took (`game`, `present`, `w3 draw`, `live`,
  `grass`, `frame copy`, ..., and the GPU's `world` time) in milliseconds.

## 7. Troubleshooting

| What you see | Cause and fix |
|---|---|
| Black screen or broken picture | An old `ddraw.dll` / `dsound.dll` / `d3dcompiler_47.dll` is in the folder. Delete them; the game needs `rrddraw.dll`, `rrdsound.dll`, `rr_d3dcompiler_47.dll`. |
| No 3D world in races, only the sky / HUD | `rr_d3dcompiler_47.dll` is missing: copy `C:\Windows\SysWOW64\d3dcompiler_47.dll` under that name. |
| Menu pictures black, text shows | An old build (before 0.1.105 it used a Windows-only picture decoder). Copy the new `OpenRash.exe` and `rrddraw.dll`. |
| Plain or wrong fonts in the menus | `data\fonts\` is missing (see "Fonts" above). |
| The game closes at once or reports missing files | **Start In** does not point at the game folder, or `AUDIO\`, `DATA\`, ... are not in it. |
| The controller does nothing, the touchscreen / trackpad does | Check that Steam Input is on for the game (controller icon in its properties) and uses a Gamepad layout. With `RR_LOG=rr_log.txt` the log has a line about the controller every 5 seconds. |
| Settings from the PC show up on the Deck | A PC `video.ini` was copied. Delete it, or ignore it: the Deck uses its own preset (see section 4). |
