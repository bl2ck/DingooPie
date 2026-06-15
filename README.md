# DingooPie

## English

DingooPie is a Windows HLE emulator for Dingoo Technology `.app` games used by
the Dingoo A320 handheld and compatible Gemei X760+ handheld software.
`.app` game files are a Dingoo Technology-owned package format. This project
does not ship game samples; users must provide legally obtained files.

The runtime uses the PPSSPP IR/x64 JIT backend for normal execution, keeps an
in-tree MIPS32 interpreter for correctness checks and diagnostics, and uses
SDL2 for video and audio.

Windows version information:

- File description: Dingoo A320 / Gemei X760+ Game Emulator
- File version: 1.0
- Product name: DingooPie
- Powered by: BL2CK
- Copyright: Copyright (c) BL2CK 2026

### Current Frontend Behavior

Open games from `File > Open Game .app`; the selected path is saved to
`recent.last_app`.
Manual INI edits may use UTF-16LE with BOM or UTF-8 with or without BOM;
Chinese app paths are supported.
A `.app` path can also be passed on the command line and takes priority over
the saved recent game:

```powershell
.\DingooPie.exe "D:\Games\Dingoo\Your Game.app"
```

The frontend menu is ordered as `File`, `Options`, `Settings`, `Debug`, and
`Help`; `Options` contains the `Video`, `Audio`, and `Input` submenus. It
provides game loading, exit confirmation, screenshot export, screen scaling,
windowed fullscreen, sampling mode, video filters, brightness, contrast,
saturation, portrait mode, FPS overlay, virtual controls, IME control, language
switching, CPU backend selection, CPU clock, runtime speed, SDK delay scaling,
master volume, audio disable, debug console, profile logging, log opening,
settings save/reset, and About.
`Debug > Open Debug Log` shows a message if the log file has not been
created yet. Profile logging creates that file next to `DingooPie.exe`.

The UI language can be switched between English and Chinese from
`Settings > Language`. The default language is English, and the selected
language is saved with the emulator configuration.

Screenshots can be saved as PNG, JPG, or BMP. Automatic screenshots use `F12`
and include a timestamped file name.

### Keyboard Mapping

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
The internal control names follow the Dingoo A320 SDK. Gemei X760+ face-button
input is mapped onto the same A/B/X/Y control bits, while Dingoo-only START,
SELECT, and shoulder controls remain available through the host keyboard and
virtual controls.

### Video And Audio

The Options > Video > Scale submenu provides 1x, 2x, 3x, and a windowed fullscreen mode
that preserves the top menu bar and fills the current monitor work area. The Video menu also provides nearest/linear sampling,
grayscale, invert, sepia, amber, LCD scanline, sharpen, soft blur, brightness,
contrast, saturation, and portrait mode controls. Window scale, fullscreen,
sampling, effects, adjustments, portrait mode, and the FPS overlay save and
apply immediately without restarting the emulator. Portrait mode rotates the
display 90 degrees counter-clockwise, swaps the window to 240x320, and rotates
the virtual controls with the screen. The FPS overlay is disabled by default. When enabled, the top-left
overlay shows game frame submissions per second rather than host window refresh
rate.

The Options > Input menu controls the virtual button overlay and whether Windows IME is
disabled for the SDL window. The virtual D-pad includes diagonal corner buttons
that press the matching two directions together. `Disable IME` is enabled by
default so Chinese or other input methods do not intercept gameplay keys.

The Options > Audio menu provides master volume control and an audio disable toggle.
CPU clock, runtime speed, SDK delay scale, master volume, audio disable, and
profile logging save and apply immediately. Changing the CPU backend still
relaunches the emulator because the execution backend is selected at startup.
The default runtime options use Auto for backend, CPU clock, runtime speed,
and SDK delay scale. Auto maps to PPSSPP IR JIT, 336 MHz, 60% runtime speed,
and the original SDK delay duration.
Guest-side volume changes are combined with the host master volume before
playback.

### Source Layout

All project source files live in `dingoo_pie/`.

