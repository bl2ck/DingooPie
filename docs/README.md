# DingooPie

## 中文

DingooPie 是 Windows 平台的高层模拟器，用于运行丁果科技 `.app`
游戏文件，目标设备包括丁果 A320 掌机，并补充支持兼容的歌美 X760+
掌机游戏。`.app` 游戏文件格式归丁果科技所属。本项目不附带游戏样本，
用户需自行提供合法取得的文件。

当前运行时默认使用 PPSSPP IR/x64 JIT 后端执行游戏，保留项目内置
MIPS32 解释器用于正确性检查和诊断，并使用 SDL2 负责视频与音频输出。

Windows 版本信息：

- 文件说明：Dingoo A320 / Gemei X760+ Game Emulator
- 文件版本：1.0
- 产品名称：DingooPie
- Powered by：BL2CK
- 版权：Copyright (c) BL2CK 2026

### 快速使用

可通过 `File > Open Game .app` 打开游戏；所选路径会保存到
`recent.last_app`。最近一次正常退出的游戏会在下次启动时自动载入。

手动编辑 INI 时可使用带 BOM 的 UTF-16LE，或带/不带 BOM 的 UTF-8；
支持中文 app 路径。也可以在命令行传入 `.app` 路径，且优先于已保存
的最近游戏：

```powershell
.\DingooPie.exe "D:\Games\Dingoo\Your Game.app"
```

### 默认设置

| 项目 | 默认值 |
| --- | --- |
| 界面语言 | 中文 |
| 窗口缩放 | 2x |
| 全屏 | 关闭 |
| 采样方式 | 最近邻 |
| 滤镜 | 正常 |
| 亮度 / 对比度 / 饱和度 | 100% / 100% / 100% |
| 竖屏模式 | 关闭 |
| FPS 显示 | 关闭 |
| 虚拟按键 | 关闭 |
| 禁用输入法 | 开启 |
| 主音量 | 100% |
| 音频缓冲 | 2048 samples |
| 禁用音频 | 关闭 |
| CPU 后端 | Auto，默认映射到 PPSSPP IR JIT |
| CPU Clock | Auto，默认映射到 336 MHz |
| Runtime Speed | Auto，默认按 60% 运行速度 |
| SDK Delay Scale | Auto，默认按 1.0 延迟比例 |
| 调试控制台 | 关闭 |
| 性能日志 | 关闭 |

### 菜单与配置

当前前端菜单顺序为 `File`、`Options`、`Settings`、`Debug` 和 `Help`；
中文界面中分别显示为 `文件`、`选项`、`设置`、`调试` 和 `帮助`。

- `File / 文件`：打开游戏、重启游戏、保存截图、退出。
- `Options > Video / 选项 > 视频`：1x、2x、3x、窗口化全屏、最近邻/线性采样、正常、黑白、反色、反色黑白、怀旧褐色、琥珀屏、锐化、柔化、LCD 扫描线、亮度、对比度、饱和度、竖屏模式和 FPS 显示。
- `Options > Audio / 选项 > 音频`：主音量、音频缓冲和禁用音频。
- `Options > Input / 选项 > 输入`：显示虚拟按键和禁用输入法。
- `Settings / 设置`：CPU 后端、CPU Clock、Runtime Speed、SDK Delay Scale、语言、保存设置和恢复默认设置。
- `Debug / 调试`：调试控制台、性能日志和打开调试日志。
- `Help / 帮助`：关于 DingooPie。

修改窗口缩放或全屏会保存设置并重启模拟器；如果已加载游戏，会带同一个
app 路径重启；如果未加载游戏，会重启回到前端主界面。采样方式、滤镜、
亮度、对比度、饱和度、竖屏模式、FPS 显示、音频、输入、CPU 时钟、运行
速度、SDK 延迟比例、调试控制台和性能日志会保存并立即生效。修改 CPU
后端会保存设置并重启模拟器，因为执行后端在启动时选择。

界面语言可以在 `Settings > Language` 中切换 English / 中文，选择后的语言
会随模拟器配置一起保存。截图可以保存为 PNG、JPG 或 BMP；按 `F12`
自动截图时，文件名会自动带时间戳。

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
或虚拟按键使用。虚拟方向键包含斜向角按钮，按下后会同时触发对应的两个方向。

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
- `ui_strings.*`：英文/中文菜单与对话框文本。
- `sdl_audio.*` 与 `guest_audio.*`：SDL 音频输出和 waveout 桥接。
- `platform_win32.*`：文件选择器、路径编码和工作目录设置。
- `input_controls.*`：SDL 键盘输入到丁果 A320 / 歌美 X760+ 按键状态的映射。
- `guest_format.*`：兼容 guest 侧 `sprintf`。
- `runtime_debug.*`：寄存器、内存和反汇编诊断。
- `patches/`：项目维护的第三方源码补丁。
- `scripts/`：依赖下载、构建、基础运行测试和打包脚本。
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

支持的环境变量、基础运行测试命令和当前样本基线见 `docs\DEBUGGING.md`。

### 打包

