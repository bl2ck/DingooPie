# Dingoo App Tool

`dingoo-app-tool` is a Windows command-line utility for Dingoo Technology
`.app` files used by Dingoo A320 handheld and compatible Gemei X760+ handheld
software. It is based on the current DingooPie `.app` loader knowledge and
starts with a conservative, byte-preserving workflow:

- `unpack` reads a `.app`, validates the main chunk headers, exports metadata,
  raw payload bytes, and detected resources.
- `pack` reads the generated manifest and rebuilds the original container image,
  optionally replacing exported files before writing the output `.app`.
- `info` prints a compact summary without writing files.
- `stx-*` commands export and import editable text entries from detected `.stx`
  resource files.

The default pack mode starts from the original file bytes stored during unpack.
Known fixed-size package types are patched in place. Detected `packed64`
packages can also absorb resource size changes by inserting/removing bytes and
updating later table offsets.

## 中文说明

`dingoo-app-tool` 用于解析、解包和回包丁果科技 `.app` 文件，目标样本包括
丁果 A320 掌机和兼容的歌美 X760+ 掌机游戏。工具同时提供命令行版本和
Windows GUI 版本：

- `build\dingoo-app-tool.exe`：命令行工具。
- `build\dingoo-app-tool-gui.exe`：带进度显示的图形界面工具。

常用命令：

```powershell
.\build\dingoo-app-tool.exe info D:\Games\Snake.app
.\build\dingoo-app-tool.exe unpack D:\Games\Snake.app D:\Work\Snake-unpacked
.\build\dingoo-app-tool.exe pack D:\Work\Snake-unpacked\manifest.json D:\Work\Snake-repacked.app
```

解包目录说明：

- `manifest.json`：记录解析出的结构、偏移、大小和导出文件路径。
- `original.app.bin`：原始 `.app` 的完整字节副本，保守回包流程需要用它作为基础镜像，不能删除。
- `payload\rawd.bin`：`RAWD` 主程序数据，大小必须保持不变。
- `resources\...`：导出的资源文件，通常在这里修改资源。
- `tail\after_rawd.bin`：仅在存在尚未识别的 RAWD 后附加数据时生成。

修改资源后的回包限制：

- 可以修改 `resources\...` 下已有资源，然后使用原始 `manifest.json` 回包。
- 不要删除 `original.app.bin`、`payload\rawd.bin`、资源文件，或随意修改 `manifest.json` 中的资源路径。
- `packed64` 资源支持变大或变小，回包时会调整后续资源偏移。
- `packed` 和 `erpt` 资源目前必须保持原始字节大小。
- `payload\rawd.bin` 必须保持原始字节大小。
- 如果存在 `tail\after_rawd.bin`，它也必须保持原始字节大小。
- 新增文件不会自动加入资源表；工具只回包 manifest 中已经记录的资源。
- 容器回包成功不代表游戏一定能读取修改后的资源，资源内部格式仍需保持有效。

## Build

```powershell
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build -j 4
```

On Windows this builds both:

- `build\dingoo-app-tool.exe`: command-line tool.
- `build\dingoo-app-tool-gui.exe`: GUI wrapper for unpacking and packing with progress display.

If you are working beside the DingooPie repository, you can reuse its bundled
compiler:

```powershell
.\build_win64.ps1
```

## Usage

```powershell
.\build\dingoo-app-tool.exe info D:\Games\Snake.app
.\build\dingoo-app-tool.exe unpack D:\Games\Snake.app D:\Work\Snake-unpacked
.\build\dingoo-app-tool.exe pack D:\Work\Snake-unpacked\manifest.json D:\Work\Snake-repacked.app
```

Round-trip check:

```powershell
$a = (Get-FileHash D:\Games\Snake.app -Algorithm SHA256).Hash
$b = (Get-FileHash D:\Work\Snake-repacked.app -Algorithm SHA256).Hash
$a -eq $b
```

Known local verification samples:

