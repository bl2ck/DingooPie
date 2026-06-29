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
- Keep the PPSSPP IR JIT as the normal execution backend and the in-tree
  interpreter as the correctness and diagnostic fallback.
- Keep user configuration optional. A clean command-line run should not create
  `DingooPie.ini`; the file is written only after settings are changed or reset
  through the frontend.

## Runtime Flow

1. `main.cpp` loads optional settings, applies them to environment variables,
   initializes SDL, and starts the guest runtime thread only when an app path
   was provided on the command line. Without a startup app, the frontend stays
   open and waits for `File/Open Game`.
2. `emulator_core.cpp` parses the `.app`, computes its SHA256 identity, maps
   guest memory, initializes the HLE bridge and virtual file system, installs
   compatibility hooks, and jumps to the guest entry point.
3. `native_runtime.cpp` owns the MIPS execution contract. It selects either the
   PPSSPP IR JIT adapter or the in-tree interpreter and dispatches hooks for
   mapped SDK imports and compatibility breakpoints.
4. `sdk_hle.cpp` implements Dingoo SDK calls such as framebuffer submission,
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
app opening, game pause/resume, screenshot export, video scale/anti-aliasing/effect adjustment, windowed
fullscreen, brightness, contrast, saturation, FPS overlay, virtual controls,
SDL GameController input, IME disable mode, language, backend/runtime timing
options, master volume, audio disable, debug console/performance log controls, log
opening, settings save/reset, and About.

## Source Boundaries

- `app_loader.*`: Dingoo Technology `.app` container parsing and app resource metadata.
- `app_paths.*`: path normalization and app file-name helpers.
- `emulator_core.*`: app bootstrapping, memory mapping, app identity logging,
  and fatal runtime diagnostics.
- `emulator_options.*`: command-line and environment backend options.
- `emulator_settings.*`: frontend settings, optional INI persistence, and
  environment synchronization.
- `native_runtime.*`: backend-neutral MIPS runtime, register/memory access,
  hooks, and interpreter implementation.
- `ppsspp_irjit_backend.*`, `ppsspp_shim.cpp`: PPSSPP IR/x64 JIT adapter and
  Dingoo memory/HLE shim.
- `instruction_compat.*`: exact instruction-level compatibility hooks and
  conservative software-rendering loop promotion for recognized MIPS patterns.
- `compat_profile.*`: content-hash compatibility profile data used by HLE and
  file-system code.
- `sdk_hle.*`: Dingoo SDK import bridge and high-level guest services.
- `guest_filesystem.*`: guest file handles, resource-backed files, and
  app-specific resource views.
- `task_scheduler.*`: Dingoo task APIs backed by host threads.
- `input_controls.*`, `input_state.h`: host keyboard/mouse/virtual controls to
  Dingoo A320 and Gemei X760+ button state.
- `framebuffer.*`, `sdl_frontend.*`: framebuffer snapshots, SDL presentation,
  menus, overlays, filters, and screenshots.
- `sdl_audio.*`, `guest_audio.*`: SDL audio output and waveout-compatible HLE.
- `runtime_debug.*`, `debug_console.*`: register dumps, disassembly diagnostics,
  and optional Win32 debug console.
- `platform_win32.*`: Windows file picker, UTF-8/UTF-16 conversion, and working
  directory setup.

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
selection remains startup-bound.
The INI reader accepts UTF-16LE with BOM, UTF-8 with or without BOM, and a
system ANSI fallback so manually edited Chinese paths remain loadable.
Saves rewrite `DingooPie.ini` in frontend order so existing files are normalized
to `recent`, `video`, `audio`, `input`, `runtime`, `ui`, then `debug`.
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
`video.portrait=1` is a frontend presentation transform: the guest framebuffer
stays 320x240, while SDL rendering, screenshot output, and virtual-control
coordinates rotate 90 degrees counter-clockwise.
`input.disable_ime=1` is the default. It keeps Windows input methods detached
from the SDL window unless the user disables the option from the Input menu.
`input.keyboard_mapping` and `input.controller_mapping` follow the same Input
menu group. Empty means the built-in keyboard or SDL GameController defaults;
non-empty values store only custom differences as comma-separated
`Physical=Control` pairs. The frontend/input layer rebuilds the runtime maps
from defaults plus these overrides and releases active synthetic controls
before applying a new map.

`runtime.speed_scale=` means `Auto` in the INI and menu. Auto does not set
`DINGOO_PIE_RUNTIME_SPEED_SCALE`; the runtime maps that unset state to the
global 65% pace. Explicit menu values write their numeric scale into the INI
and the runtime environment.
`runtime.cpu_hz=` means `Auto`; explicit CPU clock menu values write the
selected IR JIT clock to the INI and `DINGOO_PIE_IRJIT_CLOCK_HZ`.
`runtime.backend=` means `Auto` and resolves through the backend parser to
PPSSPP IR JIT. `runtime.ostimedly_scale=` means `Auto` and uses the global
delay scale of 1.0 unless a compatibility profile supplies a narrower app
override.
`runtime.cheats_enabled=0` is the default. The frontend persists this global
cheat switch and the selected cheat feature names per game. Individual cheat
features remain unchecked until selected by the user, then restore when the same
game is loaded again. Cheat lookup uses the app base name, such as
`GameName.app` -> `cheats\GameName.cht`. The optional `app_sha256` field is
validation only and never a lookup key. Missing cheat files are silent and do
not create a `cheats` directory; SHA mismatches disable the loaded file and show
the user a warning.
`audio.buffer_samples` controls only the SDL output device buffer request; the
guest SDK still supplies waveout sample rate, sample format, and channel count.

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

- `DINGOO_PIE_PROFILE=1` for frontend, HLE, file-system, and JIT counters.
- `DINGOO_PIE_INPUT_TRACE=1` for keyboard and virtual-control input.
- `DINGOO_PIE_TRACE_HLE=1` for selected HLE calls.
- `DINGOO_PIE_TRACE_TASKS=1` for guest task stop paths.
- `DINGOO_PIE_TRACE_FS=1` or `DINGOO_PIE_TRACE_FS_OPEN=1` for file activity.
- `DINGOO_PIE_IRJIT_TRACE=1` for noisy PPSSPP shim diagnostics.

`scripts/smoke_test.ps1` is the preferred repeatable check after structural
changes. It can run a sample for a fixed duration, capture stdout/stderr logs,
optionally hide the user config, and summarize whether expected diagnostics
appeared.
