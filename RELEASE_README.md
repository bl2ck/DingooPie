# 丁果派 DingooPie

## 中文

丁果派 DingooPie 是 Windows 平台的丁果游戏模拟器，支持运行丁果 A320、歌美 X760+、歌美 A330 的 `.app` 与 `.cc` 游戏。
本发行版不包含游戏文件，请使用自行合法取得的游戏样本。

- Powered by：BL2CK Software
- 版权：Copyright (c) 2026 BL2CK

### 快速使用

1. 解压完整发行包后，双击 `DingooPie.exe`。
2. 通过 `文件 > 打开游戏` 选择 `.app` 或 `.cc` 文件，也可以将游戏文件拖入模拟器窗口。
3. 已运行的游戏会显示在 `文件 > 最近游戏` 中；不传入游戏启动时，模拟器会自动继续最近一次运行的游戏。
4. 未运行游戏时会显示随机动态背景；打开游戏后自动切换到游戏画面。

也可以从命令行直接启动单个游戏：

```bat
DingooPie.exe "D:\Games\Dingoo\Your Game.app"
DingooPie.exe "D:\Games\Dingoo\Your Game.cc"
```

默认建议使用 `CPU 执行模式 > 自动`。如果个别游戏运行异常，可切换为 `兼容模式` 后重试。

### 默认设置

| 选项 | 默认值 |
| --- | --- |
| 窗口缩放 | 2x |
| 全屏 | 关闭 |
| 抗锯齿 | 关闭 |
| 滤镜 | 正常 |
| 亮度 / 对比度 / 伽马 / 饱和度 | 100% / 100% / 100% / 100% |
| 最小化时 | 自动暂停 |
| 竖屏模式 | 关闭 |
| 显示 FPS | 关闭 |
| 主音量 | 100% |
| 音频缓冲 | 2048 采样 |
| 音频缓冲延迟 | 自动 |
| 音频效果 | 关闭 |
| 数字降噪 | 高 |
| 禁用音频 | 关闭 |
| 禁用系统输入法 | 开启 |
| 显示虚拟按键 | 关闭 |
| CPU 执行模式 | 自动 |
| CPU 时钟 | 自动 |
| 游戏速度 | 自动 |
| 系统延迟比例 | 自动 |
| 金手指 | 关闭 |
| 界面语言 | 中文 |

### 菜单与设置

- `文件`：打开游戏、最近游戏、重启游戏、暂停/恢复游戏、保存截图、保存存档、读取存档、存档管理器、退出模拟器。
- `选项 > 视频`：窗口缩放、全屏、抗锯齿、滤镜、画面参数、最小化行为、竖屏模式和 FPS 显示。
- `选项 > 音频`：主音量、音频缓冲、音频缓冲延迟、音频效果、数字降噪和禁用音频。
- `选项 > 输入`：禁用系统输入法、显示虚拟按键、按键手柄映射和手柄摇杆校准。
- `设置`：CPU 执行模式、CPU 时钟、游戏速度、系统延迟比例、金手指、语言和恢复默认设置。
- `调试器`：显示调试控制台、启用性能日志、打开调试日志、资源监视器、内存搜索器和游戏调试器。
- `帮助`：查看版本、支持格式和软件信息。

设置会自动保存到 `DingooPie.exe` 同目录的 `DingooPie.ini`。大多数选项会立即生效；切换 CPU 执行模式时，当前游戏会自动重启。

### 默认按键

| 键盘按键 | 丁果按键 |
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

支持 SDL GameController 兼容手柄。可在 `选项 > 输入 > 按键手柄映射` 中分别调整键盘和手柄映射，并开始或恢复默认的手柄摇杆校准。校准时先松开所有摇杆并保持静止，再按提示将左右摇杆沿最大范围缓慢转动，完成后会自动保存。
`Enter` 默认不映射到游戏按键，以避免与游戏输入冲突。

### 即时存档

每个游戏提供 15 个即时存档档位：

- 存档文件：`savestates\游戏名.slotN.dps`
- 缩略图：`savestates\游戏名.slotN.thumb.bmp`
- `文件 > 存档管理器` 可查看缩略图、保存、读取、删除存档或打开存档目录。
- 保存和读取前会要求确认；如果当前游戏阶段与存档阶段不同，请先进入相同场景后再读取。

### 金手指

金手指默认关闭，并优先读取与游戏格式同名的文件：

```text
游戏名.app -> cheats\游戏名.app.cht
游戏名.cc  -> cheats\游戏名.cc.cht
```

打开游戏后，在 `设置 > 金手指` 中启用金手指并选择需要的功能，也可以使用金手指管理器统一启用、停用、应用或刷新。
没有匹配文件或文件不适用于当前游戏时，游戏仍可正常运行，相关金手指不会应用。

### 调试功能

PC 版保留以下用户可操作的调试功能：

- 调试控制台和调试日志：查看模拟器运行信息及错误提示。
- 性能日志：记录当前游戏的运行统计。
- 资源监视器：查看游戏已加载的内部资源和外部文件。
- 内存搜索器：搜索并修改运行中的数值，也可复制为 `.cht` 金手指代码。
- 调试器：APP 游戏可查看反汇编、寄存器、内存和命中记录；CC 游戏可查看 ARM32 寄存器与内存。

资源监视器、内存搜索器和调试器需要在游戏运行时使用。

### 常见问题