- `main.cpp`: process startup and high-level boot flow.
- `emulator_core.*`: app loading, native runtime setup, AppMain injection, and fatal diagnostics.
- `native_runtime.*`: backend selection, in-tree MIPS32 CPU, memory map, register, and hook runtime.
- `ppsspp_irjit_backend.*` and `ppsspp_shim.cpp`: PPSSPP IR/x64 JIT adapter and Dingoo memory shim.
- `instruction_compat.*`: precise break/cache compatibility hooks without mutating packed app data.
- `compat_profile.*`: content-hash compatibility profiles for sample-specific timing, resource, and exit behavior.
- `sdk_hle.*`: Dingoo SDK import bridge used by A320 and X760+ app software.
- `app_loader.*`: Dingoo Technology CCDL/IMPT/EXPT/RAWD app parsing and resource lookup.
- `emulated_memory.*`: guest heap, stack, register, alias, and pointer mapping.
- `guest_filesystem.*`: virtual file and resource-backed file access.
- `task_scheduler.*`: Dingoo SDK task creation backed by host pthreads.
- `sdl_frontend.*`: SDL2 window, input polling, and framebuffer presentation.
- `frontend_menu.*`: native Windows menu construction and command dispatch.
- `ui_strings.*`: English/Chinese frontend menu and dialog text.
- `sdl_audio.*` and `guest_audio.*`: SDL audio output and waveout bridge.
- `platform_win32.*`: file picker, path encoding, and working-directory setup.
- `input_controls.*`: SDL key mapping to Dingoo A320 and Gemei X760+ button state.
- `guest_format.*`: guest `sprintf` compatibility.
- `runtime_debug.*`: register, memory, and disassembly diagnostics.
- `patches/`: project-owned patches applied to third-party source trees during bootstrap.
- `scripts/`: bootstrap, build, smoke-test, and packaging automation.
- `docs/`: architecture, debugging, and packaging notes.

See `docs\ARCHITECTURE.md` for runtime flow, compatibility policy, and the
configuration lifecycle.

### Build

From a clean package:

```powershell
Set-ExecutionPolicy -Scope Process Bypass -Force
.\scripts\bootstrap_windows.ps1
.\scripts\build_release.ps1
```

`bootstrap_windows.ps1` downloads or reuses cached copies of w64devkit, SDL2,
Capstone, and PPSSPP. It then applies `patches\ppsspp-irjit-dingoo.patch` to
the PPSSPP source tree.

If the current workspace already contains the shared `work\downloads` cache,
the script reuses it before attempting network downloads.

### Runtime Diagnostics

```powershell
set DINGOO_PIE_PROFILE=1
set DINGOO_PIE_INPUT_TRACE=1
set DINGOO_PIE_COMPAT_TRACE=1
set DINGOO_PIE_IRJIT_TRACE=1
set DINGOO_PIE_BACKEND=ppsspp_irjit
DingooPie.exe "D:\Games\Dingoo\Your Game.app"
```

`DINGOO_PIE_BACKEND=ppsspp_irjit` selects the PPSSPP IR JIT backend. Omit it or
set it to `interpreter` to use the native interpreter. `DINGOO_PIE_PROFILE=1`
prints low-frequency frontend, bridge, and compatibility statistics.
`DINGOO_PIE_INPUT_TRACE=1` prints host-to-Dingoo input state changes.
`DINGOO_PIE_COMPAT_TRACE=1` prints unique compatibility instruction hits.
`DINGOO_PIE_IRJIT_TRACE=1` prints JIT adapter diagnostics.

See `docs\DEBUGGING.md` for supported environment variables, smoke-test
commands, and current sample baselines.

### Packaging

```powershell
.\scripts\package_project.ps1
```

The packaging script stages source, scripts, docs, patches, resources, and the
release binary set. It generates `manifest.files.txt` and `manifest.sha256`,
compresses the package, extracts it to a temporary verification directory,
validates every hash, checks required files, and fails if any `.app`/`.apk`
game file, log, or debug dump artifact is present.

See `docs\PACKAGING.md` for package contents and policy.

## 中文

