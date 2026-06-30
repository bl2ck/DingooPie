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
`Dingoo Game Emulator`, file/product version `1.2`, product
name `DingooPie`, and `Copyright (c) BL2CK 2026`.
Starting without command-line arguments does not show a file picker. Empty
`recent.last_app` opens the frontend only; an existing `recent.last_app` is
auto-loaded; a command-line `.app` path takes priority.
Selecting a game from `File -> Open Game`, choosing `File -> Recent Games`,
or dropping an `.app` file onto the window saves the chosen UTF-8 path to
`recent.last_app` and promotes it into `recent.app1` through `recent.app10`.
`File -> Recent Games -> Clear Recent Games` clears both `recent.last_app` and
the ordered `recent.app1`...`recent.app10` list.
Automatic `recent.last_app` startup clears the matching recent entry when the
path is missing, does not end in `.app`, or fails during app open/parse.
Command-line startup failures are diagnostic-only and do not modify
`DingooPie.ini`.
The menu is ordered as File, Options, Settings, Debug, and Help. Options
contains Video, Audio, and Input submenus. Video settings are scale, fullscreen,
anti-aliasing, effect, brightness, contrast, gamma, saturation, minimized
behavior, portrait mode, and FPS overlay. Audio and input settings follow, then
runtime settings, cheats, UI language, and debug options. File > Pause
Game/Resume Game freezes game execution and audio at frame boundaries, but it is
runtime-only and is not written to `DingooPie.ini`.
`DingooPie.ini` is rewritten in the same practical order:
`recent`, `video`, `audio`, `input`, `runtime`, optional `cheats`, `ui`, then
`debug`. `settings-trace` prints the same section order when debug output is
enabled.
The `recent` section writes `last_app` first, followed by the ordered
`app1`...`app10` recent-game list.
Runtime-affecting values are saved immediately. Changes to window scale,
windowed fullscreen, minimized behavior, portrait mode, FPS overlay, CPU clock, runtime speed, delay scale,
audio disable, performance logging, anti-aliasing/effect, brightness, contrast,
gamma, saturation, IME disable mode, virtual controls, language, master volume, audio
buffer size, and debug console visibility apply without relaunching the guest.
Changing the CPU backend still relaunches the emulator because the execution
backend is selected at startup. Restored defaults relaunch only when they change
the active CPU backend.
`audio.volume_percent` stores the emulator-side master volume. It is applied
after the guest `waveout` volume so game-internal volume changes remain active.
`audio.buffer_samples` controls the SDL output buffer request. The default is
`2048`; larger values reduce underruns at the cost of more audio latency, while
smaller values reduce latency but can make weak hosts crackle.
`video.minimized_behavior` accepts `normal`, `throttle`, or `pause`. The
default `throttle` lowers frontend presentation and loop cadence while the
window is minimized. `pause` automatically pauses game execution and audio on
minimize, then resumes only pauses that were caused by minimization.
The default UI language is Chinese. `Settings -> Language` persists
`ui.language=english` or `ui.language=chinese` for menus and native file dialogs.
`runtime.speed_scale=` means `Auto`; the frontend leaves
`DINGOO_PIE_RUNTIME_SPEED_SCALE` unset so the runtime uses the global 65%
Auto pace.
`runtime.cpu_hz=` means `Auto`; explicit CPU clock menu values set
`DINGOO_PIE_IRJIT_CLOCK_HZ` and apply to the IR JIT immediately.
`runtime.backend=` means `Auto`, which maps to the PPSSPP IR JIT backend.
`runtime.ostimedly_scale=` means `Auto`, which maps host SDK delay waits to
the global 1.0 SDK delay default while explicit values preserve manual
accuracy/performance choices.
`runtime.cheats_enabled=1` enables runtime cheat-code application. Cheat files
are loaded from a `cheats` directory next to `DingooPie.exe` by app base name:
`GameName.app` loads `cheats\GameName.cht`. The optional
`app_sha256=` field inside the `.cht` file is validation only. The global cheat
switch is disabled by default; the menu item is `Settings -> Enable Cheats`.
Individual cheat features start unchecked until the user selects them under
`Settings -> Cheat Features`. Selected features are saved per game and restored
when the same game loads again. The same global switch can be forced with
`DINGOO_PIE_CHEATS=1`.
Saved feature selections are written in the optional `[cheats]` section between
`[runtime]` and `[ui]`.
Cheat feature rows are shown under `Settings -> Cheat Features`. The menu groups
multiple low-level rows by the text before `:` or `：`, so names such as
`解锁所有赛车/Unlock All Cars：patch 1` and
`解锁所有赛车/Unlock All Cars：patch 2` appear as one localized feature.
Use `中文/English` before the group separator when a feature needs both Chinese
and English menu labels.
Startup does not create `DingooPie.ini`; the file is written only after the user
changes or resets settings.
`input.disable_ime=1` is the default and disables the Windows IME for the SDL
window so input methods cannot intercept gameplay keys. It can be toggled from
`Input -> Disable IME` and applies immediately.
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
The executable is built as a Windows GUI app by default, so no console is
shown unless `Debug -> Show Debug Console` or `debug.show_console=1` is enabled.
`Debug -> Open Debug Log` checks the executable directory first and
shows a localized message if the log file has not been created yet. Performance
logging and `DINGOO_PIE_LOG_FILE=1` create that file next to `DingooPie.exe`.
`Debug -> Cheat Finder` searches u8/u16/u32 values, narrows candidates with
equal, increased, decreased, and unchanged filters, writes a selected address,
and copies a selected result as a `.cht` record for the matching cheat file.
It is enabled only while a game is running.
`Debug -> Debugger` opens a live inspection panel for the active runtime. It
shows PC-based MIPS disassembly, all GPR/HI/LO registers, a hex memory viewer,
breakpoint hit counters, and write watches. Use write watches on candidate
addresses from Cheat Finder to identify the PC that changes health, score, or
other values. Breakpoints and write watches record hits but do not pause or
single-step the CPU. It is enabled only while a game is running.
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
`video.portrait=1` rotates the SDL presentation and saved screenshots
90 degrees counter-clockwise, swaps the non-fullscreen window to 240x320 at the
selected scale, and rotates the virtual control overlay and hit testing with
the displayed screen. The guest framebuffer remains the fixed Dingoo 320x240
surface.
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
| `DINGOO_PIE_BACKEND` | `ppsspp_irjit`, `irjit`, `interpreter` | Selects the main CPU backend. The default is `ppsspp_irjit`. |
| `DINGOO_PIE_SUBTASK_BACKEND` | `interpreter`, `ppsspp_irjit` | Selects the backend for host pthread-backed Dingoo tasks. Keep the default `interpreter` until PPSSPP global state is made thread-local. |
| `DINGOO_PIE_PROFILE` | `1` | Enables one-second frontend, HLE, compatibility, and IR JIT counters. |
| `DINGOO_PIE_COMPAT_TRACE` | `1` | Prints unique compatibility hook addresses for Dingoo break/cache instructions. |
| `DINGOO_PIE_IRJIT_TRACE` | `1` | Prints detailed PPSSPP shim diagnostics. This is noisy. |
| `DINGOO_PIE_IRJIT_FASTMEM` | `0` | Disables direct fast-memory pages for diagnostics. |
| `DINGOO_PIE_IRJIT_DISABLE_FLAGS` | `none`, `default`, numeric, or comma-separated flag names | Overrides PPSSPP IR JIT disable flags. The default enables block linking and disables only cache pointer and pointerify transforms. |
| `DINGOO_PIE_IRJIT_THROTTLE` | `0`, `1` | Enables guest-clock throttling for the IR JIT. Disabled by default because SDK timers are HLE-driven and wall-clock throttling can stall software-rendered samples. |
| `DINGOO_PIE_IRJIT_CLOCK_HZ` | `1000000..1000000000` | Overrides the guest CPU clock used by IR JIT throttling. The default is the Dingoo-style 336 MHz clock. |
| `DINGOO_PIE_IRJIT_THROTTLE_AHEAD_MS` | `0..5000` | Allows the JIT to run ahead of wall time before sleeping when `DINGOO_PIE_IRJIT_THROTTLE=1`. The default is 1000 ms. |
| `DINGOO_PIE_IRJIT_THROTTLE_MAX_LAG_MS` | `1..5000` | Resets the throttle baseline after long host stalls so delayed input or loading does not cause a catch-up burst. |
| `DINGOO_PIE_DISPLAY_FPS` | `1..240` | Limits SDL texture uploads and presentations without blocking guest execution. The default is 60. |
| `DINGOO_PIE_LCD_FRAME_PACING` | `0`, `1` | Enables adaptive pacing at Dingoo LCD frame submission boundaries. The default is enabled; set `0` to diagnose raw guest frame production. |
| `DINGOO_PIE_AUDIO_QUEUE_DROP_MS` | `0..60000` | Drops guest PCM buffers after the audio queue stays full for this many milliseconds. The default `0` waits for playback so saturated queues preserve audio timing. |
| `DINGOO_PIE_AUDIO_QUEUE_TRACE` | `1` | Logs audio queue backpressure waits when `DINGOO_PIE_AUDIO_QUEUE_DROP_MS=0`. This is noisy during games that stream audio near the queue limit. |
| `DINGOO_PIE_RUNTIME_SPEED_SCALE` | `0.0..1.0` | Scales runtime pacing used by HLE and the PPSSPP shim. The menu `Auto` preset leaves it unset and maps to the global 65% runtime pace. Explicit Runtime Speed menu values apply immediately and persist to the INI. |
| `DINGOO_PIE_OSTIMEDLY_SCALE` | `0.0..1.0` | Scales host sleep time for `OSTimeDly`, `delay_ms`, and `udelay` calls while preserving guest tick accounting. Auto uses the global 1.0 delay scale unless a content-hash compatibility entry overrides it. Use this to override sample-specific delay behavior. |
| `DINGOO_PIE_CHEATS` | `1` | Enables loaded cheat files without changing `DingooPie.ini`. Cheat files use `status|name|width|address|value` or `status|name|width|address|value|compare` pipe records; see "Cheat File Format" below. |
| `DINGOO_PIE_CHEAT_DIR` | path | Overrides the directory used for `.cht` files. |
| `DINGOO_PIE_CHEAT_TRACE` | `1` | Prints cheat loading and apply counters. |
| `DINGOO_PIE_IRJIT_SLICE` | `10000..10000000` | Overrides the PPSSPP shim slice length. Useful for timing experiments only. |
| `DINGOO_PIE_INPUT_TRACE` | `1` | Prints SDL key events and Dingoo key state reads. |
| `DINGOO_PIE_AUTOPRESS_KEYS` | `KEY:DELAY_MS:COUNT:PERIOD_MS:HOLD_MS` | Injects deterministic synthetic controls from inside the frontend for automated sample tests. Example: `A:6000:8:900:300`. Keys: `A`, `B`, `X`, `Y`, `U`, `D`, `L`, `R`, `SELECT`, `START`. |
| `DINGOO_PIE_AUTOPRESS_SEQUENCE` | `KEY@DELAY_MS:HOLD_MS,...` | Injects a deterministic multi-key sequence for multi-screen startup flows. Example: `A@6000:250,A@9000:250,D@12000:300`. The key names are SDK controls, not host keyboard letters. Raw SDK key aliases are also accepted for compatibility diagnostics: `ENTER`, `AB`, `EQ`, `CAMERA`, and `MENU`. |
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
once|解锁所有赛车/Unlock All Cars：隐藏锁图标|u32|0x80A9B190|0x1000000F|0x1040000F
once|解锁所有赛车/Unlock All Cars：允许选择|u32|0x80A9C3E8|0x10000005|0x10400005
```

The menu shows one item: `解锁所有赛车` in Chinese or `Unlock All Cars` in
English.

## Repeatable Smoke Tests

Use `scripts\debug_output_regression.ps1` after changing debug console, logging,
SDL startup, or stdout/stderr handling. It launches isolated no-game runs and
checks environment-forced debug logs, INI performance logs, debug console plus
log output, console-only startup, stdout redirection, and empty stderr.

```powershell
.\scripts\debug_output_regression.ps1 `
  -BuildDir '.\release' `
  -Seconds 2
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

