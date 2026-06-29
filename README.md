DingooPie
=========

中文
----

丁果派 DingooPie 是 Windows 平台的 `.app` 游戏模拟器，用于运行丁果 A320
及兼容歌美 X760+ 掌机游戏。`.app` 格式文件归丁果科技所有；本项目和发布包
不包含游戏样本，请使用自行合法取得的文件。

版本：1.2
Powered by BL2CK Software
版权：Copyright (c) BL2CK 2026

## 快速使用

1. 双击 `DingooPie.exe` 启动。
2. 通过 `文件 > 打开游戏` 选择 `.app`，或把 `.app` 拖到窗口中。
3. 最近游戏会记录到 `文件 > 最近游戏`，可在该子菜单清除。

也可以在命令行传入 `.app` 路径：

```bat
DingooPie.exe "D:\Games\Dingoo\Your Game.app"
```

## 默认设置

| 项目 | 默认值 |
| --- | --- |
| 界面语言 | 中文 |
| 窗口缩放 | 2x |
| 全屏 | 关闭 |
| 抗锯齿 | 关闭 |
| 滤镜 | 正常 |
| 亮度 / 对比度 / 饱和度 | 100% / 100% / 100% |
| 竖屏模式 | 关闭 |
| FPS 显示 | 关闭 |
| 虚拟按键 | 关闭 |
| 禁用输入法 | 开启 |
| 主音量 | 100% |
| 音频缓冲 | 2048 samples |
| 禁用音频 | 关闭 |
| CPU 后端 | 自动，默认使用 PPSSPP IR JIT |
| CPU 时钟 | 自动，默认 336 MHz |
| 运行速度 | 自动，默认 65% |
| 延迟比例 | 自动，默认 1.0 |
| 金手指 | 关闭 |
| 调试控制台 | 关闭 |
| 性能日志 | 关闭 |

## 菜单与配置

- `文件`：打开游戏、最近游戏/清除最近游戏、重启游戏、暂停/恢复游戏、保存截图、即时存档/读取存档、退出。
- `选项 > 视频`：缩放、全屏、抗锯齿、滤镜、亮度、对比度、饱和度、最小化时、竖屏模式、FPS 显示。
- `选项 > 音频`：主音量、音频缓冲、禁用音频。
- `选项 > 输入`：禁用输入法、显示虚拟按键、按键映射。
- `设置`：CPU 后端、CPU 时钟、运行速度、延迟比例、启用金手指、金手指功能、语言、恢复默认设置。
- `调试`：调试控制台、性能日志、打开调试日志、内存搜索器、调试器。
- `帮助`：关于 DingooPie。

设置会保存到 `DingooPie.ini`。视频、音频、输入、CPU 时钟、运行速度、
延迟比例、语言、金手指总开关和调试选项会立即生效。修改 CPU 后端会保存设置
并重启模拟器，因为执行后端在启动时选择。暂停/恢复游戏只影响当前运行状态，
不写入 INI。即时存档提供 10 个档位，存档文件名使用游戏名，格式为
`游戏名.slot1.dps`，保存到游戏所在目录旁的 `savestates` 文件夹。菜单会显示
已有档位的保存时间，精确到秒。存档会自动压缩，从菜单手动保存和读取时会显示
压缩/解压进度，进度窗体仅用于显示状态；保存和读取前都会询问确认。读取时会
校验游戏和运行状态格式；如果当前还在标题、选择等不同阶段，请先进入与存档相同的场景再读取。

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
2. 勾选 `设置 > 启用金手指`。
3. 在 `设置 > 金手指功能` 中勾选需要的功能。

`app_sha256` 只用于校验，不用于查找文件。没有同名 `.cht` 文件时游戏正常运行；
如果 SHA256 不匹配，模拟器会提示并停用该金手指文件。`.cht` 语法和维护规则见
`docs\DEBUGGING.md`。
具体功能默认不勾选；勾选状态按游戏保存，下次启动同一游戏会自动恢复。勾选后会
立即尝试应用。

`调试 > 内存搜索器` 可搜索 u8/u16/u32 数值，用等于、变大、变小、不变缩小候选。
选中候选后可加入下方锁定地址列表并刷新查看当前值，也可写入一次验证或复制为 `.cht` 金手指行。
u8 为 0~255，u16 为 0~65535，u32 为 0~4294967295。内存搜索器需要游戏运行中才能打开。