DingooPie 是 Windows 平台的高层模拟器，用于运行丁果科技 `.app` 游戏文件，
目标设备包括丁果 A320 掌机，并补充支持兼容的歌美 X760+ 掌机游戏。
`.app` 游戏文件格式归丁果科技所属。本项目不附带游戏样本，用户需自行提供
合法取得的文件。

当前运行时默认使用 PPSSPP IR/x64 JIT 后端执行游戏，保留项目内置 MIPS32
解释器用于正确性检查和诊断，并使用 SDL2 负责视频与音频输出。

Windows 版本信息：

- 文件说明：Dingoo A320 / Gemei X760+ Game Emulator
- 文件版本：1.0
- 产品名称：DingooPie
- Powered by：BL2CK
- 版权：Copyright (c) BL2CK 2026

### 当前前端行为

可通过 `File > Open Game .app` 打开游戏；所选路径会保存到
`recent.last_app`。
手动编辑 INI 时可使用带 BOM 的 UTF-16LE，或带/不带 BOM 的 UTF-8；
支持中文 app 路径。
也可以在命令行传入 `.app` 路径，且优先于已保存的最近游戏：

```powershell
.\DingooPie.exe "D:\Games\Dingoo\Your Game.app"
```

当前前端菜单顺序为 `File`、`Options`、`Settings`、`Debug` 和 `Help`；
`Options` 下包含 `Video`、`Audio` 和 `Input` 子菜单。菜单提供打开游戏、
退出确认、截图导出、画面缩放、采样方式、窗口化全屏、滤镜效果、亮度、
对比度、饱和度、FPS 显示、虚拟按键、禁用输入法、语言切换、CPU 后端选择、
运行速度、`OSTimeDly` 比例、主音量、禁用音频、调试控制台、性能日志、
打开日志、保存设置、恢复默认设置和关于信息。
如果 `Debug > 打开调试日志` 对应的日志尚未生成，程序会弹出提示。

界面语言可以在 `Settings > Language` 中切换 English / 中文。默认语言为
English，选择后的语言会随模拟器配置一起保存。

截图可以保存为 PNG、JPG 或 BMP。按 `F12` 自动截图时，文件名会自动带时间戳。

### 按键映射

| 键盘按键 | 丁果 A320 / 歌美 X760+ 控制 |
| --- | --- |
| `WASD` / 方向键 | 方向键 |
| `L` | A |
| `K` | B |
| `I` | X |
| `J` | Y |
| `1` / `Q` | SELECT |
| `0` / `O` | START |
| 左 Shift | 左肩键 |
| 右 Shift | 右肩键 |
| Esc | 询问是否退出模拟器 |
| F12 | 自动截图 |

`Enter` 当前没有映射到任何 Dingoo 按键，避免和游戏输入产生冲突。
内部控制名称沿用丁果 A320 SDK。歌美 X760+ 的面部按键会映射到同一组
A/B/X/Y 控制位；丁果 A320 独有的 START、SELECT 和肩键仍可通过宿主键盘
或虚拟按键使用。

### 画面与声音

Options > Video > 缩放 子菜单提供 1x、2x、3x 窗口缩放和保留顶部菜单栏、铺满当前显示器工作区的窗口化全屏。Video 菜单还提供最近邻/线性采样、黑白、反色、
怀旧褐色、琥珀屏、LCD 扫描线、锐化、柔化、亮度、对比度和饱和度调节。修改窗口缩放或全屏会保存设置并重启模拟器；如果已加载游戏，会带同一个 app 路径重启；如果未加载游戏，会重启回到前端主界面。FPS 显示默认关闭；
开启后左上角显示的是游戏提交帧率，不是宿主窗口刷新率。

Options > Input 菜单提供虚拟按键显示和禁用 Windows 输入法开关。`禁用输入法` 默认开启，避免中文或其他输入法拦截游戏按键。

Options > Audio 菜单提供主音量调节和禁用音频开关。游戏内部音量会和宿主主音量
合并后再输出。

### 源码目录

所有项目源码都位于 `dingoo_pie/` 目录。

