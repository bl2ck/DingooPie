# Architecture Notes

This project is a Windows HLE runner for Dingoo Technology `.app` programs used
by Dingoo A320 handheld software and compatible Gemei X760+ handheld software.
It does not emulate the original firmware as a full machine. Instead, it loads
the guest MIPS executable, maps its memory, and implements the Dingoo SDK calls
that commercial samples use. The `.app` package format belongs to Dingoo
Technology; game files are external test inputs, not project assets.

## Design Goals

- Keep app compatibility fixes deterministic and tied to the content hash, not
  to a file name that users may rename.
- Prefer generic SDK behavior in the HLE bridge. Add per-sample compatibility
  rules only when logs show a specific guest behavior that cannot be handled
  safely for every app.
- Keep PPSSPP IR JIT as the normal APP backend and Dynarmic as the normal CC
  backend. Retain the in-tree MIPS32 and ARM32 interpreters as correctness and
  diagnostic fallbacks.
- Keep user configuration optional. A clean command-line run should not create
  `DingooPie.ini`; the file is written only after settings are changed or reset
  through the frontend.

## Runtime Flow

1. `main.cpp` loads settings, initializes SDL, and starts the guest runtime when an
   APP or CC game is selected. Without a startup game, the frontend waits for
   `File/Open Game`.
2. `game_runtime.cpp` dispatches `.app` games to `app_runtime.cpp` and `.cc`
   games to `cc_runtime.cpp`. Both paths compute a SHA256 identity before
   loading format-specific runtime state.
3. APP maps MIPS guest memory, initializes the HLE bridge and virtual file
   system, then selects PPSSPP IR JIT or the in-tree MIPS interpreter. CC maps
   ARM32 runtime state and selects Dynarmic or the in-tree ARM32 interpreter.
4. `app_hle.cpp` implements Dingoo SDK calls such as framebuffer submission,
   timers, input, task APIs, resources, audio, and formatted output.
5. `sdl_frontend.cpp`, `framebuffer.cpp`, and `sdl_audio.cpp` present video,
   input, overlays, virtual controls, filters, screenshots, and audio on the
   host.
6. `guest_filesystem.cpp` exposes app resources and host files through
   Dingoo-like file APIs.

## Frontend Surface

The Windows frontend is a normal menu-driven SDL window. It does not show a
file picker on process startup. Empty `recent.last_app` opens the frontend only;
an existing `recent.last_app` is auto-loaded; command-line paths take priority.
`--no-recent` suppresses automatic recent-game startup without changing the list.
The no-game state presents an idle background in the SDL window. Menu tracking
and modal dialogs pause that idle presentation, and gameplay startup releases
idle resources before the runtime owns frame presentation.
`File/Open Game`, `File/Recent Games`, and SDL file drops validate the
selected path, save it to `recent.last_app`, promote it in the recent-game list,
and launch the selected app in a fresh emulator process.
`File/Recent Games/Clear Recent Games` clears the startup recent path and the
ordered recent-game menu list. `File/Pause Game` and `File/Resume Game` are
runtime-only commands that block guest frame submission at a complete frame
boundary while keeping the frontend menu responsive.
Automatic `recent.last_app` startup is self-healing: invalid extensions,
missing files, and app open/parse failures clear the matching stored path.
Command-line startup failures are not persisted back to settings.

Runtime UI text is localized through `ui_strings.*`. Keep English and Chinese
strings in sync when changing menu labels, dialog titles, confirmation prompts,
or About text.

Current user-facing controls are grouped as File, Options, Settings, Debug, and
Help. Options contains the Video, Audio, and Input submenus. The menus include
app opening, game pause/resume, screenshot export, video scale/anti-aliasing/effect adjustment,
fullscreen, brightness, contrast, gamma, saturation, screen orientation, screen
fill, FPS overlay, virtual controls, SDL GameController input, IME disable mode,
language, backend/runtime timing options, master volume, audio effect, audio disable, debug console/performance log controls, log
opening, Resource Monitor, Memory Searcher, Debugger, settings save/reset, and
About.