For PPSSPP IR JIT rebuild checks, verify that the extracted source contains the
Dingoo shim sentinels after `scripts\bootstrap_windows.ps1` completes:

- `Core\MIPS\x86\X64IRAsm.cpp`: `ppssppShimRead32` and `ppssppShimRunCodeHook`
- `Core\MemMap.h`: `DINGOO_PIE_DINGOO_MEMORY`
- `Core\MIPS\IR\IRInst.h`: `MulLow`

The bootstrap script performs this check automatically. Treat a missing sentinel
as a broken build environment even if CMake can still compile the emulator.

## Profile Counters

`profile frontend` reports:

- `draws`: SDL presentations per second. This is now driven by game frame submissions, not by a 60 Hz window timer.
- `presented_fps`: successful frontend presentations per second. The on-screen FPS overlay uses this same value.
- `submitted_fps`: compatibility alias for `presented_fps` in current logs.
- `content_fps`: submitted frames whose snapshot hash changed.

`profile hle` reports:

- `lcd_set`: calls to `_lcd_set_frame`, `lcd_set_frame`, or `ap_lcd_set_frame`.
- `time`, `gettick`, `ostimedly`: timer usage. High `ostimedly` totals usually mean the game is throttling itself through the SDK timer.
- `DINGOO_PIE_OSTIMEDLY_SCALE` only changes host waiting time. The original delay/tick totals are still reported so timing bottlenecks remain visible. Short waits use microsecond-level accumulated pacing rather than direct millisecond `SDL_Delay` calls.
- `wave_write`, `sem`: audio task activity and synchronization.
- `sys_event`, `kbd`: input polling cadence.

