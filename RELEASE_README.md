DingooPie Release
=================

中文
----

丁果派 DingooPie 是 Windows 平台的 `.app` 游戏模拟器，用于运行丁果 A320
及兼容歌美 X760+ 掌机游戏。本发布包不包含任何游戏文件，请使用自行合法取得
的 `.app` 样本。

快速使用：

1. 双击 `DingooPie.exe` 启动。
2. 通过 `文件 > 打开游戏` 选择 `.app`，或把 `.app` 拖到窗口中。
3. 最近游戏会记录到 `文件 > 最近游戏`，可在该子菜单清除。

命令行也可以直接传入游戏路径：

```bat
DingooPie.exe "D:\Games\Dingoo\Your Game.app"
```

常用按键：

| 键盘 | 掌机按键 |
| --- | --- |
| 方向键 / WASD | 方向键 |
| L / K / I / J | A / B / X / Y |
| 1 / Q | SELECT |
| 0 / O | START |
| 左 Shift / 右 Shift | 左 / 右肩键 |
| Esc | 询问退出 |
| F12 | 保存截图 |

支持 SDL GameController 兼容手柄。可在
`选项 > 输入 > 按键映射` 中自定义键盘和手柄映射。

配置说明：

- 配置保存在 `DingooPie.ini`，位于 `DingooPie.exe` 同目录。
- 窗口缩放、全屏、抗锯齿、滤镜、亮度、对比度、饱和度、最小化时、
  竖屏、FPS、音频、输入、CPU 时钟、运行速度、延迟比例、调试控制台和
  性能日志会保存并立即生效。
- 修改 CPU 后端会保存设置并重启模拟器，因为后端在启动时选择。
- 暂停/恢复游戏只影响当前运行状态，不写入 INI 配置。
- `manifest.sha256` 可用于校验发布包文件完整性。

发布包文件：

- `DingooPie.exe`
- `SDL2.dll`
- `libcapstone.dll`
- `libwinpthread-1.dll`
- `README.md`
- `manifest.sha256`

English
-------

DingooPie is a Windows emulator for Dingoo A320 `.app` games and compatible
Gemei X760+ handheld games. This release does not include game files. Use
legally obtained `.app` samples.

Quick start:

1. Run `DingooPie.exe`.
2. Open an `.app` from `File > Open Game`, or drop an `.app` onto the window.
3. Recent games are stored under `File > Recent Games` and can be cleared there.

You can also pass a game path on the command line:

```bat
DingooPie.exe "D:\Games\Dingoo\Your Game.app"
```

Common keys:

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

Configuration:

- Settings are saved to `DingooPie.ini` next to `DingooPie.exe`.
- Window scale, fullscreen, anti-aliasing, effects, brightness, contrast,
  saturation, minimized behavior, portrait mode, FPS overlay, audio, input,
  CPU clock, runtime speed, delay scale, debug console, and profile log settings
  save and apply immediately.
- Changing the CPU backend saves settings and relaunches the emulator because
  the backend is selected at startup.
- Pause/Resume only affects the current runtime state and is not written to INI.
- Use `manifest.sha256` to verify release file integrity.

Release files:

- `DingooPie.exe`
- `SDL2.dll`
- `libcapstone.dll`
- `libwinpthread-1.dll`
- `README.md`
- `manifest.sha256`

Version: 1.1
Powered by BL2CK Software