## Source Boundaries

- `guest_package.*`: Dingoo Technology APP container parsing, executable image
  loading, and package resource metadata.
- `game_paths.*`: path normalization, extension checks, and game file-name helpers.
- `startup_command_line.*`, `startup_game_selection.*`: Windows command-line
  parsing, startup action validation, and initial game selection.
- `app_runtime.*`: app bootstrapping, app identity logging,
  and fatal runtime diagnostics.
- `app_framebuffer_mapping.*`: APP runtime framebuffer alias mapping. The
  frontend framebuffer module owns only pixels, snapshots, pacing, and
  presentation-facing state.
- `emulator_options.*`: environment backend and runtime option parsing.
- `emulator_settings.*`: frontend settings, optional INI persistence, and
  environment synchronization.
- `mips_runtime.*`: backend-neutral MIPS runtime, register/memory access,
  hooks, and interpreter implementation.
- `ppsspp_backend.*`, `ppsspp_bridge.cpp`: PPSSPP IR/x64 JIT adapter and
  Dingoo memory/HLE shim.
- `cc_runtime.*`, `arm32_dynarmic.*`, `arm32_interpreter.*`: CC runtime,
  Dynarmic ARM32 JIT adapter, and compatibility interpreter.
- `mips_compat.*`: exact instruction-level compatibility hooks and
  conservative software-rendering loop promotion for recognized MIPS patterns.
- `compat_profile.*`: content-hash compatibility profile data used by HLE and
  file-system code.
- `app_hle.*`: Dingoo SDK import bridge and high-level guest services.
- `guest_filesystem.*`: guest file handles, resource-backed files, and
  app-specific resource views.
- `app_task_scheduler.*`: Dingoo task APIs backed by host threads.
- `input_controls.*`, `input_state.h`: host keyboard/mouse/virtual controls to
  Dingoo A320 and Gemei X760+ button state.
- `framebuffer.*`, `sdl_frontend.*`: framebuffer storage and snapshots, SDL presentation,
  menus, overlays, filters, and screenshots.
- `app_package_resource_index.*`: read-only APP package resource-name index used
  when runtime resource events expose offsets without names.
- `resource_monitor_ui.*`, `runtime_resource_monitor.*`: Resource Monitor UI,
  runtime resource-load snapshots, and transient list highlight state.
- `sdl_audio.*`, `guest_audio.*`: SDL audio output and waveout-compatible HLE.
- `app_runtime_debug.*`, `debug_console.*`: register dumps, disassembly diagnostics,
  and optional Win32 debug console.
- `platform_win32.*`: Windows file picker, UTF-8/UTF-16 conversion, storage
  directories, and working-directory setup.

`scripts/check_core_dependencies.ps1` enforces the APP/CC/shared/frontend
dependency direction before each Release build. The shared file-system header
retains one explicit legacy APP package dependency until the APP and CC package
parsers can be unified without dropping PC resource compatibility behavior.

## Compatibility Policy

`compat_profile.*` is the central registry for sample-specific behavior. A rule
must use the `.app` SHA256 hash as its key. File names are diagnostic labels
only and must not decide compatibility behavior.

Use the smallest safe rule:

- Prefer adding or improving a generic HLE implementation when several samples
  would benefit.
- Prefer exact instruction-pattern hooks for repeated SDK/compiler-generated
  loops such as RGB565 copy, indexed blit, or cache/break sequences. Keep these
  independent of file names and content hashes unless the behavior is truly
  sample-specific.
- Use a compatibility profile value when a sample needs a stable runtime tuning
  such as default host delay scaling or a resource view.
- Use a return-address keyed rule only when the app uses an SDK task stop as
  its final quit path and logs prove the call site is specific to that app.

When adding a rule, capture at least:

- App SHA256 from `DingooPie: app sha256: ...`
- The symptom and the sample behavior being matched
- The trace variable or smoke test used to prove the change

## Configuration Lifecycle

