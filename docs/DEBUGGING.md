# Debugging and Upgrade Notes

This document records the runtime switches, profile counters, and extension
points that are useful when adding support for more Dingoo A320 handheld and
compatible Gemei X760+ handheld `.app` samples. The `.app` package format
belongs to Dingoo Technology; samples are external test inputs.

## Runtime Switches

The Windows frontend also writes `DingooPie.ini` next to `DingooPie.exe`.
The INI reader accepts UTF-16LE with BOM, UTF-8 with or without BOM, and a
system ANSI fallback, so manually edited Chinese app paths remain loadable.
The executable is named `DingooPie.exe`; its Windows version resource reports
`Dingoo Game Emulator`, product name `DingooPie`, and
`Copyright (c) 2026 BL2CK`.
Starting without a game argument does not show a file picker. Empty
`recent.last_app` opens the frontend only; an existing `recent.last_app` is
auto-loaded; a command-line `.app` or `.cc` path takes priority.
Selecting a game from `File -> Open Game`, choosing `File -> Recent Games`,
or dropping an `.app` or `.cc` file onto the window saves the chosen UTF-8 path to
`recent.last_app` and promotes it into `recent.app1` through `recent.app10`.
`File -> Recent Games -> Clear Recent Games` clears both `recent.last_app` and
the ordered `recent.app1`...`recent.app10` list.
Automatic `recent.last_app` startup clears the matching recent entry when the
path is missing, does not end in `.app` or `.cc`, or fails during package open/parse.
Command-line startup failures are diagnostic-only and do not modify
`DingooPie.ini`.

## Command-Line Options

The supported syntax is:

```text
DingooPie.exe [options] [game.app|game.cc]
```

| Option | Behavior |
| --- | --- |
| `-g, --game <path>` | Start one APP or CC game. `--game=<path>` is also accepted. |
| `-c, --config <path>` | Use the specified settings file. `--config=<path>` is also accepted. |
| `--no-recent` | Skip recent-game auto-start without clearing the recent list. |
| `-h, --help`, `/?` | Print usage. |
| `-V, --version` | Print the product name and version. |
| `--core-regression` | Run built-in core and command-line parser regression tests. |
| `--` | Treat the following value as the game path. |