`调试 > 调试器` 显示反汇编、寄存器、内存、断点命中次数和写入监视。断点和写入
监视会记录命中 PC 与写入值，但不会暂停或单步 CPU。调试器需要游戏运行中才能打开。

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

Version: 1.2
Powered by BL2CK Software
Copyright (c) BL2CK 2026

## Quick Start

1. Run `DingooPie.exe`.
2. Open an `.app` from `File > Open Game`, or drop an `.app` onto the window.
3. Recent games are stored under `File > Recent Games` and can be cleared there.

A `.app` path can also be passed on the command line:

```bat
DingooPie.exe "D:\Games\Dingoo\Your Game.app"
```

## Default Settings

| Item | Default |
| --- | --- |
| UI language | Chinese |
| Window scale | 2x |
| Fullscreen | Off |
| Anti-aliasing | Off |
| Effect | Normal |
| Brightness / contrast / saturation | 100% / 100% / 100% |
| Portrait mode | Off |
| FPS overlay | Off |
| Virtual controls | Off |
| Disable IME | On |
| Master volume | 100% |
| Audio buffer | 2048 samples |
| Disable audio | Off |
| CPU backend | Auto, defaults to PPSSPP IR JIT |
| CPU clock | Auto, defaults to 336 MHz |
| Runtime speed | Auto, defaults to 65% |
| Delay scale | Auto, defaults to 1.0 |
| Cheats | Off |
| Debug console | Off |
| Performance log | Off |

## Menus And Configuration

- `File`: Open Game, Recent Games/Clear Recent Games, Restart Game, Pause/Resume Game, Save Screenshot, Save/Load State, and Exit.
- `Options > Video`: scale, fullscreen, anti-aliasing, effects, brightness, contrast, saturation, minimized behavior, portrait mode, and FPS overlay.
- `Options > Audio`: master volume, audio buffer, and disable audio.
- `Options > Input`: disable IME, show virtual controls, and input mapping.
- `Settings`: CPU Backend, CPU Clock, Runtime Speed, Delay Scale, Enable Cheats, Cheat Features, Language, and Restore Default Settings.
- `Debug`: Debug Console, Performance Log, Open Debug Log, Cheat Finder, and Debugger.
- `Help`: About DingooPie.

Settings are saved to `DingooPie.ini`. Video, audio, input, CPU clock, runtime
speed, delay scale, language, the global cheat switch, and debug options apply
immediately. Changing the CPU backend saves settings and restarts the emulator
because the execution backend is selected at startup. Pause/Resume only affects
the current runtime state and is not written to INI.
Save states provide 10 slots per game, use file names like `GameName.slot1.dps`,
and are stored in a `savestates` folder next to the game file. The menu shows
saved slot times down to seconds. Save files are compressed automatically, and
manual menu save/load shows compression or decompression progress. The progress
window is status-only. Saving and loading ask for confirmation first. Loading
validates the game and runtime-state layout. If the game is still at a title or
selection stage, enter the same scene as the saved state before loading.

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
2. Check `Settings > Enable Cheats`.
3. Select features from `Settings > Cheat Features`.

`app_sha256` is validation only, not the lookup key. If no same-name `.cht` file
exists, the game runs normally. If the SHA256 does not match, DingooPie warns
and disables that cheat file. See `docs\DEBUGGING.md` for `.cht` syntax and
maintenance rules.
Individual features start unchecked. Selections are saved per game, restored
when the same game starts again, and applied immediately when selected.
`Debug > Cheat Finder` searches u8/u16/u32 values, narrows candidates by value
change, can add addresses to the lower locked list and refresh current values,
writes one once, and copies it as a `.cht` line. u8 is 0-255, u16 is 0-65535,
and u32 is 0-4294967295. Cheat Finder is available while a game is running.
`Debug > Debugger` shows disassembly, registers, memory bytes, breakpoint hit
counters, and write watches. Breakpoints and write watches record the hit PC
and written value, but do not pause or single-step the CPU. Debugger is
available while a game is running.

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
- `scripts\`: bootstrap, build, runtime-test, and packaging automation.
- `tools\dingoo_app_tool\`: `.app` unpack/repack and resource inspection tool.
