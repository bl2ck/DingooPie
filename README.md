DingooPie
=========

中文
----

丁果派 DingooPie 是 Windows 平台的 `.app` 游戏模拟器，用于运行丁果 A320
及兼容歌美 X760+ 掌机游戏。`.app` 格式文件归丁果科技所有；本项目和发布包
不包含游戏样本，请使用自行合法取得的文件。

版本：1.5
Powered by BL2CK Software
版权：Copyright (c) 2026 BL2CK

## 快速使用

1. 双击 `DingooPie.exe` 启动。
2. 通过 `文件 > 打开游戏` 选择 `.app`，或把 `.app` 拖到窗口中。
3. 最近游戏会记录到 `文件 > 最近游戏`，可在该子菜单清除。

未打开游戏时，窗口会显示 DingooPie 动态背景；打开游戏后会自动切换到游戏画面。

也可以在命令行传入 `.app` 路径：

```bat
DingooPie.exe "D:\Games\Dingoo\Your Game.app"
```

## 默认设置

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

## 菜单与配置

- `文件`：打开游戏、最近游戏/清除最近游戏、重启游戏、暂停/恢复游戏、保存截图、保存存档/读取存档、存档管理器、退出。
- `选项 > 视频`：缩放、全屏、抗锯齿、滤镜、亮度、对比度、伽马、饱和度、最小化时、竖屏模式、FPS 显示。
- `选项 > 音频`：主音量、音频缓冲、音频效果、禁用音频。
- `选项 > 输入`：禁用输入法、显示虚拟按键、按键映射。
- `设置`：CPU 后端、CPU 时钟、运行速度、延迟比例、金手指、语言、恢复默认设置。
- `调试`：调试控制台、性能日志、打开调试日志、资源监视器、内存搜索器、调试器。
- `帮助`：关于 DingooPie。

设置会自动保存。视频、音频、输入、CPU 时钟、运行速度、延迟比例、金手指、
语言和调试选项会立即生效。修改 CPU 后端会自动重启当前游戏。
暂停/恢复游戏只影响当前运行状态。即时存档提供 15 个档位，存档文件名使用游戏名，格式为
`游戏名.slot1.dps`，保存到游戏所在目录旁的 `savestates` 文件夹。菜单会显示
已有档位的保存时间，精确到秒。`文件 > 存档管理器` 可查看档位和缩略图，也可保存、读取、删除或打开存档目录。保存和读取前都会询问确认；读取时会校验游戏
和运行状态格式。如果当前还在标题、选择等不同阶段，请先进入与存档相同的场景再读取。

`调试 > 打开调试日志` 会打开当前实例的调试日志。运行时崩溃会额外生成诊断日志。

## 按键

| 键盘按键 | 丁果 A320 / 歌美 X760+ 控制 |
| --- | --- |
| 方向键 / WASD | 方向键 |
| L | A |
| K | B |
| I | X |
| J | Y |
| 1 / Q | SELECT |
| 0 / O | START |
| 左 Shift | 左肩键 |
| 右 Shift | 右肩键 |
| Esc | 询问是否退出模拟器 |
| F12 | 保存截图 |

`Enter` 当前没有映射到任何 Dingoo 按键，避免和游戏输入冲突。支持 SDL
GameController 兼容手柄；可在 `选项 > 输入 > 按键映射` 中分别设置键盘和
手柄映射。

## 金手指

金手指默认关闭。文件按游戏同名 `.cht` 加载：

```text
游戏名.app -> cheats\游戏名.cht
```

使用步骤：

1. 打开对应游戏。
2. 勾选 `设置 > 金手指 > 启用金手指`。
3. 在 `设置 > 金手指` 中勾选需要的功能；也可以打开
   `设置 > 金手指 > 金手指管理器` 使用列表视图管理。

没有同名 `.cht` 文件时游戏正常运行；如果金手指文件不匹配当前游戏，模拟器会提示并停用该文件。
具体功能默认不勾选；勾选状态按游戏保存，下次启动同一游戏会自动恢复。勾选后会
立即尝试应用。

## 调试工具

- `调试 > 调试控制台`：显示调试输出窗口。
- `调试 > 性能日志`：记录运行时性能统计。
- `调试 > 打开调试日志`：打开当前实例的调试日志文件。
- `调试 > 资源监视器`：查看游戏运行中加载的内部资源和外部文件；勾选后会立即打开，之后启动游戏时也会自动打开。
- `调试 > 内存搜索器`：搜索 u8/u16/u32 数值，用变化条件缩小候选；选中地址可刷新当前值、写入一次或复制为 `.cht` 金手指行。内存搜索器需要游戏运行中才能打开。
- `调试 > 调试器`：显示反汇编、寄存器、内存、断点命中次数和写入监视。断点和写入监视只记录命中，不会暂停或单步 CPU。调试器需要游戏运行中才能打开。

## 构建

从干净源码包构建：

```powershell
Set-ExecutionPolicy -Scope Process Bypass -Force
.\scripts\bootstrap_windows.ps1
.\scripts\build_release.ps1
```

