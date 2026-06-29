DingooPie
=========

中文
----

丁果派 DingooPie 是 Windows 平台的 `.app` 游戏模拟器，用于运行丁果 A320
及兼容歌美 X760+ 掌机游戏。本发布包不包含游戏文件，请使用自行合法取得的
`.app` 文件。

## 基本操作

1. 双击 `DingooPie.exe` 启动。
2. 通过 `文件 > 打开游戏` 选择 `.app`，或把 `.app` 拖到窗口中。
3. 最近打开的游戏会显示在 `文件 > 最近游戏`，可在该菜单中清除。

也可以从命令行直接传入游戏路径：

```bat
DingooPie.exe "D:\Games\Dingoo\Your Game.app"
```

## 常用按键

| 键盘 | 掌机按键 |
| --- | --- |
| 方向键 / WASD | 方向键 |
| L / K / I / J | A / B / X / Y |
| 1 / Q | SELECT |
| 0 / O | START |
| 左 Shift / 右 Shift | 左 / 右肩键 |
| Esc | 询问退出 |
| F12 | 保存截图 |

支持 SDL GameController 兼容手柄。可在 `选项 > 输入 > 按键映射` 中自定义
键盘和手柄映射。

## 菜单

- `文件`：打开游戏、最近游戏、重启、暂停/恢复、保存截图、即时存档/读取存档、退出。
- `选项`：视频、音频和输入设置。
- `设置`：CPU、速度、延迟、金手指功能、语言和恢复默认设置。
- `调试`：调试控制台、性能日志、内存搜索器、调试器和日志文件。
- `帮助`：版本信息。

设置会自动保存到 `DingooPie.ini`。大多数设置会立即生效；修改 CPU 后端时，
模拟器会自动重启当前游戏。即时存档有 5 个档位，存档文件名格式为
`游戏名.slot1.dps`，保存到游戏所在目录旁的 `savestates` 文件夹。菜单会显示
已有档位的保存时间；保存和读取前都会询问确认。

## 金手指

金手指默认关闭。发布包如果带有 `cheats` 文件夹，请保持 `.cht` 文件名与游戏
同名，即 `游戏名.app` 使用 `cheats\游戏名.cht`。

使用步骤：

1. 打开对应游戏。
2. 勾选 `设置 > 启用金手指`。
3. 在 `设置 > 金手指功能` 中勾选需要的功能。

没有同名 `.cht` 文件时，游戏会正常运行，不需要额外处理。如果金手指文件和
当前游戏不匹配，模拟器会提示警告并停用该金手指文件。
具体功能默认不勾选；勾选状态会按游戏保存，下次启动同一游戏会自动恢复。
勾选后会立即尝试应用。

`调试 > 内存搜索器` 可在游戏运行中搜索 u8/u16/u32 数值，用数值变化缩小候选，
并把选中地址加入下方锁定地址列表后刷新查看当前值、写入一次或复制为 `.cht` 行。
u8 为 0~255，u16 为 0~65535，u32 为 0~4294967295。

`调试 > 调试器` 可在游戏运行中显示反汇编、寄存器、内存、断点命中次数和写入
监视。断点和写入监视只记录命中 PC/写入值，不会暂停 CPU。

English
-------

DingooPie is a Windows emulator for Dingoo A320 `.app` games and compatible
Gemei X760+ handheld games. This release does not include game files. Use
legally obtained `.app` files.

## Basic Use

1. Run `DingooPie.exe`.
2. Open an `.app` from `File > Open Game`, or drop an `.app` onto the window.
3. Recent games appear under `File > Recent Games` and can be cleared there.

You can also pass a game path on the command line:

```bat
DingooPie.exe "D:\Games\Dingoo\Your Game.app"
```

## Common Keys

| Keyboard | Handheld control |
| --- | --- |
| Arrow keys / WASD | D-pad |
| L / K / I / J | A / B / X / Y |
| 1 / Q | SELECT |
| 0 / O | START |
| Left Shift / Right Shift | Left / right shoulder |
| Esc | Ask before exit |
| F12 | Save screenshot |

SDL GameController-compatible pads are supported. Use
`Options > Input > Input Mapping` to customize keyboard and controller mappings.

## Menus

- `File`: open games, recent games, restart, pause/resume, screenshot, save/load state, exit.
- `Options`: video, audio, and input settings.
- `Settings`: CPU, speed, delay, cheats, language, and restore defaults.
- `Debug`: debug console, performance log, open debug log, Cheat Finder, and Debugger.
- `Help`: version information.

Settings are saved automatically to `DingooPie.ini`. Most settings apply
immediately. Changing the CPU backend automatically restarts the current game.
Save states have 5 slots per game, use file names like `GameName.slot1.dps`, and
are stored in a `savestates` folder next to the game file. The menu shows saved
slot times; saving and loading ask for confirmation first.

## Cheats

Cheats are disabled by default. If the release includes a `cheats` folder, keep
each `.cht` file named after its game: `GameName.app` uses
`cheats\GameName.cht`.

Use cheats:

1. Open the matching game.
2. Check `Settings > Enable Cheats`.
3. Select features from `Settings > Cheat Features`.

If no same-name `.cht` file exists, the game runs normally. If a cheat file does
not match the current game, DingooPie shows a warning and disables that cheat
file.
Individual features start unchecked. Selections are saved per game and restored
when the same game starts again.
Selection immediately tries to apply the feature.
`Debug > Cheat Finder` is available while a game is running. It can search
u8/u16/u32 memory values, narrow candidates by change, add addresses to the
lower locked list and refresh current values, write one once, and copy it as a
`.cht` line. u8 is 0-255, u16 is 0-65535, and u32 is 0-4294967295.
`Debug > Debugger` is available while a game is running. It shows live
disassembly, registers, memory bytes, breakpoint hit counters, and write
watches. Breakpoints and write watches record hit PC/value information without
pausing the CPU.

Version: 1.2
Powered by BL2CK Software