- 游戏无法正常启动：尝试将 `CPU 执行模式` 切换为 `兼容模式`。
- 没有声音：确认未启用 `禁用音频`，并检查主音量和系统音量。
- 键盘输入异常：尝试启用 `禁用系统输入法`，或重新设置按键手柄映射。
- 即时存档无法读取：确认存档属于当前游戏，并进入保存时的相同游戏阶段。
- 金手指不可用：确认 `.cht` 文件名、游戏格式和游戏版本均匹配。

---

## English

DingooPie is a Windows emulator for Dingoo A320, Gemei X760+, and Gemei A330 `.app` and `.cc` games.
Game files are not included. Use legally obtained game samples.

- Powered by: BL2CK Software
- Copyright (c) 2026 BL2CK

### Quick Start

1. Extract the complete release package and run `DingooPie.exe`.
2. Select an `.app` or `.cc` file from `File > Open Game`, or drag the game file onto the emulator window.
3. Previously launched games appear under `File > Recent Games`. Starting without a game automatically resumes the most recent game.
4. A randomized animated background is shown while no game is running; opening a game switches to gameplay automatically.

A single game can also be launched from the command line:

```bat
DingooPie.exe "D:\Games\Dingoo\Your Game.app"
DingooPie.exe "D:\Games\Dingoo\Your Game.cc"
```

`CPU Execution Mode > Auto` is recommended. If a game does not run correctly, retry with `Compatibility Mode`.

### Default Settings

| Option | Default |
| --- | --- |
| Window scale | 2x |
| Fullscreen | Off |
| Anti-aliasing | Off |
| Filter | Normal |
| Brightness / contrast / gamma / saturation | 100% / 100% / 100% / 100% |
| When minimized | Auto Pause |
| Portrait mode | Off |
| Show FPS | Off |
| Master volume | 100% |
| Audio buffer | 2048 samples |
| Audio buffer latency | Auto |
| Audio effect | Off |
| Digital noise reduction | High |
| Disable audio | Off |
| Disable system IME | On |
| Show virtual controls | Off |
| CPU execution mode | Auto |
| CPU clock | Auto |
| Game speed | Auto |
| System delay scale | Auto |
| Cheats | Off |
| UI language | Chinese |

### Menus And Settings

- `File`: Open Game, Recent Games, Restart Game, Pause/Resume Game, Save Screenshot, Save Slot, Load Slot, Save Manager, and Exit Emulator.
- `Options > Video`: window scale, fullscreen, anti-aliasing, filters, image adjustments, minimized behavior, portrait mode, and FPS display.
- `Options > Audio`: master volume, audio buffer, audio buffer latency, audio effect, digital noise reduction, and audio disable.
- `Options > Input`: Disable System IME, Show Virtual Controls, Input Mapping, and Joystick Calibration.
- `Settings`: CPU Execution Mode, CPU Clock, Game Speed, System Delay Scale, Cheats, Language, and Restore Default Settings.
- `Debugger`: Show Debug Console, Enable Performance Log, Open Debug Log, Resource Monitor, Memory Searcher, and Debugger.
- `Help`: version, supported formats, and software information.

Settings are saved automatically in `DingooPie.ini` beside `DingooPie.exe`. Most options apply immediately. Changing CPU Execution Mode automatically restarts the current game.

### Default Keys

| Keyboard | Dingoo control |
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

SDL GameController-compatible devices are supported. Keyboard/controller mappings and joystick calibration are available under `Options > Input > Input Mapping`. During calibration, release all sticks and keep them centered, then slowly move both sticks through their full range; the result is saved automatically.
`Enter` is intentionally unmapped to avoid conflicts with game input.

### Instant Saves

Each game provides 15 instant save slots:

- State files: `savestates\GameName.slotN.dps`
- Preview images: `savestates\GameName.slotN.thumb.bmp`
- `File > Save Manager` can display previews, save, load, delete states, or open the save directory.
- Saving and loading require confirmation. If the current game phase differs from the saved phase, enter the same scene before loading.

### Cheats

Cheats are disabled by default and prefer a format-specific matching file:

```text
GameName.app -> cheats\GameName.app.cht
GameName.cc  -> cheats\GameName.cc.cht
```

After opening a game, enable cheats and select features under `Settings > Cheats`, or use Cheat Manager to enable, disable, apply, or refresh entries.
If no matching file exists or the file does not match the current game, gameplay continues normally and those cheats are not applied.

### Debugging Features

The PC version retains the following user-accessible debugging features:

- Debug Console and Debug Log: view emulator status and error messages.
- Performance Log: record runtime statistics for the current game.
- Resource Monitor: inspect loaded internal resources and external files.
- Memory Searcher: search and modify runtime values or copy them as `.cht` cheat codes.
- Debugger: inspect disassembly, registers, memory, and hit records for APP games, or ARM32 registers and memory for CC games.

Resource Monitor, Memory Searcher, and Debugger are available while a game is running.

### Troubleshooting

- Game does not start correctly: switch `CPU Execution Mode` to `Compatibility Mode`.
- No audio: make sure `Disable Audio` is off, then check master and system volume.
- Keyboard input problems: enable `Disable System IME` or reset Input Mapping.
- Instant save cannot be loaded: confirm it belongs to the current game and return to the same game phase.
- Cheats are unavailable: verify the `.cht` file name, game format, and game version.
