# Dingoo App Tool

## 中文说明

`dingoo-app-tool` 是面向丁果科技 `.app` 文件的 Windows 工具，目标样本包括
丁果 A320 掌机和兼容的歌美 X760+ 掌机游戏。`.app` 文件格式归丁果科技
所属。工具基于 DingooPie 当前的 `.app` 载入器知识，采用尽量保留原始字节
布局的工作流。

工具同时提供命令行版本和 Windows GUI 版本：

- `build\dingoo-app-tool.exe`：命令行工具。
- `build\dingoo-app-tool-gui.exe`：带进度显示的图形界面工具。

主要命令：

- `unpack` 读取 `.app`，校验主要区块头，导出元数据、主程序数据和已识别资源。
- `pack` 读取生成的 manifest，以原始容器镜像为基础重新生成 `.app`，可替换已导出的文件。
- `info` 输出简要结构信息，不写入文件。
- `stx-*` 从已识别的 `.stx` 资源中导出和导入可编辑文本条目。

默认回包模式会从解包时保存的原始文件字节开始处理。已知固定大小资源包会
原位更新；已识别的 `packed64` 资源包也支持通过插入或移除字节来适配资源
大小变化，并更新后续表项偏移。

### 构建

```powershell
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build -j 4
```

在 Windows 上会生成：

- `build\dingoo-app-tool.exe`：命令行工具。
- `build\dingoo-app-tool-gui.exe`：带进度显示的图形界面工具。

如果工具目录与 DingooPie 仓库并列，可以复用 DingooPie 的本地编译器：

```powershell
.\build_win64.ps1
```

### 常用命令

```powershell
.\build\dingoo-app-tool.exe info D:\Games\Snake.app
.\build\dingoo-app-tool.exe unpack D:\Games\Snake.app D:\Work\Snake-unpacked
.\build\dingoo-app-tool.exe pack D:\Work\Snake-unpacked\manifest.json D:\Work\Snake-repacked.app
```

往返校验示例：

```powershell
$a = (Get-FileHash D:\Games\Snake.app -Algorithm SHA256).Hash
$b = (Get-FileHash D:\Work\Snake-repacked.app -Algorithm SHA256).Hash
$a -eq $b
```

已在本地样本上验证：

- `Snake.app`：746 个 packed 资源，往返字节一致。
- `Candy.app`：192 个 packed 资源，往返字节一致。
- `7Days.app`：3574 个 `packed64` 资源，分布在 `audio`、`common`、`day*`、`ui` 和 `uien`，往返字节一致。

### 解包目录

- `manifest.json`：记录解析出的结构、偏移、大小和导出文件路径。
- `original.app.bin`：原始 `.app` 的完整字节副本，保守回包流程需要用它作为基础镜像，不能删除。
- `payload\rawd.bin`：`RAWD` 主程序数据，大小必须保持不变。
- `resources\...`：导出的资源文件，通常在这里修改资源。
- `tail\after_rawd.bin`：仅在存在尚未识别的 RAWD 后附加数据时生成。

### 修改与回包限制

- 可以修改 `resources\...` 下已有资源，然后使用原始 `manifest.json` 回包。
- 不要删除 `original.app.bin`、`payload\rawd.bin`、资源文件，或随意修改 `manifest.json` 中的资源路径。
- `packed64` 资源支持变大或变小，回包时会调整后续资源偏移。
- `packed` 和 `erpt` 资源目前必须保持原始字节大小。
- `payload\rawd.bin` 必须保持原始字节大小。
- 如果存在 `tail\after_rawd.bin`，它也必须保持原始字节大小。
- 新增文件不会自动加入资源表；工具只回包 manifest 中已经记录的资源。
- 容器回包成功不代表程序一定能读取修改后的资源，资源内部格式仍需保持有效。

### STX 文本编辑

部分样本会把界面文字和资源描述保存在 `.stx` 文件中。工具可以把已识别文本
字段导出为 TSV 文件，并导入编辑后的替换文本：

```powershell
.\build\dingoo-app-tool.exe stx-info D:\Work\7Days-unpacked\resources\common\0897_dj_ken_ab.stx
.\build\dingoo-app-tool.exe stx-export D:\Work\7Days-unpacked\resources\common\0897_dj_ken_ab.stx D:\Work\0897.tsv
.\build\dingoo-app-tool.exe stx-import D:\Work\7Days-unpacked\resources\common\0897_dj_ken_ab.stx D:\Work\0897.tsv D:\Work\0897-edited.stx
```

导出的 TSV 列为：

```text
id    encoding    offset_hex    byte_length    text    replacement
```

只编辑 `replacement` 列；留空表示保持该条目不变。替换文本会按该行记录的
编码写回。UTF-16LE STX 字符串记录和已识别的 GBK 长度前缀记录可以变长或
变短，导入时会更新对应长度字段。

当前 STX 支持：

- 带有已观察到 STX 字符串记录头的 UTF-16LE 文本字段。
- OLE/BIFF 类 STX 资源中出现的 GBK 长度前缀文本字段。
- 导入时保留周围 STX 字节，只调整已识别文本字段。

