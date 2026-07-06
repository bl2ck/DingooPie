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

未打开游戏时，窗口会显示 DingooPie 动态背景；打开游戏后会自动切换到游戏画面。

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

- `文件`：打开游戏、最近游戏、重启、暂停/恢复、保存截图、保存存档/读取存档、存档管理器、退出。
- `选项`：视频、音频和输入设置。
- `设置`：CPU、速度、延迟、金手指、语言和恢复默认设置。
- `调试`：调试控制台、性能日志、打开调试日志、资源监视器、内存搜索器和调试器。
- `帮助`：版本信息。

设置会自动保存。大多数设置会立即生效；
修改 CPU 后端时，模拟器会自动重启当前游戏。即时存档有 15 个档位，存档文件名格式为
`游戏名.slot1.dps`，保存到游戏所在目录旁的 `savestates` 文件夹。菜单会显示
已有档位的保存时间；可在 `文件 > 存档管理器` 中查看档位和缩略图，也可保存、读取、删除或打开存档目录。保存和读取前都会询问确认。
`调试 > 打开调试日志` 会打开当前实例的调试日志。运行时崩溃会额外生成诊断日志。

## 金手指

金手指默认关闭。发布包如果带有 `cheats` 文件夹，请保持 `.cht` 文件名与游戏
同名，即 `游戏名.app` 使用 `cheats\游戏名.cht`。

使用步骤：

1. 打开对应游戏。
2. 勾选 `设置 > 金手指 > 启用金手指`。
3. 在 `设置 > 金手指` 中勾选需要的功能；也可以打开
   `设置 > 金手指 > 金手指管理器` 使用列表视图管理。

没有同名 `.cht` 文件时，游戏会正常运行，不需要额外处理。如果金手指文件和
当前游戏不匹配，模拟器会提示警告并停用该金手指文件。
具体功能默认不勾选；勾选状态会按游戏保存，下次启动同一游戏会自动恢复，并在勾选后立即尝试应用。

## 调试工具

- `调试 > 调试控制台`：显示调试输出窗口。
- `调试 > 性能日志`：记录运行时性能统计。
- `调试 > 打开调试日志`：打开当前实例的调试日志文件。
- `调试 > 资源监视器`：查看游戏运行中加载的内部资源和外部文件；勾选后会立即打开，之后启动游戏时也会自动打开。
- `调试 > 内存搜索器`：搜索 u8/u16/u32 数值，用变化条件缩小候选；选中地址可刷新当前值、写入一次或复制为 `.cht` 金手指行。
- `调试 > 调试器`：显示反汇编、寄存器、内存、断点命中次数和写入监视。断点和写入监视只记录命中，不会暂停 CPU。

English
-------

DingooPie is a Windows emulator for Dingoo A320 `.app` games and compatible
Gemei X760+ handheld games. This release does not include game files. Use
legally obtained `.app` files.

## Basic Use

1. Run `DingooPie.exe`.
2. Open an `.app` from `File > Open Game`, or drop an `.app` onto the window.
3. Recent games appear under `File > Recent Games` and can be cleared there.

When no game is open, the window shows the DingooPie animated background; opening
a game switches to gameplay automatically.

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

- `File`: open games, recent games, restart, pause/resume, screenshot, Save Slot/Load Slot, Save Manager, exit.
- `Options`: video, audio, and input settings.
- `Settings`: CPU, speed, delay, Cheats, language, and restore defaults.
- `Debug`: Debug Console, Performance Log, Open Debug Log, Resource Monitor, Memory Searcher, and Debugger.
- `Help`: version information.

Settings are saved automatically. Most
settings apply immediately. Changing the CPU backend automatically restarts the current game.
Save states have 15 slots per game, use file names like `GameName.slot1.dps`, and
are stored in a `savestates` folder next to the game file. The menu shows saved
slot times; `File > Save Manager` can view slots and thumbnails, save, load, delete, or open the save-state folder.
Saving and loading ask for confirmation first.
`Debug > Open Debug Log` opens the current debug log. Runtime crashes also write
an additional diagnostic log.

## Cheats

Cheats are disabled by default. If the release includes a `cheats` folder, keep
each `.cht` file named after its game: `GameName.app` uses
`cheats\GameName.cht`.

Use cheats:

1. Open the matching game.
2. Check `Settings > Cheats > Enable Cheats`.
3. Select features from `Settings > Cheats`; use
   `Settings > Cheats > Cheat Manager` when you want a list view.

If no same-name `.cht` file exists, the game runs normally. If a cheat file does
not match the current game, DingooPie shows a warning and disables that cheat
file.
Individual features start unchecked. Selections are saved per game, restored
when the same game starts again, and applied immediately when selected.

## Debug Tools

- `Debug > Debug Console`: shows the debug output window.
- `Debug > Performance Log`: records runtime performance counters.
- `Debug > Open Debug Log`: opens the current debug log file.
- `Debug > Resource Monitor`: shows internal resources and external files while a game is running; when checked, it opens immediately and automatically for later games.
- `Debug > Memory Searcher`: searches u8/u16/u32 memory values and narrows candidates by value changes; selected addresses can be refreshed, written once, or copied as `.cht` cheat lines.
- `Debug > Debugger`: shows live disassembly, registers, memory bytes, PC hit counters, and write hits. PC hits and write hits only record hits and do not pause the CPU.

Version: 1.5
Powered by BL2CK Software
