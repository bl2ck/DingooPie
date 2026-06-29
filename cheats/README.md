# 金手指 / Cheats

## 中文

金手指默认关闭。需要使用时：

1. 打开对应游戏。
2. 勾选 `设置 > 启用金手指`。
3. 在 `设置 > 金手指功能` 中勾选需要的功能。

金手指文件放在 `DingooPie.exe` 同级目录下的 `cheats` 文件夹中，并且必须与
游戏文件同名：

```text
游戏名.app -> cheats\游戏名.cht
```

没有同名 `.cht` 文件时，游戏会正常运行，也不会自动创建 `cheats` 目录。
如果 `.cht` 文件声明的 SHA256 与当前游戏不一致，DingooPie 会提示警告并停用
该金手指文件。

`设置 > 启用金手指` 的总开关会保存到 `DingooPie.ini`。每个具体功能默认不勾选，
只有在 `设置 > 金手指功能` 中勾选后才会生效；已勾选功能会按游戏保存，并在下次
启动同一游戏时自动恢复。勾选后会立即尝试应用。功能名变更后需要重新勾选。

## English

Cheats are disabled by default. To use them:

1. Open the matching game.
2. Check `Settings > Enable Cheats`.
3. Select features from `Settings > Cheat Features`.

Cheat files live in a `cheats` folder next to `DingooPie.exe`, and each file
must use the same base name as the game:

```text
GameName.app -> cheats\GameName.cht
```

If no same-name `.cht` file exists, the game runs normally and no `cheats`
folder is created automatically. If a `.cht` file declares a SHA256 that does
not match the current game, DingooPie shows a warning and disables that cheat
file.

The global `Settings > Enable Cheats` switch is saved to `DingooPie.ini`.
Each feature starts unchecked until it is selected from
`Settings > Cheat Features`. Selected features are saved per game and restored
when the same game starts again. Selection immediately tries to apply the
feature. If a feature name changes, select it again.
