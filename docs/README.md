# DingooPie

## 中文

丁果派 DingooPie 是 Windows 平台的高层模拟器，用于运行丁果科技 `.app`
游戏文件，目标设备包括丁果 A320 掌机，并补充支持兼容的歌美 X760+
掌机游戏。`.app` 格式文件归丁果科技所有。本项目不附带游戏样本，
用户需自行提供合法取得的文件。

Windows 版本信息：

- 文件说明：Dingoo Game Emulator
- 文件版本：1.6
- 产品名称：丁果派 DingooPie
- Powered by：BL2CK Software
- 版权：Copyright (c) 2026 BL2CK

### 快速使用

可通过 `文件 > 打开游戏` 打开游戏，也可以把 `.app` 文件拖到模拟器窗口。
最近游戏会显示在 `文件 > 最近游戏`，可在该子菜单中清空。未运行游戏时窗口会
显示 DingooPie 待机背景；打开游戏后会自动切换到游戏画面。

也可以在命令行传入 `.app` 路径：

```powershell
.\DingooPie.exe "D:\Games\Dingoo\Your Game.app"
```

### 默认设置

| 项目 | 默认值 |
| --- | --- |
| 窗口缩放 | 2x |
| 全屏 | 关闭 |
| 抗锯齿 | 关闭 |
| 滤镜 | 正常 |
| 亮度 / 对比度 / 伽马 / 饱和度 | 100% / 100% / 100% / 100% |
| 竖屏模式 | 关闭 |
| FPS 显示 | 关闭 |
| 主音量 | 100% |
| 音频缓冲 | 2048 采样 |
| 音频效果 | 关闭 |
| 禁用音频 | 关闭 |
| 禁用输入法 | 开启 |
| 虚拟按键 | 关闭 |
| CPU 后端 | 自动，默认使用 PPSSPP IR JIT |
| CPU 时钟 | 自动，默认 336 MHz |
| 运行速度 | 自动，默认 65% |
| 延迟比例 | 自动，默认 1.0 |
| 金手指 | 关闭 |
| 界面语言 | 中文 |
| 调试控制台 | 关闭 |
| 性能日志 | 关闭 |
| 资源监视器自动打开 | 关闭 |

### 菜单与配置

当前中文前端菜单顺序为 `文件`、`选项`、`设置`、`调试` 和 `帮助`。

- `文件`：打开游戏、最近游戏/清除最近游戏、重启游戏、暂停/恢复游戏、保存截图、保存存档/读取存档、存档管理器、退出。
- `选项 > 视频`：1x、2x、3x、窗口化全屏、抗锯齿、正常、黑白、反色、柔化、锐化、色彩增强、怀旧褐色、像素网格、LCD 扫描线、轻量 CRT、亮度、对比度、伽马、饱和度、最小化时、竖屏模式和显示 FPS。
- `选项 > 音频`：主音量、音频缓冲、音频效果和禁用音频。
- `选项 > 输入`：禁用输入法、显示虚拟按键和按键映射。
- `设置`：CPU 后端、CPU 时钟、运行速度、延迟比例、金手指、语言和恢复默认设置。
- `调试`：调试控制台、性能日志、打开调试日志、资源监视器、内存搜索器和调试器。
- `帮助`：关于 丁果派 DingooPie。

设置会自动保存。视频、音频、输入、CPU 时钟、运行速度、延迟比例、金手指、
语言和调试选项会立即生效。修改 CPU 后端会自动重启当前游戏。
暂停/恢复游戏只冻结当前游戏执行和音频输出。
即时存档提供 15 个档位，存档文件名使用游戏名，格式为
`游戏名.slot1.dps`，保存到游戏所在目录旁的 `savestates` 文件夹。菜单会显示
已有档位的保存时间，精确到秒。`文件 > 存档管理器` 可查看档位和缩略图，
也可保存、读取、删除或打开存档目录。保存和读取前都会询问确认；读取时会
校验游戏和运行状态格式。如果当前还在标题、选择等不同阶段，请先进入与存档
相同的场景再读取。

`选项 > 输入 > 按键映射` 会打开键盘和手柄共用的独立窗口。选择某一行的
“设置键盘”或“设置手柄”后，按下目标键盘键、手柄按键、摇杆方向或扳机即可
绑定。恢复默认按钮会清空对应设备的自定义配置。

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
支持 SDL GameController 兼容手柄：十字键/左摇杆映射方向键，A/B/X/Y
映射同名 Dingoo 按键，Back/Start 映射 SELECT/START，肩键和扳机映射左右肩键。