Specify one game either directly or with `--game`, and quote paths containing
spaces. Without `--config`, the default remains `DingooPie.ini` beside the
executable. An explicit game takes priority over the recent-game list.
The menu is ordered as File, Options, Settings, Debug, and Help. Options
contains Video, Audio, and Input submenus. Video settings are scale, fullscreen,
anti-aliasing, effect, brightness, contrast, gamma, saturation, minimized
behavior, screen orientation, screen fill, and FPS overlay. Audio settings also
include buffer latency; Input includes virtual-control scale, D-pad type,
mapping, and controller calibration. Runtime settings, the Cheats submenu,
Cheat Manager, UI language, and Debug menu items follow in this
order:
Show Debug Console, Performance Log, Open Debug Log, Resource Monitor, Memory
Searcher, and Debugger. File > Pause Game/Resume Game freezes game execution and
audio at frame boundaries, but it is runtime-only and is not written to
`DingooPie.ini`.
`DingooPie.ini` is rewritten in the same practical order:
`recent`, `video`, `audio`, `input`, `runtime`, optional `cheats`, `ui`, then
`debug`. `settings-trace` prints the same section order when debug output is
enabled.
The `recent` section writes `last_app` first, followed by the ordered
`app1`...`app10` recent-game list.
Runtime-affecting values are saved immediately. Changes to window scale,
fullscreen, minimized behavior, screen orientation, screen fill, FPS overlay, CPU clock, Game Speed, System Delay Scale,
audio disable, performance logging, Resource Monitor auto-open,
anti-aliasing/effect, brightness, contrast,
gamma, saturation, IME disable mode, virtual controls, language, master volume, audio
buffer size, buffer latency, audio effect, digital noise reduction, and debug console
visibility apply without relaunching the guest.
Changing CPU Execution Mode still relaunches the emulator because the execution
backend is selected at startup. Restored defaults relaunch only when they change
the active CPU backend.
`audio.volume_percent` stores the emulator-side master volume. It is applied
after the guest `waveout` volume so game-internal volume changes remain active.
`audio.buffer_samples` controls the SDL output buffer request. The default is
`2048`; larger values reduce underruns at the cost of more audio latency, while
smaller values reduce latency but can make weak hosts crackle.
`audio.buffer_latency` accepts `auto`, `110ms`, `120ms`, `130ms`, `140ms`, or
`150ms`. `auto` currently uses the balanced 130 ms target. This setting controls
the queued-audio target independently of `audio.buffer_samples`.
`audio.effect` controls lightweight PCM processing after guest audio is written
and before master volume is applied. Valid values are `off`, `soft`, `clear`,
`bass_boost`, and `mono`; the default is `off`.
`audio.digital_noise_reduction` accepts `high`, `medium`, or `low`; the default
is `high`. It controls the strength of the frontend PCM noise-reduction pass.
`audio.audio_disabled=1` disables guest audio output; the default is `0`.
`video.minimized_behavior` accepts `normal`, `throttle`, or `pause`. The default
`pause` automatically pauses game execution and audio while minimized, then
resumes only pauses caused by minimization. `throttle` keeps the runtime active
with reduced frontend presentation and loop cadence.
The default UI language is Chinese. `Settings -> Language` persists
`ui.language=english` or `ui.language=chinese` for menus and native file dialogs.
`runtime.speed_scale=` means `Auto`; the frontend leaves
`DINGOO_PIE_RUNTIME_SPEED_SCALE` unset so the runtime uses the global 65%
Auto pace.
`runtime.cpu_hz=` means `Auto`; explicit CPU clock menu values set
`DINGOO_PIE_IRJIT_CLOCK_HZ`. Despite the legacy environment-variable name,
the value is the guest CPU clock reference used by both APP and CC runtimes.
`runtime.backend=` means `Auto`: APP maps to PPSSPP IR JIT and CC maps to
Dynarmic when the optimized backends are compiled. Compatibility Mode selects
the matching in-tree interpreter.
`runtime.ostimedly_scale=` means `Auto`, which maps host SDK delay waits to
the global 1.0 SDK delay default while explicit values preserve manual
accuracy/performance choices.
`runtime.cheats_enabled=1` enables runtime cheat-code application. Cheat files
are loaded from a `cheats` directory next to `DingooPie.exe` by game filename:
`GameName.app` loads `cheats\GameName.app.cht`, while `GameName.cc` loads
`cheats\GameName.cc.cht`. If that format-specific file is absent, lookup falls
back to the legacy `cheats\GameName.cht` filename. The optional
`app_sha256=` field inside the `.cht` file is validation only. The global cheat
switch is disabled by default; the menu item is `Settings -> Cheats -> Enable Cheats`.
Individual cheat features start unchecked until the user selects them under
`Settings -> Cheats`. Selected features are saved per game and restored
when the same game loads again. The same global switch can be forced with
`DINGOO_PIE_CHEATS=1`.
Saved feature selections are written in the optional `[cheats]` section between
`[runtime]` and `[ui]`.
Cheat feature rows are shown under `Settings -> Cheats`. The menu groups
multiple low-level rows by the text before `:` or `：`, so names such as
`解锁所有赛车/Unlock All Cars：patch 1` and
`解锁所有赛车/Unlock All Cars：patch 2` appear as one localized feature.
Use `中文/English` before the group separator when a feature needs both Chinese
and English menu labels.
Startup does not create `DingooPie.ini`; the file is written only after the user
changes or resets settings.
`input.system_ime_disabled=1` is the default and disables the Windows IME for the SDL
window so input methods cannot intercept gameplay keys. It can be toggled from
`Input -> Disable IME` and applies immediately.
`input.show_virtual_controls` enables the on-screen controls.
`input.virtual_control_scale` accepts `75`, `100`, `125`, or `150`, while
`input.virtual_dpad_type` accepts `joystick` or `segmented_ring`; the defaults
are `100` and `joystick`.
SDL GameController-compatible pads are accepted by the frontend. D-pad and left
stick feed Dingoo D-pad controls, A/B/X/Y feed the matching face buttons,
Back/Start feed SELECT/START, and shoulder buttons or analog triggers feed the
left/right shoulder controls by default. Custom keyboard and controller
bindings are saved in `input.keyboard_mapping` and `input.controller_mapping`
as comma-separated `Physical=Control` pairs; empty means default. Supported
controller physical names include `A`, `B`, `X`, `Y`, `Back`, `Start`,
`LeftShoulder`, `RightShoulder`, `DPadUp`, `DPadDown`, `DPadLeft`, `DPadRight`,
`LeftX-`, `LeftX+`, `LeftY-`, `LeftY+`, `RightX-`, `RightX+`, `RightY-`,
`RightY+`, `LeftTrigger`, and `RightTrigger`. Supported controls are `A`, `B`,
`X`, `Y`, `Start`, `Select`, `L`, `R`, `Up`, `Down`, `Left`, `Right`, `Power`,
and `None`.
`input.controller_calibration` stores the calibration result produced by the
Input Mapping window. An empty value uses the built-in axis ranges and dead zone;
the calibration and restore-default actions update this field automatically.
The executable is built as a Windows GUI app by default, so no console is
shown unless `Debug -> Show Debug Console` or `debug.show_console=1` is enabled.
`Debug -> Open Debug Log` checks the executable directory first and
shows a localized message if the log file has not been created yet. The
persisted Performance Log setting and `DINGOO_PIE_LOG_FILE=1` create that file
next to `DingooPie.exe`.
Debug logs are named `DingooPie-debug-<timestamp>-<pid>.log`; the menu opens
the current instance's file.
Guest runtime failures also write a separate `DingooPie-crash-<timestamp>-<pid>.log`
next to the executable. The regular debug log keeps only the failure summary and
the crash-log file name.
Structured log lines use compact prefixes and fields, such as
`profile:frontend loops=60/s` and
`debug-log:opened file=DingooPie-debug-20260717-120000-1234.log`.
`Debug -> Resource Monitor` opens the live resource list. It is enabled while a
game is running, or can be checked before launch to auto-open once the next game
starts. The persisted setting is `debug.resource_monitor_auto_open`.
The upper list shows loaded entries and the lower list keeps unloaded entries.
The status line reports read count and read bytes. First/new loads highlight
light green, repeated loads highlight light yellow, and closed resources
highlight light red before leaving the list.
`Debug -> Memory Searcher` searches u8/u16/u32 values, narrows candidates with
equal, increased, decreased, and unchanged filters, writes a selected address,
and copies a selected result as a `.cht` record for the matching cheat file.
It is enabled only while a game is running.
`Debug -> Debugger` opens a live inspection panel for the active runtime. It
shows PC-based MIPS disassembly, all GPR/HI/LO registers, a hex memory viewer,
PC hit counters, and write hits. Use write hits on candidate
addresses from Memory Searcher to identify the PC that changes health, score, or
other values. PC hits and write hits record hits but do not pause or
single-step the CPU. It is enabled only while a game is running.