```powershell
.\scripts\package_project.ps1
```

打包脚本会收集源码、脚本、文档、补丁和资源，不包含本地生成的
`release/` 发布产物。脚本会生成 `manifest.files.txt` 与 `manifest.sha256`，
压缩后再解压到临时
校验目录，逐项验证 hash、必需文件，并在发现任何样本 app 文件、日志、
调试截图或生成的分析产物时失败。

包内容和策略见 `docs\PACKAGING.md`。

## English

DingooPie is a Windows HLE emulator for Dingoo Technology `.app` game files
used by the Dingoo A320 handheld and compatible Gemei X760+ handheld software.
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

### Quick Start

Open games from `File > Open Game .app`; the selected path is saved to
`recent.last_app`. The last game that exits normally is auto-loaded on the next
start.

Manual INI edits may use UTF-16LE with BOM or UTF-8 with or without BOM;
Chinese app paths are supported. A `.app` path can also be passed on the command
line and takes priority over the saved recent game:

```powershell
.\DingooPie.exe "D:\Games\Dingoo\Your Game.app"
```

### Default Settings

| Item | Default |
| --- | --- |
| UI language | Chinese |
| Window scale | 2x |
| Fullscreen | Off |
| Sampling | Nearest |
| Effect | Normal |
| Brightness / contrast / saturation | 100% / 100% / 100% |
| Portrait mode | Off |
| FPS overlay | Off |
| Virtual controls | Off |
| Disable IME | On |
| Master volume | 100% |
| Audio buffer | 2048 samples |
| Disable audio | Off |
| CPU backend | Auto, mapped to PPSSPP IR JIT by default |
| CPU Clock | Auto, mapped to 336 MHz by default |
| Runtime Speed | Auto, mapped to 60% speed by default |
| SDK Delay Scale | Auto, mapped to 1.0 delay scale by default |
| Debug console | Off |
| Profile log | Off |

### Menu And Configuration

The frontend menu is ordered as `File`, `Options`, `Settings`, `Debug`, and
`Help`; the Chinese UI displays them as `文件`, `选项`, `设置`, `调试`, and
`帮助`.

- `File`: Open Game, Restart Game, Save Screenshot, and Exit.
- `Options > Video`: 1x, 2x, 3x, windowed fullscreen, nearest/linear sampling, normal, black and white, invert, inverted black and white, sepia, amber, sharpen, soft blur, LCD scanline, brightness, contrast, saturation, portrait mode, and FPS overlay.
- `Options > Audio`: master volume, audio buffer, and disable audio.
- `Options > Input`: show virtual controls and disable IME.
- `Settings`: CPU Backend, CPU Clock, Runtime Speed, SDK Delay Scale, Language, Save Settings, and Restore Default Settings.
- `Debug`: Debug Console, Profile Log, and Open Debug Log.
- `Help`: About DingooPie.

Changing window scale or fullscreen saves the setting and relaunches the
emulator. If a game is loaded, the same app path is passed to the relaunched
process; otherwise the frontend returns to its startup screen. Sampling, video
effects, brightness, contrast, saturation, portrait mode, FPS overlay, audio,
input, CPU clock, runtime speed, SDK delay scale, debug console, and profile log
settings save and apply immediately. Changing the CPU backend saves the setting
and relaunches the emulator because the execution backend is selected at
startup.

The UI language can be switched between English and Chinese from
`Settings > Language`; the selected language is saved with the emulator
configuration. Screenshots can be saved as PNG, JPG, or BMP. Automatic
screenshots use `F12` and include a timestamped file name.

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
The internal control names follow the Dingoo A320 SDK. Gemei X760+ face-button
input is mapped onto the same A/B/X/Y control bits, while Dingoo-only START,
SELECT, and shoulder controls remain available through the host keyboard and
virtual controls. The virtual D-pad includes diagonal corner buttons that press
the matching two directions together.

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
- `ui_strings.*`: English/Chinese frontend menu and dialog text.
- `sdl_audio.*` and `guest_audio.*`: SDL audio output and waveout bridge.
- `platform_win32.*`: file picker, path encoding, and working-directory setup.
- `input_controls.*`: SDL key mapping to Dingoo A320 and Gemei X760+ button state.
- `guest_format.*`: guest `sprintf` compatibility.
- `runtime_debug.*`: register, memory, and disassembly diagnostics.
- `patches/`: project-owned patches applied to third-party source trees during bootstrap.
- `scripts/`: bootstrap, build, basic runtime-test, and packaging automation.
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

See `docs\DEBUGGING.md` for supported environment variables, basic runtime-test
commands, and current sample baselines.

### Packaging

```powershell
.\scripts\package_project.ps1
```

The packaging script stages source, scripts, docs, patches, and resources; it
does not include locally generated `release/` artifacts. It generates
`manifest.files.txt` and `manifest.sha256`, compresses the package, extracts it
to a temporary verification directory, validates every hash, checks required
files, and fails if any sample app file, log, debug screenshot, or generated
analysis artifact is present.

See `docs\PACKAGING.md` for package contents and policy.