写出编辑后的 `.stx` 后，把它覆盖到解包目录中的对应资源，再使用原始
`manifest.json` 运行 `pack`。如果编辑的资源属于已识别的 `packed64`
资源包，输出 `.app` 可以变大或变小，并会调整后续资源偏移。其他资源包类型
仍要求资源大小不变。

### 当前格式知识

已知顶层区块：

- `CCDL`：固定 32 字节载入器区块。
- `IMPT`：导入表描述。
- `EXPT`：导出表描述。
- `RAWD`：原始可执行数据描述，包含入口、载入基址和程序大小。
- `ERPT`：部分样本中出现的可选资源表描述。

已识别资源：

- `ERPT` 资源使用 0x1fc 字节记录，并且看起来使用 `0x40` 做字节异或。
- 部分样本会在 RAWD 主载荷之后放置 packed 资源表；表内使用 16 位数量和 36 字节记录：32 字节名称加 32 位相对数据偏移。
- 部分较大的样本使用长路径资源表。工具把这类条目标记为 `packed64`：32 位存储偏移加 64 字节相对路径，例如 `.\audio\name.sau`。

工具会在 manifest 中同时记录十进制和十六进制偏移，方便排查格式问题。

### 使用说明

本项目不附带游戏样本。`.app` 文件格式归丁果科技所属。请只处理你合法取得
并有权使用的文件；不要把解包后的第三方资源提交到项目仓库。

## English

`dingoo-app-tool` is a Windows utility for Dingoo Technology `.app` files used
by Dingoo A320 handheld and compatible Gemei X760+ handheld software. The
`.app` package format belongs to Dingoo Technology. The tool is based on the
current DingooPie `.app` loader knowledge and starts with a conservative,
byte-preserving workflow.

The tool provides both command-line and Windows GUI builds:

- `build\dingoo-app-tool.exe`: command-line tool.
- `build\dingoo-app-tool-gui.exe`: GUI wrapper with progress display.

Main commands:

- `unpack` reads a `.app`, validates the main chunk headers, exports metadata,
  raw payload bytes, and detected resources.
- `pack` reads the generated manifest and rebuilds the container image from the
  original bytes, optionally replacing exported files before writing the output
  `.app`.
- `info` prints a compact summary without writing files.
- `stx-*` commands export and import editable text entries from detected `.stx`
  resource files.

The default pack mode starts from the original file bytes stored during unpack.
Known fixed-size package types are patched in place. Detected `packed64`
packages can also absorb resource size changes by inserting or removing bytes
and updating later table offsets.

### Build

```powershell
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build -j 4
```

On Windows this builds both:

- `build\dingoo-app-tool.exe`: command-line tool.
- `build\dingoo-app-tool-gui.exe`: GUI wrapper with progress display.

If you are working beside the DingooPie repository, you can reuse its bundled
compiler:

```powershell
.\build_win64.ps1
```

### Usage

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

### Unpacked Directory

- `manifest.json`: parsed structure, offsets, sizes, and exported file names.
- `original.app.bin`: byte-for-byte copy used by conservative repacking.
- `payload\rawd.bin`: raw executable payload from the `RAWD` chunk.
- `resources\...`: exported resource files, usually where resource edits are made.
- `tail\after_rawd.bin`: created only when unparsed data exists after `RAWD`.

### Editing And Repacking Limits

- You can edit existing files under `resources\...` and then run `pack` with the original `manifest.json`.
- Do not delete `original.app.bin`, `payload\rawd.bin`, resource files, or edit resource paths in `manifest.json` unless you also keep the manifest consistent.
- `packed64` resources may grow or shrink; later resource offsets are adjusted during `pack`.
- `packed` and `erpt` resources must keep their original byte size.
- `payload\rawd.bin` must keep its original byte size.
- `tail\after_rawd.bin`, when present, must keep its original byte size.
- Adding new resource files does not add entries to the package table. The tool repacks only resources already recorded in the manifest.
- Successful container repacking does not guarantee that an edited resource can be consumed by the program; the internal format of each resource still has to remain valid.

### STX Text Editing

Some samples store UI labels and resource descriptions in `.stx` files. The tool
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

### Current Format Knowledge

Known top-level chunks:

- `CCDL`: fixed 32-byte loader chunk.
- `IMPT`: import table descriptor.
- `EXPT`: export table descriptor.
- `RAWD`: raw executable descriptor, including entry, origin, and program size.
- `ERPT`: optional resource table descriptor found in several samples.

Detected resources:

- `ERPT` resources use 0x1fc-byte records and appear to store bytes XORed with
  `0x40`.
- Some samples use packed resource tables aligned after the raw payload. These
  tables use a 16-bit count and 36-byte records: 32-byte name plus 32-bit
  relative data offset.
- Some larger samples use long-path package tables. The tool labels these
  entries as `packed64`: 32-bit stored offset plus a 64-byte relative path such
  as `.\audio\name.sau`.

The tool records all numeric offsets in decimal and hexadecimal form in the
manifest for easier format troubleshooting.

### Use Notes

Game samples are not included in this project. The `.app` package format belongs
to Dingoo Technology. Use only files that you legally obtained and have the right
to process; do not commit unpacked third-party resources to the project
repository.
