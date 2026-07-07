# 金手指 / Cheats

## 中文

金手指默认关闭。使用步骤：

1. 打开对应游戏。
2. 勾选 `设置 > 金手指 > 启用金手指`。
3. 在 `设置 > 金手指` 中勾选需要的功能，或打开
   `设置 > 金手指 > 金手指管理器` 用列表视图管理。

金手指文件放在 `DingooPie.exe` 同级目录下的 `cheats` 文件夹中，并且必须与
游戏文件同名：

```text
游戏名.app -> cheats\游戏名.cht
```

没有同名 `.cht` 文件时，游戏会正常运行。如果 `.cht` 文件声明的 `app_sha256`
与当前游戏不一致，DingooPie 会提示警告并停用该金手指文件。

已勾选的功能会按游戏保存，并在下次启动同一游戏时自动恢复。功能名变更后需要
重新勾选。

### .cht 文件格式

`.cht` 是 UTF-8 文本文件。空行会被忽略，`#` 后面的内容会作为注释忽略。

可选校验行：

```text
app_sha256=<当前游戏的 64 位十六进制 SHA256>
```

金手指记录格式：

```text
status|name|width|address|value
status|name|width|address|value|compare
```

字段说明：

- `status`：普通记录写 `on`；只需要写入一次的记录写 `once`。
- `name`：菜单中显示的功能名。需要多处写入同一功能时，可使用相同功能名前缀，
  例如 `解锁全部/Unlock All：步骤1` 和 `解锁全部/Unlock All：步骤2`。
  斜杠前后分别作为中文/英文显示名。
- `width`：写入宽度，支持 `u8`、`u16`、`u32`。
- `address`：客体内存地址。
- `value`：要写入的值。
- `compare`：可选的当前值检查；只有内存当前值匹配时才写入。

数字支持十进制、`0x` 十六进制和 `$` 十六进制写法。`u16/u32` 地址必须按宽度
对齐。做代码补丁时建议填写 `compare`，避免误改其他版本的游戏。

示例：

```text
app_sha256=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef
on|生命不减/Infinite HP|u16|0x80A01234|999
once|解锁全部/Unlock All：隐藏锁图标|u32|0x80A9B190|0x1000000F|0x1040000F
once|解锁全部/Unlock All：允许选择|u32|0x80A9C3E8|0x10000005|0x10400005
```

游戏运行时可用 `调试 > 内存搜索器` 查找 u8/u16/u32 数值，并把选中的地址复制为
`.cht` 行，再粘贴到对应游戏的金手指文件中。

## English

Cheats are disabled by default. To use them:

1. Open the matching game.
2. Check `Settings > Cheats > Enable Cheats`.
3. Select features from `Settings > Cheats`, or open
   `Settings > Cheats > Cheat Manager` for the list view.

Cheat files live in a `cheats` folder next to `DingooPie.exe`, and each file
must use the same base name as the game:

```text
GameName.app -> cheats\GameName.cht
```

If no same-name `.cht` file exists, the game runs normally. If a `.cht` file
declares an `app_sha256` that does not match the current game, DingooPie shows a
warning and disables that cheat file.

Selected features are saved per game and restored when the same game starts
again. If a feature name changes, select it again.

### .cht File Format

`.cht` files are UTF-8 text files. Blank lines are ignored, and text after `#`
is treated as a comment.

Optional guard line:

```text
app_sha256=<64-hex SHA256 of the current game>
```

Cheat records use one of these forms:

```text
status|name|width|address|value
status|name|width|address|value|compare
```

Fields:

- `status`: use `on` for a normal record; use `once` for a one-time write.
- `name`: feature name shown in the menu. Multiple writes for the same feature
  can use the same prefix, for example `Unlock All：step 1` and
  `Unlock All：step 2`. Use `Chinese/English` for localized labels.
- `width`: write width, one of `u8`, `u16`, or `u32`.
- `address`: guest memory address.
- `value`: value to write.
- `compare`: optional current value required before writing.

Numbers may be decimal, `0x` hexadecimal, or `$` hexadecimal. `u16/u32`
addresses must be aligned to their width. Use compare values for code patches
whenever possible so a patch does not silently change another game version.

Example:

```text
app_sha256=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef
on|生命不减/Infinite HP|u16|0x80A01234|999
once|解锁全部/Unlock All：隐藏锁图标|u32|0x80A9B190|0x1000000F|0x1040000F
once|解锁全部/Unlock All：允许选择|u32|0x80A9C3E8|0x10000005|0x10400005
```

While a game is running, `Debug > Memory Searcher` can search u8/u16/u32 values
and copy selected addresses as `.cht` lines for the matching cheat file.