Debug-related INI keys and environment variables use the same order as the
Debug menu:

| Debug menu item | INI / environment variable | Purpose |
| --- | --- | --- |
| Show Debug Console | `debug.show_console=1` | Shows the Win32 debug console for the current run. |
| Performance Log | `debug.profile=1`, `DINGOO_PIE_PROFILE=1`, `DINGOO_PIE_PROFILE_EMPTY=1`, `DINGOO_PIE_PROFILE_INTERVAL_MS=<ms>` | Enables low-frequency performance counters and optional empty-window output. |
| Open Debug Log | `DINGOO_PIE_LOG_FILE=1` | Forces creation of `DingooPie-debug-*.log` so the menu has a file to open; unset or `0` disables it. |
| Resource Monitor | `debug.resource_monitor_auto_open=1`, `DINGOO_PIE_RESOURCE_MONITOR_AUTOTEST=1` | Persists Resource Monitor auto-open or captures resource events for automated tests. |
| Memory Searcher | `DINGOO_PIE_MEMORY_SEARCHER_AUTOTEST=1` | Enables Memory Searcher automation hooks. |
| Debugger | `DINGOO_PIE_DEBUGGER_AUTOTEST=1` | Enables Debugger automation hooks. |
Current video effects are frontend-side only: SDL provides nearest/linear
texture scaling. Anti-aliasing uses nearest sampling for off, linear scaling
for low strength, and linear scaling plus a light CPU RGB565 clarity pass for
clear mode. Unknown or invalid INI values fall back to current defaults instead
of being specially mapped.
Color effects are frontend-only presentation effects. Grayscale, invert, soft
blur, sharpen, vivid, sepia, LCD scanline, and light CRT are CPU RGB565
post-processes before texture upload. Pixel grid is a display-size
overlay that darkens source-pixel boundaries after scaling and is also applied
to saved screenshots. Brightness, contrast, and saturation adjustments are
applied after the selected pixel effect and are also reflected in saved
screenshots. The guest framebuffer is not modified.
`video.brightness`, `video.contrast`, `video.gamma`, and `video.saturation`
accept `50`, `75`, `90`, `100`, `110`, `125`, or `150`; all default to `100`.
`video.show_fps=1` enables the frontend FPS overlay and defaults to `0`.
`video.screen_orientation` accepts `auto`, `landscape`, or `portrait`. Portrait
rotates the SDL presentation and saved screenshots 90 degrees counter-clockwise,
swaps the non-fullscreen window to 240x320 at the selected scale, and rotates
virtual-control rendering and hit testing. Auto uses portrait only when the
current renderer output is taller than it is wide. The guest framebuffer remains
the fixed Dingoo 320x240 surface.
`video.screen_fill` accepts `aspect`, `blurred`, or `stretch`. Aspect preserves
the source ratio, blurred extension fills unused edges from a blurred copy of
the current frame, and stretch fills the complete renderer output.
Saved screenshots use the current SDL display output size, so a 2x window saves
640x480 in landscape mode and 480x640 in portrait mode.
Window scale values are limited to 1, 2, or 3 in `DingooPie.ini`; old or invalid
values fall back to the current default when settings are loaded. Fullscreen is a
separate `video.fullscreen=1` option implemented as a maximized SDL window, so
the native top menu bar remains visible and can still be used to leave
fullscreen. The SDL window is internally resizable so the frontend can fit the
monitor work area exactly, but the native maximize button is still hidden.