`profile irjit` reports:

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

`profile interpreter` reports:

- `ips`: interpreted MIPS instructions per second. Low values are not always a
  regression after HLE loop promotion because host-side hooks can replace large
  guest loops.
- `hooks`: interpreter code-hook callbacks per second.
- `fb_submit`, `fb_copy_us`, `fb_interval_us`, `over25`, `over33`: the same
  framebuffer pacing counters used by `profile irjit`.
- `pc` / `ra`: current MIPS PC and return address at the sample point.
- `DINGOO_PIE_INTERPRETER_PC_PROFILE=1` adds a low-frequency hot-PC histogram
  for diagnosing remaining interpreter-only bottlenecks.

## Current Sample Baselines

The latest known smoke results are:

- `Dicke Snake.app` (`22531CCED426F19232613C8235B44A3DD4CDECDA18CD6A517044DC05160C5D39`): the title screen is sensitive to short `OSTimeDly` jitter. Microsecond-level HLE delay pacing keeps framebuffer submissions near the display cadence without a content-hash rule. The interpreter backend also reaches stable frame pacing after exact-pattern RGB565/indexed-blit loop promotion in `instruction_compat.cpp`; use the interpreter profile threshold above to guard this fallback path.
- `Snake.app`: frontend and HLE frame submission are aligned around 19-21 FPS after framebuffer snapshotting on the default Auto backend.
- `PoPo Bash.app`: frontend submission is aligned with HLE at roughly 15-16 FPS. Remaining visible cadence is likely Dingoo timer/task/audio semantics rather than SDL presentation.
- `Ultimate Drift.app`: remains a diagnostic sample for CPU/VFPU coverage and framebuffer behavior. This codebase no longer carries a game-specific Soft3D or 3D resource parser; investigate remaining 3D issues through guest execution traces, SDK/HLE calls, and framebuffer submissions.
- `7Days.app` (`AF681C338A9932C98A3B450D4391C43D13747F1DFD937232AE38BEDB44359BF0`): the title/menu path throttles heavily through `OSTimeDly`. Auto now uses the global 1.0 delay scale; use `Settings -> Delay Scale` to tune it per user preference. Use `DINGOO_PIE_AUTOPRESS_SEQUENCE=A@9000:300` for smoke tests; repeated confirm presses can enter the save-load screen instead of staying on the title screen. Framebuffer dumps from this path should show title text and the background corridor rather than an all-black frame.

Game files are not part of the repository or release packages. `.app` files
belong to Dingoo Technology's package format and must come from legally obtained
user samples.

## Extension Points

- Add sample-specific content-hash rules in `dingoo_pie/compat_profile.cpp`.
- Add SDK import handlers in `dingoo_pie/sdk_hle.cpp`.
- Add exact instruction compatibility hooks in `dingoo_pie/instruction_compat.cpp`.
- Add CPU instruction support to `dingoo_pie/native_runtime.cpp` for interpreter checks.
- Add PPSSPP shim or fast-memory work in `dingoo_pie/ppsspp_shim.cpp` and the patch files under `patches/`.
- Keep subtask JIT disabled by default until PPSSPP globals such as `currentMIPS`, `coreState`, and `MIPSComp::jit` are isolated per runtime or per thread.