- `main.cpp`：进程启动和整体启动流程。
- `emulator_core.*`：游戏加载、运行时初始化、AppMain 注入和致命错误诊断。
- `native_runtime.*`：后端选择、内置 MIPS32 CPU、内存映射、寄存器和 hook 运行时。
- `ppsspp_irjit_backend.*` 与 `ppsspp_shim.cpp`：PPSSPP IR/x64 JIT 适配层和 Dingoo 内存桥接。
- `instruction_compat.*`：精确处理 break/cache 等兼容指令，不直接修改打包后的游戏数据。
- `compat_profile.*`：基于内容 hash 的兼容性配置，用于样本相关的时序、资源和退出行为。
- `sdk_hle.*`：丁果 SDK 导入桥接，供 A320 与 X760+ app 软件使用。
- `app_loader.*`：解析丁果科技 CCDL/IMPT/EXPT/RAWD app 格式并查找资源。
- `emulated_memory.*`：模拟堆、栈、寄存器、地址别名和指针映射。
- `guest_filesystem.*`：虚拟文件和资源文件访问。
- `task_scheduler.*`：使用宿主线程模拟丁果 SDK 任务创建。
- `sdl_frontend.*`：SDL2 窗口、输入轮询和帧缓冲显示。
- `frontend_menu.*`：Windows 原生菜单创建和命令分发。
- `ui_strings.*`：英文/中文菜单与对话框文本。
- `sdl_audio.*` 与 `guest_audio.*`：SDL 音频输出和 waveout 桥接。
- `platform_win32.*`：文件选择器、路径编码和工作目录设置。
- `input_controls.*`：SDL 键盘输入到丁果 A320 / 歌美 X760+ 按键状态的映射。
- `guest_format.*`：兼容 guest 侧 `sprintf`。
- `runtime_debug.*`：寄存器、内存和反汇编诊断。
- `patches/`：项目维护的第三方源码补丁。
- `scripts/`：依赖下载、构建、冒烟测试和打包脚本。
- `docs/`：架构、调试和打包说明。

运行流程、兼容策略和配置生命周期见 `docs\ARCHITECTURE.md`。

### 构建

从干净源码包构建：

```powershell
Set-ExecutionPolicy -Scope Process Bypass -Force
.\scripts\bootstrap_windows.ps1
.\scripts\build_release.ps1
```

`bootstrap_windows.ps1` 会下载或复用缓存中的 w64devkit、SDL2、Capstone 和
PPSSPP，然后把 `patches\ppsspp-irjit-dingoo.patch` 应用到 PPSSPP 源码树。

如果当前工作区已经有共享的 `work\downloads` 缓存，脚本会优先复用缓存，
再尝试网络下载。

### 运行时诊断

```powershell
set DINGOO_PIE_PROFILE=1
set DINGOO_PIE_INPUT_TRACE=1
set DINGOO_PIE_COMPAT_TRACE=1
set DINGOO_PIE_IRJIT_TRACE=1
set DINGOO_PIE_BACKEND=ppsspp_irjit
DingooPie.exe "D:\Games\Dingoo\Your Game.app"
```

`DINGOO_PIE_BACKEND=ppsspp_irjit` 选择 PPSSPP IR JIT 后端；不设置或设为
`interpreter` 时使用内置解释器。`DINGOO_PIE_PROFILE=1` 输出低频前端、
桥接和兼容性统计。`DINGOO_PIE_INPUT_TRACE=1` 输出宿主键盘到 Dingoo
按键状态的变化。`DINGOO_PIE_COMPAT_TRACE=1` 输出唯一兼容性指令命中。
`DINGOO_PIE_IRJIT_TRACE=1` 输出 JIT 适配层诊断。

支持的环境变量、冒烟测试命令和当前样本基线见 `docs\DEBUGGING.md`。

### 打包

```powershell
.\scripts\package_project.ps1
```

打包脚本会收集源码、脚本、文档、补丁、资源和 release 可运行文件，
生成 `manifest.files.txt` 与 `manifest.sha256`，压缩后再解压到临时
校验目录，逐项验证 hash、必需文件，并在发现任何 `.app`/`.apk` 游戏文件、
日志或调试截图/转储文件时失败。

包内容和策略见 `docs\PACKAGING.md`。