`bootstrap_windows.ps1` 下载或复用 w64devkit、SDL2、Capstone、MinGW
winpthread runtime 和 PPSSPP，并应用项目补丁。`build_release.ps1` 会生成
可直接运行的 `release\` 目录。

## 源码目录

- `dingoo_pie\`：模拟器主体源码。
- `cheats\`：随项目维护的示例金手指和说明。
- `docs\`：架构、调试、样本基线和打包说明。
- `patches\`：项目维护的第三方源码补丁。
- `scripts\`：依赖下载、构建、运行测试和打包脚本。
- `tools\dingoo_app_tool\`：`.app` 解包、回包和资源检查工具。

English
-------

DingooPie is a Windows emulator for Dingoo A320 `.app` games and compatible
Gemei X760+ handheld games. The `.app` package format belongs to Dingoo
Technology. This project and its releases do not include game samples; use
legally obtained files.

Version: 1.5
Powered by BL2CK Software
Copyright (c) 2026 BL2CK

## Quick Start

1. Run `DingooPie.exe`.
2. Open an `.app` from `File > Open Game`, or drop an `.app` onto the window.
3. Recent games are stored under `File > Recent Games` and can be cleared there.

When no game is open, the window shows the DingooPie animated background; opening
a game switches to gameplay automatically.

A `.app` path can also be passed on the command line:

```bat
DingooPie.exe "D:\Games\Dingoo\Your Game.app"
```

## Default Settings

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
| CPU clock | Auto, defaults to 336 MHz |
| Runtime speed | Auto, defaults to 65% |
| Delay scale | Auto, defaults to 1.0 |
| Cheats | Off |
| UI language | Chinese |
| Debug Console | Off |
| Performance log | Off |
| Resource Monitor auto-open | Off |

## Menus And Configuration

- `File`: Open Game, Recent Games/Clear Recent Games, Restart Game, Pause/Resume Game, Save Screenshot, Save Slot/Load Slot, Save Manager, and Exit.
- `Options > Video`: scale, fullscreen, anti-aliasing, effects, brightness, contrast, gamma, saturation, minimized behavior, portrait mode, and FPS overlay.
- `Options > Audio`: master volume, audio buffer, audio effect, and disable audio.
- `Options > Input`: disable IME, show virtual controls, and input mapping.
- `Settings`: CPU Backend, CPU Clock, Runtime Speed, Delay Scale, Cheats, Language, and Restore Default Settings.
- `Debug`: Debug Console, Performance Log, Open Debug Log, Resource Monitor, Memory Searcher, and Debugger.
- `Help`: About DingooPie.

Settings are saved automatically. Video, audio, input, CPU clock, runtime speed,
delay scale, cheats, language, and debug options apply immediately. Changing the
CPU backend automatically restarts the current game. Pause/Resume only affects
the current runtime state. Save states provide 15 slots per game, use file names like `GameName.slot1.dps`,
and are stored in a `savestates` folder next to the game file. The menu shows
saved slot times down to seconds. `File > Save Manager` can view slots and
thumbnails, save, load, delete, or open the save-state folder. Saving and loading ask for confirmation first.
Loading validates the game and runtime-state layout. If the game is still at a
title or selection stage, enter the same scene as the saved state before loading.

`Debug > Open Debug Log` opens the current debug log. Runtime crashes also write
an additional diagnostic log.

## Keys

| Host key | Dingoo A320 / Gemei X760+ control |
| --- | --- |
| Arrow keys / WASD | D-pad |
| L | A |
| K | B |
| I | X |
| J | Y |
| 1 / Q | SELECT |
| 0 / O | START |
| Left Shift | Left shoulder |
| Right Shift | Right shoulder |
| Esc | Ask before exiting the emulator |
| F12 | Save screenshot |

`Enter` is intentionally unmapped so it cannot conflict with game input. SDL
GameController-compatible pads are supported. Use
`Options > Input > Input Mapping` to customize keyboard and controller mappings.

## Cheats

Cheats are disabled by default. Files are loaded by the running app's base name:

```text
GameName.app -> cheats\GameName.cht
```

Use cheats:

1. Open the matching game.
2. Check `Settings > Cheats > Enable Cheats`.
3. Select features from `Settings > Cheats`; use
   `Settings > Cheats > Cheat Manager` when you want a list view.

If no same-name `.cht` file exists, the game runs normally. If a cheat file does
not match the current game, DingooPie warns and disables that file.
Individual features start unchecked. Selections are saved per game, restored
when the same game starts again, and applied immediately when selected.

## Debug Tools

- `Debug > Debug Console`: shows the debug output window.
- `Debug > Performance Log`: records runtime performance counters.
- `Debug > Open Debug Log`: opens the current debug log file.
- `Debug > Resource Monitor`: shows internal resources and external files while a game is running; when checked, it opens immediately and automatically for later games.
- `Debug > Memory Searcher`: searches u8/u16/u32 values and narrows candidates by value changes; selected addresses can be refreshed, written once, or copied as `.cht` cheat lines. Memory Searcher is available while a game is running.
- `Debug > Debugger`: shows disassembly, registers, memory bytes, PC hit counters, and write hits. PC hits and write hits only record hits and do not pause or single-step the CPU. Debugger is available while a game is running.

## Build

From a clean package:

```powershell
Set-ExecutionPolicy -Scope Process Bypass -Force
.\scripts\bootstrap_windows.ps1
.\scripts\build_release.ps1
```

`bootstrap_windows.ps1` downloads or reuses w64devkit, SDL2, Capstone, the
MinGW winpthread runtime, and PPSSPP, then applies the project patch.
`build_release.ps1` creates a runnable `release\` directory.

## Source Layout

- `dingoo_pie\`: emulator source.
- `cheats\`: maintained sample cheats and documentation.
- `docs\`: architecture, debugging, sample baseline, and packaging notes.
- `patches\`: project-owned patches applied to third-party source trees.
- `scripts\`: bootstrap, build, test, and packaging scripts.
- `tools\dingoo_app_tool\`: `.app` unpack/repack and resource inspection tool.