## Host Input Map

| Host key | Dingoo A320 / Gemei X760+ control |
| --- | --- |
| `WASD` / arrow keys | D-pad |
| `L` | A |
| `K` | B |
| `I` | X |
| `J` | Y |
| `1` / `Q` | SELECT |
| `0` / `O` | START |
| Left Shift | Left shoulder |
| Right Shift | Right shoulder |
| Esc | Exit emulator |
| F12 | Save automatic screenshot |

`Enter` is intentionally unmapped so it cannot conflict with game input.
The virtual D-pad also exposes four diagonal corner buttons; each one presses
the matching pair of cardinal directions through the same synthetic input path.
Internal control names follow the Dingoo A320 SDK. Gemei X760+ face-button
input is mapped onto the same A/B/X/Y control bits; Dingoo-only START, SELECT,
and shoulder controls remain available through host and virtual controls for
compatibility.

## App Path Handling

Host file loading accepts `.app` extensions case-insensitively, so paths such as
`AliBaba.APP` and `AliBaba.app` refer to the same sample on Windows. The real
host path is preserved for opening the file, but the filename passed into the
guest `AppMain` argument is normalized to a lowercase `.app` extension because
some games compare that string directly.

| Variable | Values | Purpose |
| --- | --- | --- |
| `DINGOO_PIE_BACKEND` | `ppsspp_irjit`, `irjit`, `interpreter` | Overrides the main CPU backend for the current process. The default is `ppsspp_irjit`. |
| `DINGOO_PIE_SUBTASK_BACKEND` | `interpreter`, `ppsspp_irjit` | Selects the backend for host pthread-backed Dingoo tasks. Keep the default `interpreter` until PPSSPP global state is made thread-local. |
| `DINGOO_PIE_PROFILE` | `1` | Enables the Debug > Performance Log counters without changing `DingooPie.ini`. |
| `DINGOO_PIE_PROFILE_EMPTY` | `1` | Prints empty profile windows; by default profile logs skip no-activity windows. |
| `DINGOO_PIE_PROFILE_INTERVAL_MS` | milliseconds | Sets the Performance Log sample interval. |
| `DINGOO_PIE_LOG_FILE` | `1` | Forces debug-log file creation for Debug > Open Debug Log without enabling the debug console; unset or `0` disables it. |
| `DINGOO_PIE_RESOURCE_MONITOR_AUTOTEST` | `1` | Captures Resource Monitor data during automation without opening the UI. |
| `DINGOO_PIE_MEMORY_SEARCHER_AUTOTEST` | `1` | Enables Memory Searcher automation hooks. |
| `DINGOO_PIE_DEBUGGER_AUTOTEST` | `1` | Enables Debugger automation hooks. |
| `DINGOO_PIE_COMPAT_TRACE` | `1` | Prints unique compatibility hook addresses for Dingoo break/cache instructions. |
| `DINGOO_PIE_IRJIT_TRACE` | `1` | Prints detailed PPSSPP shim diagnostics. This is noisy. |
| `DINGOO_PIE_IRJIT_FASTMEM` | `0` | Disables direct fast-memory pages for diagnostics. |
| `DINGOO_PIE_IRJIT_DISABLE_FLAGS` | `none`, `default`, numeric, or comma-separated flag names | Overrides PPSSPP IR JIT disable flags. The default enables block linking and disables only cache pointer and pointerify transforms. |
| `DINGOO_PIE_IRJIT_THROTTLE` | `0`, `1` | Enables guest-clock throttling for the IR JIT. Disabled by default because SDK timers are HLE-driven and wall-clock throttling can stall software-rendered samples. |
| `DINGOO_PIE_IRJIT_CLOCK_HZ` | `1000000..1000000000` | Overrides the guest CPU clock for the current process. The default is the Dingoo-style 336 MHz clock. |
| `DINGOO_PIE_IRJIT_THROTTLE_AHEAD_MS` | `0..5000` | Allows the JIT to run ahead of wall time before sleeping when `DINGOO_PIE_IRJIT_THROTTLE=1`. The default is 1000 ms. |
| `DINGOO_PIE_IRJIT_THROTTLE_MAX_LAG_MS` | `1..5000` | Resets the throttle baseline after long host stalls so delayed input or loading does not cause a catch-up burst. |
| `DINGOO_PIE_DISPLAY_FPS` | `1..240` | Limits SDL texture uploads and presentations without blocking guest execution. The default is 60. |
| `DINGOO_PIE_LCD_FRAME_PACING` | `0`, `1` | Enables adaptive pacing at Dingoo LCD frame submission boundaries. The default is enabled; set `0` to diagnose raw guest frame production. |
| `DINGOO_PIE_AUDIO_QUEUE_DROP_MS` | `0..60000` | Drops guest PCM buffers after the audio queue stays full for this many milliseconds. The default `0` waits for playback so saturated queues preserve audio timing. |
| `DINGOO_PIE_AUDIO_QUEUE_TRACE` | `1` | Logs audio queue backpressure waits when `DINGOO_PIE_AUDIO_QUEUE_DROP_MS=0`. This is noisy during games that stream audio near the queue limit. |
| `DINGOO_PIE_RUNTIME_SPEED_SCALE` | `0.1..1.0` | Overrides runtime pacing for the current process. The menu supports `0.2..1.0`; `Auto` maps to the global 65% runtime pace. |
| `DINGOO_PIE_OSTIMEDLY_SCALE` | `0.1..1.0` | Overrides host sleep scaling for `OSTimeDly`, `delay_ms`, and `udelay` for the current process while preserving guest tick accounting. Auto uses the global 1.0 delay scale unless a content-hash compatibility entry overrides it. |
| `DINGOO_PIE_AUDIO_DISABLED` | `0`, `1` | Overrides audio output disabling for the current process without changing `DingooPie.ini`. |
| `DINGOO_PIE_CHEATS` | `1` | Enables loaded cheat files without changing `DingooPie.ini`. Cheat files use `status|name|width|address|value` or `status|name|width|address|value|compare` pipe records; see "Cheat File Format" below. |
| `DINGOO_PIE_CHEAT_DIR` | path | Overrides the directory used for `.cht` files. |
| `DINGOO_PIE_CHEAT_TRACE` | `1` | Prints cheat loading and apply counters. |
| `DINGOO_PIE_AUTOTEST_QUIT_MS` | milliseconds | Requests a clean frontend exit after the specified delay for PC lifecycle regression tests. |
| `DINGOO_PIE_IRJIT_SLICE` | `10000..10000000` | Overrides the PPSSPP shim slice length. Useful for timing experiments only. |
| `DINGOO_PIE_INPUT_TRACE` | `1` | Prints SDL key events and Dingoo key state reads. |
| `DINGOO_PIE_AUTOPRESS_KEYS` | `KEY:DELAY_MS:COUNT:PERIOD_MS:HOLD_MS` | Injects deterministic synthetic controls from inside the frontend for automated sample tests. Example: `A:6000:8:900:300`. Keys: `A`, `B`, `X`, `Y`, `U`, `D`, `L`, `R`, `SELECT`, `START`. |
| `DINGOO_PIE_AUTOPRESS_SEQUENCE` | `KEY@DELAY_MS:HOLD_MS,...` | Injects a deterministic multi-key sequence for multi-screen startup flows. Example: `A@6000:250,A@9000:250,D@12000:300`. The key names are SDK controls, not host keyboard letters. Raw SDK key aliases are also accepted for compatibility diagnostics: `ENTER`, `AB`, `EQ`, `CAMERA`, and `MENU`. |
| `DINGOO_PIE_FORCE_GUEST_CRASH` | `1` | Forces the guest-runtime failure path after initialization so crash-log output can be validated with a known sample. |
| `DINGOO_PIE_TASK_PROFILE` | `1` | Adds a per-subtask instruction hook profile. This can slow execution. |
| `DINGOO_PIE_TRACE_HLE` | `1` | Prints selected resource and HLE calls. |
| `DINGOO_PIE_TRACE_TASKS` | `1` | Prints guest task stop details. Use this before adding a return-address keyed exit promotion. |
| `DINGOO_PIE_TRACE_FS` | `1` | Prints virtual file-system read/seek activity. |
| `DINGOO_PIE_TRACE_FS_OPEN` | `1` | Prints virtual file-system open decisions without logging every read. |
| `DINGOO_PIE_TRACE_KBD_CALLERS` | `1` | Prints guest call sites for `_kbd_get_status` and `_kbd_get_key` whenever a non-zero key state is returned. |
| `DINGOO_PIE_TRACE_COPY` | `1` | Traces memory copies. Pair with `DINGOO_PIE_TRACE_MEM_START` and `DINGOO_PIE_TRACE_MEM_END`. |
| `DINGOO_PIE_TRACE_PC_START` / `DINGOO_PIE_TRACE_PC_END` | hex addresses | Traces interpreter PCs in a range. |

