DingooPie
=========

中文
----

丁果派 DingooPie 是 Windows 平台的 `.app` 游戏模拟器，面向丁果 A320 掌机，
并补充支持兼容的歌美 X760+ 掌机游戏。`.app` 格式文件归丁果科技所有。

版本：1.1
Powered by BL2CK Software
版权：Copyright (c) BL2CK 2026

快速使用：

1. 双击 `DingooPie.exe` 启动模拟器。
2. 通过 `文件 > 打开游戏` 选择游戏，或把 `.app` 文件拖到窗口中。
3. 最近游戏会自动记录到 `文件 > 最近游戏`，可在该子菜单清空；最近一次正常退出的游戏会在下次启动时尝试自动载入。

也可以在命令行直接传入 `.app` 路径：

```bat
DingooPie.exe "D:\Games\Dingoo\Your Game.app"
```

默认设置：

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
| CPU 后端 | 自动，默认映射到 PPSSPP IR JIT |
| CPU 时钟 | 自动，默认映射到 336 MHz |
| 运行速度 | 自动，默认按 65% 运行速度 |
| 延迟比例 | 自动，默认按 1.0 延迟比例 |
| 调试控制台 | 关闭 |
| 性能日志 | 关闭 |

常用按键：

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
| F12 | 自动截图 |

`Enter` 当前没有映射到任何 Dingoo 按键，避免和游戏输入产生冲突。
虚拟方向键包含斜向角按钮，按下后会同时触发对应的两个方向。
支持 SDL GameController 兼容手柄：十字键/左摇杆映射方向键，A/B/X/Y
映射同名 Dingoo 按键，Back/Start 映射 SELECT/START，肩键和扳机映射左右肩键。
可在 `选项 > 输入 > 按键映射` 独立窗口中分别设置键盘键、手柄按键、
摇杆方向或扳机；恢复默认按钮会清空对应设备的自定义配置。

菜单结构：

- `文件`：打开游戏、最近游戏/清除最近游戏、重启游戏、暂停/恢复游戏、保存截图、退出。
- `选项 > 视频`：1x、2x、3x、窗口化全屏、抗锯齿、滤镜、亮度、对比度、饱和度、最小化时、竖屏模式、FPS 显示。
- `选项 > 音频`：主音量、音频缓冲、禁用音频。
- `选项 > 输入`：禁用输入法、显示虚拟按键、按键映射。
- `设置`：CPU 后端、CPU 时钟、运行速度、延迟比例、语言、恢复默认设置。
- `调试`：调试控制台、性能日志、打开调试日志。
- `帮助`：关于 丁果派 DingooPie。

配置说明：

- 修改窗口缩放或全屏会保存设置并立即生效。
- 修改 CPU 后端会保存设置并重启模拟器，因为执行后端在启动时选择。
- 抗锯齿、滤镜、亮度、对比度、饱和度、最小化时、竖屏、FPS、音频、输入、CPU 时钟、运行速度、延迟比例、调试控制台和性能日志会保存并立即生效。
- 暂停/恢复游戏是运行态功能，只冻结当前游戏执行和音频输出，不写入 INI 配置。
- `video.minimized_behavior` 控制窗口最小化后的行为：`normal` 正常运行、`throttle` 降低帧率、`pause` 自动暂停；默认值为 `throttle`。
- 抗锯齿提供关闭、轻度和清晰三档；关闭使用最近邻采样，轻度使用线性采样，清晰使用线性采样和轻度清晰补偿。未知或非法 INI 值不做特殊兼容，会按当前默认值兜底并在下次保存时写回当前格式。
- 滤镜顺序为正常、黑白、反色、柔化、锐化、色彩增强、怀旧褐色、像素网格、LCD 扫描线、轻量 CRT；INI 分别使用 `normal`、`grayscale`、`invert`、`soft_blur`、`sharpen`、`vivid`、`sepia`、`pixel_grid`、`lcd_scanline`、`light_crt`。
- `禁用输入法` 默认开启，避免中文或其他输入法拦截游戏按键。
- `input.keyboard_mapping` 和 `input.controller_mapping` 分别保存键盘和手柄自定义映射；空值表示默认映射。示例：`Space=A,Return=Start`，`A=None,B=A,LeftX-=Left,LeftX+=Right`。