### 金手指

金手指按当前游戏同名 `.cht` 文件加载，即 `游戏名.app` 对应
`cheats\游戏名.cht`。没有同名文件时游戏正常运行；如果金手指文件不匹配当前游戏，
模拟器会提示并停用该文件。金手指总开关默认关闭；具体功能默认不勾选，勾选状态
按游戏保存，并在下次启动同一游戏时自动恢复。

界面语言可以在 `设置 > 语言` 中切换 English / 中文。截图可以保存为 PNG、JPG
或 BMP；按 `F12` 自动截图时，文件名会自动带时间戳。

`调试 > 打开调试日志` 会打开当前实例的调试日志。运行时崩溃会额外生成诊断日志。

### 调试工具

- `调试 > 调试控制台`：显示调试输出窗口。
- `调试 > 性能日志`：记录运行时性能统计。
- `调试 > 打开调试日志`：打开当前实例的调试日志文件。
- `调试 > 资源监视器`：查看游戏运行中加载的内部资源和外部文件；上方/下方列表分别显示已加载和已卸载条目，状态栏显示读取次数和读取字节；勾选后会立即打开，之后启动游戏时也会自动打开。
- `调试 > 内存搜索器`：搜索 u8/u16/u32 数值，用变化条件缩小候选；选中地址可刷新当前值、写入一次或复制为同名 `.cht` 文件可用的记录。内存搜索器需要游戏运行中才能打开。
- `调试 > 调试器`：打开运行时检查窗口，显示反汇编、寄存器、内存字节、断点命中次数和写入监视。断点和写入监视只记录命中，不会暂停或单步 CPU。调试器需要游戏运行中才能打开。

### 源码目录

所有项目源码都位于 `dingoo_pie/` 目录。

- `main.cpp`：进程启动和整体启动流程。
- `emulator_core.*`：游戏加载、运行时初始化、AppMain 入口调用和致命错误诊断。
- `native_runtime.*`：后端选择、内置 MIPS32 CPU、内存映射、寄存器和运行时回调。
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
- `resource_monitor_ui.*` 与 `runtime_resource_monitor.*`：资源监视器窗口、运行时资源加载快照和高亮状态。
- `ui_strings.*`：英文/中文菜单与对话框文本。
- `sdl_audio.*` 与 `guest_audio.*`：SDL 音频输出和 waveout 桥接。
- `platform_win32.*`：文件选择器、路径编码和工作目录设置。
- `input_controls.*`：SDL 键盘输入到丁果 A320 / 歌美 X760+ 按键状态的映射。
- `guest_format.*`：兼容 guest 侧 `sprintf`。
- `runtime_debug.*`：寄存器、内存和反汇编诊断。
- `patches/`：项目维护的第三方源码补丁。
- `scripts/`：依赖下载、构建、测试和打包脚本。
- `docs/`：架构、调试和打包说明。

运行流程、兼容策略和配置生命周期见 `docs\ARCHITECTURE.md`。

### 构建

从干净源码包构建：

```powershell
Set-ExecutionPolicy -Scope Process Bypass -Force
.\scripts\bootstrap_windows.ps1
.\scripts\build_release.ps1
```

`bootstrap_windows.ps1` 会下载或复用缓存中的 w64devkit、SDL2、Capstone、
MinGW winpthread runtime 和 PPSSPP，然后把
`patches\ppsspp-irjit-dingoo.patch` 应用到 PPSSPP 源码树。