## Cheat File Format

`.cht` files are UTF-8 text files. They are loaded by app base name and may
include one optional SHA256 guard:

```text
app_sha256=<optional 64-hex SHA256>
status|name|width|address|value
status|name|width|address|value|compare
```

Fields:

- `status`: `on`, `off`, or `once`. Feature rows remain unchecked until the
  user enables them; after that the enabled feature names are saved per game.
  `once` applies after its optional compare check passes, then stops applying
  until the game is reloaded.
- `name`: menu text. Use `Chinese/English` before the feature separator to
  provide localized labels.
- `width`: `u8`, `u16`, or `u32`; values are written little-endian.
- `address`: guest VM address.
- `value`: value to write.
- `compare`: optional current value required before writing.

Use compare values for code patches whenever possible. They prevent a patch
from silently changing a different game version. One visible menu feature can
use multiple low-level writes by sharing the same prefix:

```text
once|解锁所有赛车/Unlock All Cars：隐藏锁图标|u32|0x80A092F4|0x1000000F|0x1040000F
once|解锁所有赛车/Unlock All Cars：允许选择|u32|0x80A0A54C|0x10000005|0x10400005
```

The menu shows one item: `解锁所有赛车` in Chinese or `Unlock All Cars` in
English.

## Repeatable Smoke Tests