- `Snake.app`: 746 packed resources, byte-identical round trip.
- `Candy.app`: 192 packed resources, byte-identical round trip.
- `7Days.app`: 3574 `packed64` resources across `audio`, `common`, `day*`,
  `ui`, and `uien`, byte-identical round trip.

After unpacking, the output directory contains:

- `manifest.json`: parsed structure, offsets, sizes, and exported file names.
- `original.app.bin`: byte-for-byte copy used by conservative repacking.
- `payload\rawd.bin`: raw executable payload from the `RAWD` chunk.
- `resources\...`: detected ERPT or packed resources, decoded when the format
  uses a known XOR key.
- `tail\after_rawd.bin`: created only when there is unparsed data after `RAWD`
  for samples whose appended package/resource format is not identified yet.

## Editing And Repacking Limits

You can edit exported files under `resources\...` and then run `pack` with the
original `manifest.json`, but the manifest describes existing resources only.
Do not delete `original.app.bin`, `payload\rawd.bin`, resource files, or edit
resource paths in `manifest.json` unless you also know how to keep the manifest
consistent.

Current size-change support:

- `packed64` resources may grow or shrink; later resource offsets are adjusted
  during `pack`.
- `packed` and `erpt` resources must keep their original byte size.
- `payload\rawd.bin` must keep its original byte size.
- `tail\after_rawd.bin`, when present, must keep its original byte size.

Adding new resource files does not add entries to the package table. Successful
container repacking also does not guarantee that the game can consume an edited
resource; the internal format of each resource still has to remain valid.

## STX Text Editing

Some games store UI labels and resource descriptions in `.stx` files. The tool
exports detected text fields to a TSV file and imports edited replacements:

```powershell
.\build\dingoo-app-tool.exe stx-info D:\Work\7Days-unpacked\resources\common\0897_dj_ken_ab.stx
.\build\dingoo-app-tool.exe stx-export D:\Work\7Days-unpacked\resources\common\0897_dj_ken_ab.stx D:\Work\0897.tsv
.\build\dingoo-app-tool.exe stx-import D:\Work\7Days-unpacked\resources\common\0897_dj_ken_ab.stx D:\Work\0897.tsv D:\Work\0897-edited.stx
```

The exported TSV columns are:

```text
id    encoding    offset_hex    byte_length    text    replacement
```

Edit only the `replacement` column. Leave it blank to keep an entry unchanged.
Replacement text is encoded using the row encoding. UTF-16LE STX string records
and detected GBK length-prefixed records may grow or shrink; their record length
fields are updated during import.

Current STX support:

- UTF-16LE text fields with the observed STX string record header.
- GBK length-prefixed text fields seen in OLE/BIFF-like STX resources.
- Import preserves the surrounding STX bytes and only resizes known text fields.

After writing an edited `.stx`, copy it over the exported resource in the
unpacked directory and run `pack` with the original `manifest.json`. If the
edited resource belongs to a detected `packed64` package, the output `.app` can
grow or shrink and later resource offsets are adjusted. Other package types
still require unchanged resource sizes during `pack`.

## Current Format Knowledge

Known top-level chunks:

- `CCDL`: fixed 32-byte loader chunk.
- `IMPT`: import table descriptor.
- `EXPT`: export table descriptor.
- `RAWD`: raw executable descriptor, including entry, origin, and program size.
- `ERPT`: optional resource table descriptor found in several games.

Detected resources:

- `ERPT` resources use 0x1fc-byte records and appear to store bytes XORed with
  `0x40`.
- Some games use packed resource tables aligned after the raw payload. These
  tables use a 16-bit count and 36-byte records: 32-byte name plus 32-bit
  relative data offset.
- Some larger games use long-path package tables. The tool labels these entries
  as `packed64`: 32-bit stored offset plus a 64-byte relative path such as
  `.\audio\name.sau`.

The tool records all numeric offsets in decimal and hexadecimal form in the
manifest for easier debugging.

## Safety

Game samples are not included in this project. The `.app` package format belongs
to Dingoo Technology. Do not commit unpacked commercial game data unless you own
the content and intentionally want to version it.
