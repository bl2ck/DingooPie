DingooPie
=========

中文
----

DingooPie 是 Windows 平台的 `.app` 游戏模拟器，面向丁果 A320 掌机，
并补充支持兼容的歌美 X760+ 掌机游戏。`.app` 文件格式归丁果科技所属。

版本：1.0
Powered by BL2CK
版权：Copyright (c) BL2CK 2026

快速使用：

1. 双击 `DingooPie.exe` 启动模拟器。
2. 通过 `文件 > 打开游戏 .app` 选择游戏。
3. 最近一次正常退出的游戏会自动记录，下次启动会尝试自动载入。

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

菜单结构：

- `文件`：打开游戏、重启游戏、保存截图、退出。
- `选项 > 视频`：1x、2x、3x、窗口化全屏、采样方式、滤镜、亮度、对比度、饱和度、竖屏模式、FPS 显示。
- `选项 > 音频`：主音量、音频缓冲、禁用音频。
- `选项 > 输入`：显示虚拟按键、禁用输入法。
- `设置`：CPU 后端、CPU Clock、Runtime Speed、SDK Delay Scale、语言、保存设置、恢复默认设置。
- `调试`：调试控制台、性能日志、打开调试日志。
- `帮助`：关于 DingooPie。

配置说明：

- 修改窗口缩放或全屏会保存设置并重启模拟器。
- 修改 CPU 后端会保存设置并重启模拟器，因为执行后端在启动时选择。
- 采样方式、滤镜、亮度、对比度、饱和度、竖屏、FPS、音频、输入、CPU 时钟、运行速度、SDK 延迟比例、调试控制台和性能日志会保存并立即生效。
- `禁用输入法` 默认开启，避免中文或其他输入法拦截游戏按键。

发布包不包含任何游戏 `.app` 文件。请使用自行合法取得的样本。
`manifest.sha256` 可用于校验 `DingooPie.exe`、DLL 和 README 是否完整。

English
-------

DingooPie is a Windows emulator for Dingoo Technology `.app` games used by the
Dingoo A320 handheld and compatible Gemei X760+ handheld software. The `.app`
package format belongs to Dingoo Technology.

Version: 1.0
Powered by BL2CK
Copyright (c) BL2CK 2026

Quick start:

1. Run `DingooPie.exe`.
2. Open a game from `File > Open Game .app`.
3. The last game that exits normally is saved and auto-loaded on the next start.

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

Menu structure:

- `File`: Open Game, Restart Game, Save Screenshot, and Exit.
- `Options > Video`: 1x, 2x, 3x, windowed fullscreen, sampling, effects, brightness, contrast, saturation, portrait mode, and FPS overlay.
- `Options > Audio`: master volume, audio buffer, and disable audio.
- `Options > Input`: show virtual controls and disable IME.
- `Settings`: CPU Backend, CPU Clock, Runtime Speed, SDK Delay Scale, Language, Save Settings, and Restore Default Settings.
- `Debug`: Debug Console, Profile Log, and Open Debug Log.
- `Help`: About DingooPie.

Configuration notes:

- Changing window scale or fullscreen saves the setting and relaunches the emulator.
- Changing the CPU backend saves the setting and relaunches the emulator because the execution backend is selected at startup.
- Sampling, effects, brightness, contrast, saturation, portrait mode, FPS overlay, audio, input, CPU clock, runtime speed, SDK delay scale, debug console, and profile log settings save and apply immediately.
- `Disable IME` is enabled by default so Chinese or other input methods do not intercept gameplay keys.

This package does not include game `.app` files. Use legally obtained samples.
Use `manifest.sha256` to verify the executable, DLLs, and README after
packaging.