Use `scripts\debug_output_regression.ps1` after changing Debug Console, logging,
SDL startup, or stdout/stderr handling. It launches isolated no-game runs and
checks Open Debug Log file creation through `DINGOO_PIE_LOG_FILE`, INI
Performance Log, Debug Console plus Open Debug Log, Debug Console-only startup,
stdout redirection, and empty stderr.

```powershell
.\scripts\debug_output_regression.ps1 `
  -BuildDir '.\release' `
  -Seconds 5
```

Use `scripts\smoke_test.ps1` after compatibility or structure changes. It runs
an app for a fixed duration, captures stdout/stderr beside the executable, and
prints a JSON summary of expected diagnostics.
Run smoke tests sequentially. Each run owns the `DingooPie.exe` process and
terminates any existing emulator instance before launching its sample.

```powershell
.\scripts\smoke_test.ps1 `
  -Name smoke-7days-noconfig `
  -AppPath 'D:\Games\Dingoo\7Days.app' `
  -BuildDir 'C:\path\to\build-dingoo-emu' `
  -Seconds 8 `
  -NoConfig
```

`-NoConfig` temporarily hides `DingooPie.ini` so startup can be checked without
persisted settings. It restores the user's INI file after the run.

Use `scripts\profile_sample.ps1` when checking timing-sensitive samples. It can
skip startup profile windows and fail the run when framebuffer submission jitter
exceeds thresholds:

```powershell
.\scripts\profile_sample.ps1 `
  -Name dicke-snake-title `
  -AppPath 'D:\Games\Dingoo\Dicke Snake.app' `
  -BuildDir 'C:\path\to\build-dingoo-emu' `
  -Seconds 12 `
  -DisplayFps 60 `
  -SkipProfileSamples 2 `
  -MinIrJitSamples 8 `
  -MaxAvgFrameIntervalUs 20500 `
  -MaxAvgFrameIntervalMaxUs 24000 `
  -MaxPeakFrameIntervalUs 28000 `
  -MaxAvgOver25 1 `
  -MaxPeakOver25 2 `
  -MaxAvgOver33 0.5 `
  -MaxPeakOver33 1