发布包不包含任何游戏 `.app` 文件。请使用自行合法取得的样本。
`manifest.sha256` 可用于校验 `DingooPie.exe`、DLL 和 README 是否完整。

English
-------

DingooPie is a Windows emulator for Dingoo Technology `.app` games used by the
Dingoo A320 handheld and compatible Gemei X760+ handheld software. The `.app`
package format belongs to Dingoo Technology.

Version: 1.1
Powered by BL2CK Software
Copyright (c) BL2CK 2026

Quick start:

1. Run `DingooPie.exe`.
2. Open a game from `File > Open Game`, or drop an `.app` file onto the window.
3. Recent games are kept under `File > Recent Games` and can be cleared there; the last game that exits normally is auto-loaded on the next start.

A `.app` path can also be passed on the command line:

```bat
DingooPie.exe "D:\Games\Dingoo\Your Game.app"
```

Default settings:

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
| CPU backend | Auto, mapped to PPSSPP IR JIT by default |
| CPU Clock | Auto, mapped to 336 MHz by default |
| Runtime Speed | Auto, mapped to 65% speed by default |
| Delay Scale | Auto, mapped to 1.0 delay scale by default |
| Debug console | Off |
| Profile log | Off |

Common keys:

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
| F12 | Save automatic screenshot |

`Enter` is intentionally unmapped so it cannot conflict with game input.
The virtual D-pad includes diagonal corner buttons that press the matching two
directions together.
SDL GameController-compatible pads are supported: D-pad/left stick map to the
D-pad, A/B/X/Y map to the matching Dingoo buttons, Back/Start map to
SELECT/START, and shoulder buttons/triggers map to the shoulders. Use
`Options > Input > Input Mapping` to open a standalone mapping window for both
keyboard and controller inputs. Each row lets you bind a keyboard key,
controller button, stick direction, or trigger, and restore defaults per device.

Menu structure:

- `File`: Open Game, Recent Games/Clear Recent Games, Restart Game, Pause/Resume Game, Save Screenshot, and Exit.
- `Options > Video`: 1x, 2x, 3x, windowed fullscreen, anti-aliasing, effects, brightness, contrast, saturation, minimized behavior, portrait mode, and FPS overlay.
- `Options > Audio`: master volume, audio buffer, and disable audio.
- `Options > Input`: disable IME, show virtual controls, and input mapping.
- `Settings`: CPU Backend, CPU Clock, Runtime Speed, Delay Scale, Language, and Restore Default Settings.
- `Debug`: Debug Console, Profile Log, and Open Debug Log.
- `Help`: About DingooPie.

Configuration notes:

- Changing window scale or fullscreen saves the setting and applies immediately.
- Changing the CPU backend saves the setting and relaunches the emulator because the execution backend is selected at startup.
- Anti-aliasing, effects, brightness, contrast, saturation, minimized behavior, portrait mode, FPS overlay, audio, input, CPU clock, runtime speed, delay scale, debug console, and profile log settings save and apply immediately.
- Pause/Resume Game is runtime-only. It freezes current game execution and audio output without writing to the INI configuration.
- `video.minimized_behavior` controls minimized-window behavior: `normal` runs normally, `throttle` lowers the frame rate, and `pause` auto-pauses. The default is `throttle`.
- Anti-aliasing offers Off, Low, and Clear. Off uses nearest sampling, Low uses linear sampling, and Clear uses linear sampling with a light clarity pass. Unknown or invalid INI values are not specially mapped; they fall back to current defaults and are rewritten in the current format on the next save.
- Effect order is Normal, Black and White, Invert, Soft Blur, Sharpen, Vivid, Sepia, Pixel Grid, LCD Scanline, and Light CRT. INI values are `normal`, `grayscale`, `invert`, `soft_blur`, `sharpen`, `vivid`, `sepia`, `pixel_grid`, `lcd_scanline`, and `light_crt`.
- `Disable IME` is enabled by default so Chinese or other input methods do not intercept gameplay keys.
- `input.keyboard_mapping` and `input.controller_mapping` store custom keyboard and controller bindings; empty values mean defaults. Examples: `Space=A,Return=Start` and `A=None,B=A,LeftX-=Left,LeftX+=Right`.

This package does not include game `.app` files. Use legally obtained samples.
Use `manifest.sha256` to verify the executable, DLLs, and README after
packaging.
