# DingooPie

## 中文

丁果派 DingooPie 是 Windows 平台的丁果游戏模拟器，用于运行丁果 A320、
歌美 X760+、歌美 A330 的 `.app` 与 `.cc` 游戏。游戏文件格式归原厂商所有，
本项目不附带游戏样本，用户需自行提供合法取得的文件。

Windows 版本信息：

- 文件说明：Dingoo Game Emulator
- 文件版本：1.7
- 产品名称：丁果派 DingooPie
- Powered by：BL2CK Software
- 版权：Copyright (c) 2026 BL2CK
- 作者主页：[bl2ck](https://github.com/bl2ck)
- 项目主页：[bl2ck/DingooPie](https://github.com/bl2ck/DingooPie)

### 快速使用

可通过 `文件 > 打开游戏` 打开游戏，也可以把 `.app` 或 `.cc` 文件拖到模拟器窗口。
最近游戏会显示在 `文件 > 最近游戏`，可在该子菜单中清空。未运行游戏时窗口会
显示 DingooPie 待机背景；打开游戏后会自动切换到游戏画面。

命令行示例：

```powershell
.\DingooPie.exe "D:\Games\Dingoo\Your Game.app"
.\DingooPie.exe --game "D:\Games\Dingoo\Your Game.cc"
.\DingooPie.exe --game "D:\Games\Dingoo\Your Game.cc" --config "D:\DingooPie\Portable.ini" --no-recent
```

命令行格式为 `DingooPie.exe [选项] [game.app|game.cc]`。支持
`-g/--game <路径>`、`-c/--config <路径>`、`--no-recent`、`-h/--help`、
`-V/--version` 和 `--`。游戏路径只能指定一次，含空格时必须加引号。
`--config` 指定本次运行使用的配置文件；`--no-recent` 只跳过本次最近游戏
自动启动，不会清空列表。

### 默认设置

| 项目 | 默认值 |
| --- | --- |
| 窗口缩放 | 2x |
| 全屏 | 关闭 |
| 抗锯齿 | 关闭 |
| 滤镜 | 正常 |
| 亮度 / 对比度 / 伽马 / 饱和度 | 100% / 100% / 100% / 100% |
| 最小化时 | 自动暂停 |
| 屏幕方向 | 横屏 |
| 画面填充 | 保持比例 |
| 显示 FPS | 关闭 |
| 主音量 | 100% |
| 音频缓冲 | 2048 采样 |
| 音频缓冲延迟 | 自动 |
| 音频效果 | 关闭 |
| 数字降噪 | 高 |
| 禁用音频 | 关闭 |
| 禁用系统输入法 | 开启 |
| 显示虚拟按键 | 关闭 |
| 虚拟按键大小 | 100% |
| 方向键类型 | 摇杆 |
| CPU 执行模式 | 自动；APP 使用 PPSSPP IR JIT，CC 使用 Dynarmic |
| CPU 时钟 | 自动，使用 336 MHz 参考时钟 |
| 游戏速度 | 自动，使用 65% 全局速度比例 |
| 系统延迟比例 | 自动，使用 1.0 全局比例 |
| 金手指 | 关闭 |
| 界面语言 | 中文 |
| 调试控制台 | 关闭 |
| 性能日志 | 关闭 |
| 资源监视器自动打开 | 关闭 |

### 菜单与配置

当前中文前端菜单顺序为 `文件`、`选项`、`设置`、`调试` 和 `帮助`。

- `文件`：打开游戏、最近游戏/清除最近游戏、重启游戏、暂停/恢复游戏、保存截图、保存即时存档/读取即时存档、存档管理器、退出模拟器。
- `选项 > 视频`：1x、2x、3x、全屏、抗锯齿、正常、黑白、反色、柔化、锐化、色彩增强、怀旧褐色、像素网格、LCD 扫描线、轻量 CRT、亮度、对比度、伽马、饱和度、最小化时、屏幕方向、画面填充和显示 FPS。
- `选项 > 音频`：主音量、音频缓冲、音频缓冲延迟、音频效果、数字降噪和禁用音频。
- `选项 > 输入`：禁用系统输入法、显示虚拟按键、虚拟按键大小、方向键类型、键盘与手柄映射和手柄摇杆校准。
- `设置`：CPU 执行模式、CPU 时钟、游戏速度、系统延迟比例、金手指、金手指管理器、语言和恢复默认设置。
- `调试`：调试控制台、性能日志、打开调试日志、资源监视器、内存搜索器和调试器。
- `帮助`：作者主页、项目主页和关于丁果派。

设置会自动保存。视频、音频、输入、CPU 时钟、游戏速度、系统延迟比例、金手指、
语言和调试选项会立即生效。修改 CPU 执行模式会自动重启当前游戏。
暂停/恢复游戏只冻结当前游戏执行和音频输出。
即时存档提供 15 个档位，存档文件名使用游戏名，格式为
`游戏名.slot1.dps`，保存到 `saves\<游戏 SHA-256>\savestates` 文件夹。菜单会显示
已有档位的保存时间，精确到秒。`文件 > 存档管理器` 可查看档位和缩略图，
也可保存、读取、删除或打开存档目录。保存和读取前都会询问确认；读取时会
校验游戏和运行状态格式。如果当前还在标题、选择等不同阶段，请先进入与存档
相同的场景再读取。
当前 PC 版与安卓版使用相同的 APP/CC 即时存档格式。使用内容完全相同的游戏
文件时，可在两端对应的 `savestates` 目录间复制 `.dps` 文件；游戏文件名不同时
需要同步调整存档文件名。缩略图文件可选，不影响读取存档。

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

金手指按当前游戏格式加载，即 `游戏名.app` 对应 `cheats\游戏名.app.cht`，
`游戏名.cc` 对应 `cheats\游戏名.cc.cht`。未找到格式专用文件时，会兼容读取
`cheats\游戏名.cht`。两种文件均不存在时游戏正常运行；如果金手指文件不匹配当前游戏，
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

公共模拟核心位于 `native/core/`，PC 前端、平台适配和调试功能位于 `native/pc/`。

- `main.cpp`：进程启动和整体启动流程。
- `startup_command_line.*` 与 `startup_game_selection.*`：Windows 命令行解析、启动动作和游戏路径选择。
- `app_runtime.*`：游戏加载、运行时初始化、AppMain 入口调用和致命错误诊断。
- `mips_runtime.*`：后端选择、内置 MIPS32 CPU、内存映射、寄存器和运行时回调。
- `ppsspp_backend.*` 与 `ppsspp_bridge.cpp`：PPSSPP IR/x64 JIT 适配层和 Dingoo 内存桥接。
- `cc_runtime.*`、`arm32_dynarmic.*` 与 `arm32_interpreter.*`：CC 游戏运行时、Dynarmic ARM32 JIT 和兼容解释器。
- `mips_compat.*`：精确处理 break/cache 等兼容指令，不直接修改打包后的游戏数据。
- `compat_profile.*`：基于内容 hash 的兼容性配置，用于样本相关的时序、资源和退出行为。
- `app_hle.*`：丁果 SDK 导入桥接，供 A320、X760+ 与 A330 app 软件使用。
- `guest_package.*`：解析丁果科技 CCDL/IMPT/EXPT/RAWD APP 包格式和资源表。
- `app_memory.*`：模拟堆、栈、寄存器、地址别名和指针映射。
- `guest_filesystem.*`：虚拟文件和资源文件访问。
- `app_task_scheduler.*`：使用宿主线程模拟丁果 SDK 任务创建。
- `sdl_frontend.*`：SDL2 窗口、输入轮询和帧缓冲显示。
- `frontend_menu.*`：Windows 原生菜单创建和命令分发。
- `app_package_resource_index.*`、`resource_monitor_ui.*` 与 `runtime_resource_monitor.*`：APP 资源名索引、资源监视器窗口、运行时资源加载快照和高亮状态。
- `ui_strings.*`：英文/中文菜单与对话框文本。
- `sdl_audio.*` 与 `guest_audio.*`：SDL 音频输出和 waveout 桥接。
- `platform_win32.*`：文件选择器、路径编码和工作目录设置。
- `input_controls.*`：SDL 键盘输入到丁果 A320 / 歌美 X760+ / 歌美 A330 按键状态的映射。
- `app_text_format.*`：兼容 guest 侧 `sprintf`。
- `app_runtime_debug.*`：寄存器、内存和反汇编诊断。
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
MinGW winpthread runtime、PPSSPP、Dynarmic 和 Boost，然后把
`patches\ppsspp-irjit-dingoo.patch` 与 `patches\ppsspp-irjit-vfpu-bounds.patch`
应用到 PPSSPP 源码树。

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

DingooPie is a Windows emulator for Dingoo A320, Gemei X760+, and Gemei A330
`.app` and `.cc` games. The game file formats belong to their original vendors.
This project does not ship game samples; users must provide legally obtained files.

Windows version information:

- File description: Dingoo Game Emulator
- File version: 1.7
- Product name: DingooPie
- Powered by: BL2CK Software
- Copyright: Copyright (c) 2026 BL2CK
- Author Homepage: [bl2ck](https://github.com/bl2ck)
- Project Homepage: [bl2ck/DingooPie](https://github.com/bl2ck/DingooPie)

### Quick Start

Open games from `File > Open Game`, or drop an `.app` or `.cc` file onto the emulator
window. Recent games appear under `File > Recent Games` and can be cleared from
that submenu. When no game is running, the window shows the DingooPie idle
background; opening a game switches to gameplay automatically.

Command-line examples:

```powershell
.\DingooPie.exe "D:\Games\Dingoo\Your Game.app"
.\DingooPie.exe --game "D:\Games\Dingoo\Your Game.cc"
.\DingooPie.exe --game "D:\Games\Dingoo\Your Game.cc" --config "D:\DingooPie\Portable.ini" --no-recent
```

The syntax is `DingooPie.exe [options] [game.app|game.cc]`. Supported options are
`-g/--game <path>`, `-c/--config <path>`, `--no-recent`, `-h/--help`,
`-V/--version`, and `--`. Specify one game and quote paths containing spaces.
`--config` selects the settings file for this run. `--no-recent` skips recent-game
auto-start without clearing the list.

### Default Settings

| Item | Default |
| --- | --- |
| Window scale | 2x |
| Fullscreen | Off |
| Anti-aliasing | Off |
| Filter | Normal |
| Brightness / contrast / gamma / saturation | 100% / 100% / 100% / 100% |
| When minimized | Auto Pause |
| Screen orientation | Landscape |
| Screen fill | Keep Aspect Ratio |
| Show FPS | Off |
| Master volume | 100% |
| Audio buffer | 2048 samples |
| Audio buffer latency | Auto |
| Audio effect | Off |
| Digital noise reduction | High |
| Disable audio | Off |
| Disable System IME | On |
| Show Virtual Controls | Off |
| Virtual control size | 100% |
| D-pad type | Joystick |
| CPU Execution Mode | Auto; APP uses PPSSPP IR JIT and CC uses Dynarmic |
| CPU Clock | Auto, using the 336 MHz reference clock |
| Game Speed | Auto, using the global 65% speed scale |
| System Delay Scale | Auto, using the global 1.0 scale |
| Cheats | Off |
| UI language | Chinese |
| Debug Console | Off |
| Performance log | Off |
| Resource Monitor auto-open | Off |

### Menu And Configuration

The frontend menu is ordered as `File`, `Options`, `Settings`, `Debug`, and
`Help`; the Chinese UI displays them as `文件`, `选项`, `设置`, `调试`, and
`帮助`.

- `File`: Open Game, Recent Games/Clear Recent Games, Restart Game, Pause/Resume Game, Save Screenshot, Save State/Load State, Save Manager, and Exit Emulator.
- `Options > Video`: 1x, 2x, 3x, fullscreen, anti-aliasing, normal, black and white, invert, soft blur, sharpen, vivid, sepia, pixel grid, LCD scanline, light CRT, brightness, contrast, gamma, saturation, minimized behavior, screen orientation, screen fill, and Show FPS.
- `Options > Audio`: master volume, audio buffer, audio buffer latency, audio effect, digital noise reduction, and disable audio.
- `Options > Input`: Disable System IME, Show Virtual Controls, Virtual Control Size, D-pad Type, Input Mapping, and Joystick Calibration.
- `Settings`: CPU Execution Mode, CPU Clock, Game Speed, System Delay Scale, Cheats, Cheat Manager, Language, and Restore Default Settings.
- `Debug`: Debug Console, Performance Log, Open Debug Log, Resource Monitor, Memory Searcher, and Debugger.
- `Help`: Author Homepage, Project Homepage, and About DingooPie.

Settings are saved automatically. Video, audio, input, CPU clock, game speed,
system delay scale, cheats, language, and debug options apply immediately. Changing the
CPU Execution Mode automatically restarts the current game. Pause/Resume Game freezes
current game execution and audio output.
Save/Load State provides 15 slots per game. Save files use names like
`GameName.slot1.dps` and are stored under `saves\<game SHA-256>\savestates`.
The menu shows saved slot times down to seconds. `File > Save Manager`
can view slots and thumbnails, save, load, delete, or open the save-state
folder. Saving and loading ask for confirmation first. Loading validates the
game and runtime-state layout. If the game is still at a title or selection
stage, enter the same scene as the saved state before loading.
The current PC and Android builds use the same APP/CC instant-save format.
With byte-identical game files, `.dps` files can be copied between the matching
`savestates` directories. Rename the state file when the game filenames differ.
Thumbnail files are optional and do not affect loading.

`Options > Input > Input Mapping` opens a standalone keyboard/controller mapping
window. Choose `Set Key` or `Set Controller` on a control row, then press the target
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

Cheats are loaded from the format-specific `.cht` file for the running game:
`GameName.app` maps to `cheats\GameName.app.cht`, and `GameName.cc` maps to
`cheats\GameName.cc.cht`. If the format-specific file is missing, the legacy
`cheats\GameName.cht` filename is also accepted. If neither file exists, the
game runs normally. If a cheat file is not for the current game, DingooPie
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
- `Debug > Debugger`: APP exposes disassembly, registers, memory, PC hit counters, and write hits; CC exposes ARM32 registers and memory inspection. Hit counters do not pause or single-step the CPU.

### Source Layout

Shared emulation code lives in `native/core/`; PC frontend, platform, and debug code lives in `native/pc/`.

- `main.cpp`: process startup and high-level boot flow.
- `startup_command_line.*` and `startup_game_selection.*`: Windows command-line parsing, startup actions, and game-path selection.
- `app_runtime.*`: app loading, runtime initialization, AppMain handoff, and fatal diagnostics.
- `mips_runtime.*`: backend selection, in-tree MIPS32 CPU, memory map, register, and runtime callback support.
- `ppsspp_backend.*` and `ppsspp_bridge.cpp`: PPSSPP IR/x64 JIT adapter and Dingoo memory shim.
- `cc_runtime.*`, `arm32_dynarmic.*`, and `arm32_interpreter.*`: CC runtime, Dynarmic ARM32 JIT, and compatibility interpreter.
- `mips_compat.*`: precise break/cache compatibility handling without mutating packed app data.
- `compat_profile.*`: content-hash compatibility profiles for sample-specific timing, resource, and exit behavior.
- `app_hle.*`: Dingoo SDK import bridge used by A320, X760+, and A330 app software.
- `guest_package.*`: Dingoo Technology CCDL/IMPT/EXPT/RAWD APP package parsing and resource tables.
- `app_memory.*`: guest heap, stack, register, alias, and pointer mapping.
- `guest_filesystem.*`: virtual file and resource-backed file access.
- `app_task_scheduler.*`: Dingoo SDK task creation backed by host pthreads.
- `sdl_frontend.*`: SDL2 window, input polling, and framebuffer presentation.
- `frontend_menu.*`: native Windows menu construction and command dispatch.
- `app_package_resource_index.*`, `resource_monitor_ui.*`, and `runtime_resource_monitor.*`: APP resource-name index, Resource Monitor window, runtime resource-load snapshots, and highlight state.
- `ui_strings.*`: English/Chinese frontend menu and dialog text.
- `sdl_audio.*` and `guest_audio.*`: SDL audio output and waveout bridge.
- `platform_win32.*`: file picker, path encoding, and working-directory setup.
- `input_controls.*`: SDL key mapping to Dingoo A320, Gemei X760+, and Gemei A330 button state.
- `app_text_format.*`: guest `sprintf` compatibility.
- `app_runtime_debug.*`: register, memory, and disassembly diagnostics.
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
Capstone, the MinGW winpthread runtime, PPSSPP, Dynarmic, and Boost. It then
applies `patches\ppsspp-irjit-dingoo.patch` and
`patches\ppsspp-irjit-vfpu-bounds.patch` to the PPSSPP source tree.

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