```

Use `scripts\quit_sample.ps1` when validating an in-game quit path. Unlike the
fixed-duration profile harness, it waits for natural process exit first and only
force-stops the emulator after the timeout. Treat `natural_exit=false` with
`stopped_by_harness=true` as a failed quit path, not as cleanup success.
When a game's quit option returns to the title screen instead of closing the
process, pass the title frame's `ahash` through `-TitleFrameHash`; the result
will report `return_to_title=true` so that behavior is tracked separately from
both process exit and no-op input.

```powershell
.\scripts\quit_sample.ps1 `
  -Name quit-sample `
  -AppPath 'D:\Games\Dingoo\Sample.app' `
  -BuildDir 'D:\Project\C++\dingoo-emu\build\win64' `
  -AutoPressSequence 'START@1500:150,MENU@3500:150,A@5000:150' `
  -DumpFramePattern 'quit-sample-frame-%u.bmp' `
  -DumpFrameStart 120 `
  -DumpFrameEnd 480 `
  -DumpFrameStep 120
```

The same script can pin the interpreter backend and validate fallback
performance without changing the user's persisted INI:

```powershell
.\scripts\profile_sample.ps1 `
  -Name dicke-snake-interpreter `
  -AppPath 'D:\Games\Dingoo\Dicke Snake.app' `
  -BuildDir 'C:\path\to\build-dingoo-emu' `
  -Seconds 12 `
  -Backend interpreter `
  -DisplayFps 60 `
  -SkipProfileSamples 6 `
  -MinFrontendSamples 4 `
  -MinInterpreterSamples 4 `
  -MinAvgPresentedFps 55 `
  -MaxAvgFrameIntervalUs 18000 `
  -MaxAvgFrameIntervalMaxUs 21000 `
  -MaxPeakFrameIntervalUs 38000 `
  -MaxAvgOver25 0.5 `
  -MaxPeakOver25 1 `
  -MaxAvgOver33 0.5 `
  -MaxPeakOver33 1
```

Use `scripts\profile_samples.ps1` for directory-level compatibility baselines.
It runs every `.app` in a sample directory sequentially, invokes
`profile_sample.ps1` for one or both backends, copies logs/frame dumps into
per-run artifact directories, and writes CSV/JSON/Markdown summaries:

```powershell
.\scripts\profile_samples.ps1 `
  -SampleDir 'C:\Users\bl2ck\Desktop\丁果A320或歌美X760+样本' `
  -BuildDir 'C:\path\to\build-dingoo-emu' `
  -OutputDir 'C:\path\to\artifacts\a320-x760plus-3d-baseline' `
  -Seconds 10 `
  -SkipProfileSamples 2 `
  -DumpFrameStart 120 `
  -DumpFrameEnd 360 `
  -DumpFrameStep 120 `
  -Backend both