`build_release.ps1` 会生成可直接运行的发布目录，包含 `DingooPie.exe`、
`SDL2.dll`、`libcapstone.dll`、`libwinpthread-1.dll`、`README.md`、
按需包含的 `cheats\`。缺少必要运行时 DLL 会导致发布失败。

如果当前工作区已经有共享的 `work\downloads` 缓存，脚本会优先复用缓存，
再尝试网络下载。

### 运行时诊断

常规诊断优先使用 `调试` 菜单中的调试控制台、性能日志、调试日志、资源监视器、
内存搜索器和调试器。开发诊断细节见 `docs\DEBUGGING.md`。

### 打包

```powershell
.\scripts\package_project.ps1
```

打包脚本会收集源码、脚本、文档、补丁和资源，不包含本地生成的
`release/` 发布产物。脚本会压缩后再解压到临时校验目录，验证必需文件，
并在发现任何样本 app 文件、日志、调试截图或生成的分析产物时失败。

包内容和策略见 `docs\PACKAGING.md`。

## English

DingooPie is a Windows HLE emulator for Dingoo Technology `.app` game files
used by the Dingoo A320 handheld and compatible Gemei X760+ handheld software.
`.app` game files are a Dingoo Technology-owned package format. This project
does not ship game samples; users must provide legally obtained files.

Windows version information:

- File description: Dingoo Game Emulator
- File version: 1.6
- Product name: DingooPie
- Powered by: BL2CK Software
- Copyright: Copyright (c) 2026 BL2CK

### Quick Start

Open games from `File > Open Game`, or drop an `.app` file onto the emulator
window. Recent games appear under `File > Recent Games` and can be cleared from
that submenu. When no game is running, the window shows the DingooPie idle
background; opening a game switches to gameplay automatically.

A `.app` path can also be passed on the command line:

```powershell
.\DingooPie.exe "D:\Games\Dingoo\Your Game.app"
```

### Default Settings

| Item | Default |
| --- | --- |
| Window scale | 2x |
| Fullscreen | Off |
| Anti-aliasing | Off |
| Effect | Normal |
| Brightness / contrast / gamma / saturation | 100% / 100% / 100% / 100% |
| Portrait mode | Off |
| FPS overlay | Off |
| Master volume | 100% |
| Audio buffer | 2048 samples |
| Audio effect | Off |
| Disable audio | Off |
| Disable IME | On |
| Virtual controls | Off |
| CPU backend | Auto, defaults to PPSSPP IR JIT |
| CPU Clock | Auto, defaults to 336 MHz |
| Runtime Speed | Auto, defaults to 65% |
| Delay Scale | Auto, defaults to 1.0 |
| Cheats | Off |
| UI language | Chinese |
| Debug Console | Off |
| Performance log | Off |
| Resource Monitor auto-open | Off |

### Menu And Configuration

The frontend menu is ordered as `File`, `Options`, `Settings`, `Debug`, and
`Help`; the Chinese UI displays them as `文件`, `选项`, `设置`, `调试`, and
`帮助`.

- `File`: Open Game, Recent Games/Clear Recent Games, Restart Game, Pause/Resume Game, Save Screenshot, Save Slot/Load Slot, Save Manager, and Exit.
- `Options > Video`: 1x, 2x, 3x, windowed fullscreen, anti-aliasing, normal, black and white, invert, soft blur, sharpen, vivid, sepia, pixel grid, LCD scanline, light CRT, brightness, contrast, gamma, saturation, minimized behavior, portrait mode, and FPS overlay.
- `Options > Audio`: master volume, audio buffer, audio effect, and disable audio.
- `Options > Input`: disable IME, show virtual controls, and input mapping.
- `Settings`: CPU Backend, CPU Clock, Runtime Speed, Delay Scale, Cheats, Cheat Manager, Language, and Restore Default Settings.
- `Debug`: Debug Console, Performance Log, Open Debug Log, Resource Monitor, Memory Searcher, and Debugger.
- `Help`: About DingooPie.

Settings are saved automatically. Video, audio, input, CPU clock, runtime speed,
delay scale, cheats, language, and debug options apply immediately. Changing the
CPU backend automatically restarts the current game. Pause/Resume Game freezes
current game execution and audio output.
Save/Load State provides 15 slots per game. Save files use names like
`GameName.slot1.dps` and are stored in a `savestates` folder next to the game
file. The menu shows saved slot times down to seconds. `File > Save Manager`
can view slots and thumbnails, save, load, delete, or open the save-state
folder. Saving and loading ask for confirmation first. Loading validates the
game and runtime-state layout. If the game is still at a title or selection
stage, enter the same scene as the saved state before loading.

`Options > Input > Input Mapping` opens a standalone keyboard/controller mapping
window. Choose `Set Key` or `Set Gamepad` on a control row, then press the target
keyboard key, controller button, stick direction, or trigger. Restore defaults
clears the custom mapping for that device.

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
| Esc | Ask before exiting the emulator |
| F12 | Save automatic screenshot |

`Enter` is intentionally unmapped so it cannot conflict with game input.
SDL GameController-compatible pads are supported: D-pad/left stick map to the
D-pad, A/B/X/Y map to the matching Dingoo buttons, Back/Start map to
SELECT/START, and shoulder buttons/triggers map to the shoulders. Use
`Options > Input > Input Mapping` to customize keyboard and controller inputs.

### Cheats

Cheats are loaded from the same-name `.cht` file for the running app:
`GameName.app` maps to `cheats\GameName.cht`. If no same-name file exists, the
game runs normally. If a cheat file does not match the current game, DingooPie
warns and disables that file.
The global cheat switch is off by default. Individual features start unchecked;
selections are saved per game and restored when the same game starts again.

The UI language can be switched between English and Chinese from
`Settings > Language`. Screenshots can be saved as PNG, JPG, or BMP. Automatic
screenshots use `F12` and include a timestamped file name.

`Debug > Open Debug Log` opens the current debug log. Runtime crashes also write
an additional diagnostic log.

### Debug Tools

- `Debug > Debug Console`: shows the debug output window.
- `Debug > Performance Log`: records runtime performance counters.
- `Debug > Open Debug Log`: opens the current debug log file.
- `Debug > Resource Monitor`: shows internal resources and external files while a game is running; upper/lower lists show loaded and unloaded entries, and the status line reports read count and read bytes. When checked, it opens immediately and automatically for later games.
- `Debug > Memory Searcher`: searches u8/u16/u32 values and narrows candidates by value changes; selected addresses can be refreshed, written once, or copied as `.cht` records. Memory Searcher is available while a game is running.
- `Debug > Debugger`: opens a live inspection panel with disassembly, registers, memory bytes, PC hit counters, and write hits. PC hits and write hits only record hits and do not pause or single-step the CPU. Debugger is available while a game is running.

### Source Layout

All project source files live in `dingoo_pie/`.

- `main.cpp`: process startup and high-level boot flow.
- `emulator_core.*`: app loading, runtime initialization, AppMain handoff, and fatal diagnostics.
- `native_runtime.*`: backend selection, in-tree MIPS32 CPU, memory map, register, and runtime callback support.
- `ppsspp_irjit_backend.*` and `ppsspp_shim.cpp`: PPSSPP IR/x64 JIT adapter and Dingoo memory shim.
- `instruction_compat.*`: precise break/cache compatibility handling without mutating packed app data.
- `compat_profile.*`: content-hash compatibility profiles for sample-specific timing, resource, and exit behavior.
- `sdk_hle.*`: Dingoo SDK import bridge used by A320 and X760+ app software.
- `app_loader.*`: Dingoo Technology CCDL/IMPT/EXPT/RAWD app parsing and resource lookup.
- `emulated_memory.*`: guest heap, stack, register, alias, and pointer mapping.
- `guest_filesystem.*`: virtual file and resource-backed file access.
- `task_scheduler.*`: Dingoo SDK task creation backed by host pthreads.
- `sdl_frontend.*`: SDL2 window, input polling, and framebuffer presentation.
- `frontend_menu.*`: native Windows menu construction and command dispatch.
- `resource_monitor_ui.*` and `runtime_resource_monitor.*`: Resource Monitor window, runtime resource-load snapshots, and highlight state.
- `ui_strings.*`: English/Chinese frontend menu and dialog text.
- `sdl_audio.*` and `guest_audio.*`: SDL audio output and waveout bridge.
- `platform_win32.*`: file picker, path encoding, and working-directory setup.
- `input_controls.*`: SDL key mapping to Dingoo A320 and Gemei X760+ button state.
- `guest_format.*`: guest `sprintf` compatibility.
- `runtime_debug.*`: register, memory, and disassembly diagnostics.
- `patches/`: project-owned patches applied to third-party source trees during bootstrap.
- `scripts/`: bootstrap, build, test, and packaging scripts.
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
Capstone, the MinGW winpthread runtime, and PPSSPP. It then applies
`patches\ppsspp-irjit-dingoo.patch` to the PPSSPP source tree.

`build_release.ps1` creates a runnable release with `DingooPie.exe`, `SDL2.dll`,
`libcapstone.dll`, `libwinpthread-1.dll`, `README.md`, optional `cheats\`, and
other required runtime files. Missing required runtime DLLs fail the release step.

If the current workspace already contains the shared `work\downloads` cache,
the script reuses it before attempting network downloads.

### Runtime Diagnostics

For normal diagnostics, use the Debug menu: Debug Console, Performance Log,
Open Debug Log, Resource Monitor, Memory Searcher, and Debugger. See
`docs\DEBUGGING.md` for development diagnostics.

### Packaging

```powershell
.\scripts\package_project.ps1
```

The packaging script stages source, scripts, docs, patches, and resources; it
does not include locally generated `release/` artifacts. It compresses the
package, extracts it to a temporary verification directory, checks required
files, and fails if any sample app file, log, debug screenshot, or generated
analysis artifact is present.

See `docs\PACKAGING.md` for package contents and policy.