Default settings live in `emulatorDefaultSettings()`. The frontend loads
`DingooPie.ini` if it exists, but startup does not create it. Runtime-facing
settings are mirrored into process environment variables and explicitly
refreshed in HLE/JIT components when the frontend changes them. CPU backend
selection remains startup-bound. `--config <path>` selects an alternate settings
file for the current run.
The INI reader accepts UTF-16LE with BOM, UTF-8 with or without BOM, and a
system ANSI fallback so manually edited Chinese paths remain loadable.
Saves rewrite `DingooPie.ini` in frontend order so existing files are normalized
to `recent`, `video`, `audio`, `input`, `runtime`, optional `cheats`, `ui`, then
`debug`.
The `recent` section keeps `last_app` for startup compatibility and writes
`app1` through `app10` as the ordered recent-game menu source.
`video.scale` is limited to 1, 2, or 3. `video.fullscreen=1` uses a maximized
window instead of SDL exclusive fullscreen so the native Windows menu remains
accessible.
`video.anti_aliasing` is a frontend presentation option. `off` uses nearest
sampling, `low` uses SDL linear sampling, and `clear` adds a light CPU RGB565
clarity pass before texture upload while leaving guest memory unchanged.
Unknown or invalid INI values fall back to current defaults instead of being
specially mapped.
`video.effect` is also presentation-only. Most effects are applied as RGB565
post-processes before texture upload; `pixel_grid` is a scaled-output overlay so
the grid follows the current window or screenshot size without blurring the
guest framebuffer.
`video.minimized_behavior` controls the SDL minimized-window policy. `normal`
keeps the normal loop, `throttle` lowers frontend presentation and loop cadence,
and `pause` uses the shared pause gate until the window is restored. Unknown or
invalid values fall back to the current default.
`video.screen_orientation` accepts `auto`, `landscape`, or `portrait`.
Portrait is a frontend presentation transform: the guest framebuffer stays
320x240, while SDL rendering, screenshot output, and virtual-control coordinates
rotate 90 degrees counter-clockwise. Auto follows the current renderer shape.
`video.screen_fill` accepts `aspect`, `blurred`, or `stretch`. Aspect preserves
the source ratio, blurred fills unused edges from a blurred game image, and
stretch fills the complete output.
`input.system_ime_disabled=1` is the default. It keeps Windows input methods detached
from the SDL window unless the user disables the option from the Input menu.
`input.virtual_control_scale` and `input.virtual_dpad_type` persist virtual-control
size and either the joystick or segmented-ring D-pad style.
`input.keyboard_mapping` and `input.controller_mapping` follow the same Input
menu group. Empty means the built-in keyboard or SDL GameController defaults;
non-empty values store only custom differences as comma-separated
`Physical=Control` pairs. The frontend/input layer rebuilds the runtime maps
from defaults plus these overrides and releases active synthetic controls
before applying a new map.
The persisted Debug menu settings follow menu order: `debug.show_console`
for Debug > Debug Console, `debug.profile` for Debug > Performance Log, and
`debug.resource_monitor_auto_open` for Debug > Resource Monitor auto-open.
`debug.resource_monitor_auto_open=1` keeps Debug > Resource Monitor checked.
The frontend opens the Resource Monitor once when a game becomes active, or
immediately if the user enables it while a game is already running. Resource
parsing and capture stay idle until that window path is entered.

