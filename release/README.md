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
3. 最近一次成功运行的游戏会自动记录，下次启动会尝试自动载入。

也可以在命令行直接传入 `.app` 路径：

```bat
DingooPie.exe "D:\Games\Dingoo\Your Game.app"
```

默认设置：

| 项目 | 默认值 |
| --- | --- |
| 界面语言 | 中文 |
| 执行后端 | Auto |
| CPU Clock | Auto |
| Runtime Speed | Auto，按全局 60% 运行速度 |
| SDK Delay Scale | Auto，按全局 1.0 延迟比例 |
| 音量 | 100% |

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
| Esc | 退出模拟器 |
| F12 | 截图 |

菜单结构：

- `文件`：打开游戏、截图、退出。
- `选项`：视频、音频、输入。
- `设置`：语言、CPU、运行速度、SDK Delay、保存和恢复默认值。
- `调试`：调试控制台、性能日志、打开调试日志。
- `帮助`：关于 DingooPie。

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

This package does not include game `.app` files. Use legally obtained samples.
Use `manifest.sha256` to verify the executable, DLLs, and README after
packaging.
