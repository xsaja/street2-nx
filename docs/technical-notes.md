# street2-nx technical notes

What FIFA Street 2 needed on top of mvp-nx, and why. The game: USA release, title ID `45410085`, `default.xbe` SHA-1
`9c59ced6a2e361ba7130bb0dbfdb9106026574d5`, SHA-256 `5fabb5b763be0cc8823e814ee343ac5be6950305330f46c5170e08eaadea910b`
(3.293.184 bytes), XDK 5849 (the same libraries as MVP Baseball 2005).

## Symbols named by hand

`xbe_symbols` names 123 of the 238 functions and 10 of the 12 data symbols. The ones the game calls and the runtime
replaces, but it missed, are named in `[symbols]` of `game/config/game.toml`. Each was matched by comparing its bytes
with FIFA Street's executable (addresses masked), or by its position next to a named function:

| Address | Function | How it was named |
|---|---|---|
| 0x2914B0 | `D3DDevice_SetBackBufferScale` (HLE: keeps the scale in the device) | bytes, from FIFA Street |
| 0x28FE20 | `D3DDevice_SetPalette` | bytes |
| 0x2932F0 / 0x293310 | `D3DDevice_BeginPush` / `EndPush` | bytes |
| 0x2A2174 | `XInputGetCapabilities` | bytes |
| 0x285DAB | `IDirectSoundBuffer_SetFilter` (`.XTLID` picks it over 0x285D8F) | `.XTLID` |
| 0x285D8F | `IDirectSoundBuffer_SetEG` | position |
| 0x285E43 | `IDirectSoundBuffer_Pause` (HLE, as the stream's) | position |

Still missing, as in FIFA Street: the `CDirectSoundStream` functions, DirectSound 3D, `SetLight`/`SetMaterial` and the
`crt_*` helpers. The executable also imports networking (`XNet*`, `XOnline*`, sockets) for System Link and Xbox Live,
which the port does not provide.

`tools/unnamed_api.py` lists the library functions the game calls that still have no name; `tools/crash_report.py`
names a PC crash's function from its dump.

## Recompiler

`xbe_recomp` translates 16.425 functions (818.778 instructions). Eighteen instructions are not implemented yet: `aam`,
`aas`, `bound`, `das`, `into`, `outsd`, and the MMX ones `paddw`, `pmulhuw`, `psraw`, `psubw`, `pxor`. The intro
movies do not hit them; if a movie fails, look for these first.

## EA's sound engine

The engine is the same as FIFA Street's in structure, another version. Its channel table is at `[0x350C28]+0x40`, the
ready byte at 0x350C38, the voice factory at `[0x350C70]`. Two differences matter:

- **The guard is a semaphore**, not a counter: enter at 0x23044C (`sub_0024BEDB`, `lock xadd` and a wait), leave at
  0x230477 (`sub_0024BF67`, a release). The object is at `[0x350C74]`. Its `lock xadd` is translated as a sequentially
  consistent atomic, so it holds on several cores. No compare-and-swap hook is needed here (FIFA Street's was removed).
- **A channel can keep no voice.** `sub_002302F2` creates a channel's voice and registers the channel even when the
  factory gives none; the engine then calls the voice at `+0x12C` without a check in many places. On the Xbox the
  factory never failed. `game/src/game/hooks.cpp` logs the first 16 such channels (`sound: channel X ended without a
  voice`) and makes the stop-all walk (`sub_0022FD8C`) skip the null call. If these lines appear, find out why the
  factory fails (voices running out in the emulator?) rather than adding more guards.

## Threads

The game sets its threads' priorities, and on the Xbox they all shared one processor: a low-priority loader built
objects that the main thread could not run in the middle of. On the Switch, the threads had every core and the main
thread ran the match object while the loader was still building it (a crash when the match interface loaded, at 110 s).
`sdk/runtime/kernel/threads.cpp` (`StartThread`) puts every guest thread on core 1 with `svcSetThreadCoreMask`. The
host's render, vblank and audio mixing stay on any core. Cost: all the game's code shares one core (as on the Xbox).
`NtYieldExecution` is `std::this_thread::yield()`, which on Horizon only yields to the same priority; the port sleeps
50 µs instead so a lower thread gets the core. The game does not call it in FIFA Street 2, so this is a safety net.

## Video and textures

- **The intro movies** are EA's VP6 (`data/movies/*.vp6`), decoded by the game's own code (no host decoder). They ran at
  12 to 19 fps. The profile put 60 % of the guest thread in the decoder (`sub_001C4C60` and its callees). PGO
  (below) brought it to 47 to 58 fps. Native copies of its hot functions were the next step; not needed for now.
- **Create a Zone's pitch** is painted by the game in 16-bit render targets (1024x128 linear, format 0x1C) and read back
  by `Lock2DSurface` into a texture with swizzle. The read-back of all three backends wrote 32-bit BGRA whatever the
  format, so the texture came out scrambled (a moire). `Target.gfmt` now keeps the surface's format, and
  `gpu/texture.cpp` (`TargetTexelBytes`, `TargetPitch`, `WriteTargetPixels`) writes R5G6B5, A4R4G4B4, X1R5G5B5 and
  A1R5G5B5. The 32-bit targets are unchanged.

## Occlusion queries (the Gamebreaker)

The game reads visibility tests through `D3DDevice_GetVisibilityTestResult` (`sub_0028F0A0`) and loops while the result
is `D3DERR_TESTINCOMPLETE`. Only the D3D12 backend implements `GetReport` (real occlusion queries); deko3d and Vulkan
used to leave it pending, and the Gamebreaker hung. `Backend::GetReport` (`render_thread.cpp`) now writes a report at
once: time 0, count 0x00FFFFFF, state 0, meaning "everything visible". Shortcut: no occlusion on deko3d and Vulkan,
so the count the game divides by pixels is not the real one. Upgrade when a visible-pixel count matters.

## Loading screen

The loading bar draws only its new piece over the frame before. `gpu::g_swap_keeps_back_buffer` (set in
`game_info.cpp`) copies each shown frame into the next back buffer, as `D3DSWAPEFFECT_COPY` would.

## Log and performance

- The log is written by its own thread (`core/log.cpp`), so a line never waits for the SD card under a lock.
- `frame_pacing = "vsync"`: the screen paces the game instead of a second 60 Hz clock.
- Disc files of 2 MB or less are read once and kept in memory (`kernel/files.cpp`, `SmallFile`); the game reads the same
  small files again and again during a match.
- Header groups are recorded again only when the game sets them, and a draw with the same resources as the one before
  skips marking them used (`render_thread.cpp`).
- **PGO**: `switch/CMakeLists.txt` (`STREET2_PGO` = `generate` or `use`), `switch/pgo.cpp` writes the counters to
  `sdmc:/switch/street2-nx/pgo/session-NN.stream` (every 60 s and at exit), `tools/pgo_merge.ps1` merges them into the
  `.gcda` files of `build-switch`. The guest code is built with `-fomit-frame-pointer -fno-math-errno`. The profile
  matches only the code it was taken from: after a code change, build the instrumented NRO again and play it.

## Open points

- The Gamebreaker's red or blue tint: compare with the D3D12 build, which has real occlusion queries.
- The screen's dark replay on D3D12 and sepia on Vulkan: probably the game's own fade and filter; not confirmed.
- Pauses of 3 to 4 seconds when a movie opens, and frames up to about 50 ms in the first loads: profile them.
- Triggers: L and R are the Xbox triggers in `switch/input.cpp`. Mapping them to ZL/ZR instead is under study; it
  changes the input code, so it needs a new PGO profile.

## Options left off

- **SMAA** (`smaa = true`): costs GPU time every frame.
- **Clocks**: not needed.