```

See `docs\A320_X760_PLUS_3D_BASELINES.md` for the current local 14-sample
baseline, known failures, and the manual verification checklist.

For optimized-backend rebuild checks, verify that bootstrap installs Dynarmic
and Boost and that the extracted PPSSPP source contains the Dingoo shim
sentinels after `scripts\bootstrap_windows.ps1` completes:

- `Core\MIPS\x86\X64IRAsm.cpp`: `ppssppShimRead32` and `ppssppShimRunCodeHook`
- `Core\MemMap.h`: `DINGOO_PIE_DINGOO_MEMORY`
- `Core\MIPS\IR\IRInst.h`: `MulLow`
- `Core\MIPS\IR\IRFrontend.cpp`: VFPU bounds handling from
  `patches\ppsspp-irjit-vfpu-bounds.patch`

The bootstrap script performs this check automatically. Treat a missing sentinel
as a broken build environment even if CMake can still compile the emulator.

## Profile Counters

`profile:frontend` reports low-frequency frontend counters:

- `draws`: SDL presentations per second. This is now driven by game frame submissions, not by a 60 Hz window timer.
- `presented_fps`: successful frontend presentations per second. The on-screen FPS overlay uses this same value.
- `submitted_fps`: compatibility alias for `presented_fps` in current logs.
- `content_fps`: submitted frames whose snapshot hash changed.

`profile:hle` reports:

- `lcd_set`: calls to `_lcd_set_frame`, `lcd_set_frame`, or `ap_lcd_set_frame`.
- `time`, `gettick`, `ostimedly`: timer usage. High `ostimedly` totals usually mean the game is throttling itself through the SDK timer.
- `DINGOO_PIE_OSTIMEDLY_SCALE` only changes host waiting time. The original delay/tick totals are still reported so timing bottlenecks remain visible. Short waits use microsecond-level accumulated pacing rather than direct millisecond `SDL_Delay` calls.
- `wave_write`, `sem`: audio task activity and synchronization.
- `sys_event`, `kbd`: input polling cadence.

`profile:irjit` reports:

- `hooks`: transitions from generated PPSSPP code into the native hook bridge.
- `fast_lcd`: LCD frame submissions handled by the PPSSPP fast-HLE path.
- `advances`: PPSSPP CoreTiming slice advances.
- `reads` / `writes`: fallback shim memory accesses. After startup, hot samples should use fast pages and keep these low.
- `fast_fread` / `fast_fseek`: resource or host file reads handled without leaving the PPSSPP fast-HLE path.
- `fb_submit`: framebuffer snapshots submitted during the sampling window.
- `fb_copy_us`: host time spent copying submitted framebuffer snapshots.
- `throttle`: whether optional guest-clock wall-time throttling is active.
- `throttle_sleep_ms`: host time spent waiting for the configured guest clock.
- `throttle_ahead_ms` / `clock_hz`: active wall-time throttle parameters.
- `pc` / `ra`: current MIPS PC and return address at the sample point.

`profile:interpreter` reports:

- `ips`: interpreted MIPS instructions per second. Low values are not always a
  regression after HLE loop promotion because host-side hooks can replace large
  guest loops.
- `hooks`: interpreter code-hook callbacks per second.
- `fb_submit`, `fb_copy_us`, `fb_interval_us`, `over25`, `over33`: the same
  framebuffer pacing counters used by `profile:irjit`.
- `pc` / `ra`: current MIPS PC and return address at the sample point.
- `DINGOO_PIE_INTERPRETER_PC_PROFILE=1` adds a low-frequency hot-PC histogram
  for diagnosing remaining interpreter-only bottlenecks.

## Current Sample Baselines

The latest known smoke results are:

- `Dicke Snake.app` (`22531CCED426F19232613C8235B44A3DD4CDECDA18CD6A517044DC05160C5D39`): the title screen is sensitive to short `OSTimeDly` jitter. Microsecond-level HLE delay pacing keeps framebuffer submissions near the display cadence without a content-hash rule. The interpreter backend also reaches stable frame pacing after exact-pattern RGB565/indexed-blit loop promotion in `mips_compat.cpp`; use the interpreter profile threshold above to guard this fallback path.
- `Snake.app`: frontend and HLE frame submission are aligned around 19-21 FPS after framebuffer snapshotting on the default Auto backend.
- `PoPo Bash.app`: frontend submission is aligned with HLE at roughly 15-16 FPS. Remaining visible cadence is likely Dingoo timer/task/audio semantics rather than SDL presentation.
- `Ultimate Drift.app`: remains a diagnostic sample for CPU/VFPU coverage and framebuffer behavior. This codebase no longer carries a game-specific Soft3D or 3D resource parser; investigate remaining 3D issues through guest execution traces, SDK/HLE calls, and framebuffer submissions.
- `7Days.app` (`AF681C338A9932C98A3B450D4391C43D13747F1DFD937232AE38BEDB44359BF0`): the title/menu path throttles heavily through `OSTimeDly`. Auto now uses the global 1.0 delay scale; use `Settings -> System Delay Scale` to tune it per user preference. Use `DINGOO_PIE_AUTOPRESS_SEQUENCE=A@9000:300` for smoke tests; repeated confirm presses can enter the save-load screen instead of staying on the title screen. Framebuffer dumps from this path should show title text and the background corridor rather than an all-black frame.

Game files are not part of the repository or release packages. `.app` files
belong to Dingoo Technology's package format and must come from legally obtained
user samples.

## Extension Points

- Add sample-specific content-hash rules in `native/core/config/compatibility/compat_profile.cpp`.
- Add SDK import handlers in `native/core/app/hle/app_hle.cpp`.
- Add exact instruction compatibility hooks in `native/core/app/cpu/mips_compat.cpp`.
- Add CPU instruction support to `native/core/app/cpu/mips_runtime.cpp` for interpreter checks.
- Add PPSSPP shim or fast-memory work in `native/core/app/cpu/ppsspp_bridge.cpp` and the patch files under `patches/`.
- Keep subtask JIT disabled by default until PPSSPP globals such as `currentMIPS`, `coreState`, and `MIPSComp::jit` are isolated per runtime or per thread.