`runtime.speed_scale=` means `Auto` in the INI and menu. Auto does not set
`DINGOO_PIE_RUNTIME_SPEED_SCALE`; the runtime maps that unset state to the
global 65% pace. Explicit menu values write their numeric scale into the INI
and the runtime environment.
`runtime.cpu_hz=` means `Auto`; explicit CPU clock menu values write the
selected guest CPU clock reference to the INI and
`DINGOO_PIE_IRJIT_CLOCK_HZ`. The environment-variable name is retained, but
both APP and CC runtimes consume the value.
`runtime.backend=` means `Auto`: APP resolves to PPSSPP IR JIT and CC resolves
to Dynarmic when their optimized backends are compiled. Compatibility Mode uses
the matching in-tree interpreter. `runtime.ostimedly_scale=` means `Auto` and
uses the global delay scale of 1.0 unless a compatibility profile supplies a
narrower APP override.
`runtime.cheats_enabled=0` is the default. The frontend persists this global
cheat switch and the selected cheat feature names per game. Individual cheat
features remain unchecked until selected by the user, then restore when the same
game is loaded again. Cheat lookup preserves the game format suffix, such as
`GameName.app` -> `cheats\GameName.app.cht` and
`GameName.cc` -> `cheats\GameName.cc.cht`, then falls back to the legacy
`cheats\GameName.cht` filename when the format-specific file is absent. The optional `app_sha256` field is
validation only and never a lookup key. Missing cheat files are silent; SHA
mismatches disable the loaded file and show the user a warning.
`audio.buffer_samples` controls only the SDL output device buffer request; the
guest SDK still supplies waveout sample rate, sample format, and channel count.
`audio.effect` applies optional lightweight PCM effects in `sdl_audio.cpp`
before master volume scaling and SDL queue submission.

## Resource And Package Policy

Game `.app` files belong to Dingoo Technology's package format. They are test
inputs and must not be committed or shipped in development packages.
`scripts/package_project.ps1` rejects staged and verified archives that contain
`.app` files.

The loader exposes CCDL/IMPT/EXPT/RAWD metadata and raw ERPT/packed resources.
ERPT payloads are treated as XOR-encoded resource records. Packed resources are
found by conservative table probes because the short-name packed table has no
magic value; candidates must have printable names, plausible offsets, and a
minimum number of known file extensions. The companion app tool mirrors this
logic so unpack/repack behavior stays aligned with runtime loading. The loader
does not parse game-specific 3D resource formats.

The package may include:

- Project source, scripts, docs, and patches
- Package SHA256 manifests

Dependency archives and extracted dependency trees such as `downloads/`,
`third_party/`, `deps_extract/`, `w64devkit/`, and generated release binaries
under `release/` are local build workspaces. They can be regenerated by
`scripts/bootstrap_windows.ps1` and `scripts/build_release.ps1` and are not
treated as project source.
Windows release builds must include `SDL2.dll`, `libcapstone.dll`, and
`libwinpthread-1.dll` alongside `DingooPie.exe`; missing required DLLs fail
release generation rather than being silently skipped.

## Debugging Policy

Use low-frequency profile counters first, then enable targeted trace variables
only for the subsystem being investigated. The most useful starting switches are:

- `DINGOO_PIE_PROFILE=1` for Debug > Performance Log counters.
- `DINGOO_PIE_LOG_FILE=1` when Debug > Open Debug Log must have a file to open.
- `DINGOO_PIE_RESOURCE_MONITOR_AUTOTEST=1` for Resource Monitor capture in automation.
- `DINGOO_PIE_MEMORY_SEARCHER_AUTOTEST=1` for Memory Searcher automation hooks.
- `DINGOO_PIE_DEBUGGER_AUTOTEST=1` for Debugger automation hooks.
- `DINGOO_PIE_INPUT_TRACE=1` for keyboard and virtual-control input.
- `DINGOO_PIE_TRACE_HLE=1` for selected HLE calls.
- `DINGOO_PIE_TRACE_TASKS=1` for guest task stop paths.
- `DINGOO_PIE_TRACE_FS=1` or `DINGOO_PIE_TRACE_FS_OPEN=1` for file activity.
- `DINGOO_PIE_IRJIT_TRACE=1` for noisy PPSSPP shim diagnostics.

Profile counters use the `profile:<area>` prefix and should avoid empty-window
output unless a diagnostic switch explicitly asks for it.

`scripts/smoke_test.ps1` is the preferred repeatable check after structural
changes. It can run a sample for a fixed duration, capture stdout/stderr logs,
optionally hide the user config, and summarize whether expected diagnostics
appeared.
