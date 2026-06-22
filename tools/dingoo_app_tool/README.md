# Dingoo App Tool

## 中文说明

`dingoo-app-tool` 是面向丁果科技 `.app` 文件的保守型解包、回包工具，主要用于检查和编辑
丁果 A320 与兼容歌美 X760+ 软件中的资源文件。工具的设计目标是尽量保留原始容器字节布局，
只替换 manifest 中已记录的内容。

工具会构建两个可执行文件：

- `dingoo-app-tool.exe`：命令行工具，覆盖结构检查、解包、回包和 STX 文本导出/导入。
- `dingoo-app-tool-gui.exe`：Windows 图形界面，覆盖常用的解包和回包流程，并显示进度和日志。

### 功能范围

命令行支持的命令与 `src\main.cpp` 中的用法一致：

- `info <input.app>`：解析 `.app` 并输出结构摘要，不写入文件。
- `unpack <input.app> <output-dir>`：导出 manifest、原始镜像副本、RAWD 主程序数据和已识别资源。
- `pack <manifest.json> <output.app>`：读取解包目录中的 manifest 和导出文件，重新生成 `.app`。
- `stx-info <input.stx>`：扫描 `.stx` 文件中已识别的文本条目。
- `stx-export <input.stx> <output.tsv>`：把已识别 STX 文本导出为 TSV。
- `stx-import <input.stx> <input.tsv> <output.stx>`：把 TSV 中的替换文本写入新的 `.stx` 文件。

GUI 当前只提供 `unpack` 和 `pack`。STX 文本编辑仍使用命令行。

### 构建

通用 CMake 构建：

```powershell
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build -j 4
```

Windows 构建产物：

- `build\dingoo-app-tool.exe`
- `build\dingoo-app-tool-gui.exe`

`build_win64.ps1` 会从脚本目录上两级位置寻找 `w64devkit\bin\cmake.exe`。如果工具目录不在带有
`w64devkit` 的仓库下面，显式传入 DingooPie 根目录：

```powershell
.\build_win64.ps1 -DingooPieRoot D:\Project\C++\dingoo-pie
```

### 常用流程

```powershell
.\build\dingoo-app-tool.exe info D:\Games\Snake.app
.\build\dingoo-app-tool.exe unpack D:\Games\Snake.app D:\Work\Snake-unpacked
.\build\dingoo-app-tool.exe pack D:\Work\Snake-unpacked\manifest.json D:\Work\Snake-repacked.app
```

未修改资源时，可以用哈希比较确认往返结果：

```powershell
$a = (Get-FileHash D:\Games\Snake.app -Algorithm SHA256).Hash
$b = (Get-FileHash D:\Work\Snake-repacked.app -Algorithm SHA256).Hash
$a -eq $b
```

当前已用工具的 `info` 命令核对过这些本地样本：

- `迪克蛇.app`：746 个 `packed` 资源。
- `糖果屋.app`：192 个 `packed` 资源。
- `七夜正式版.app`：3574 个 `packed64` 资源。

### 解包产物

`unpack` 会生成：

- `manifest.json`：解析出的结构、偏移、大小、资源类型、异或键和导出路径。
- `original.app.bin`：原始 `.app` 的完整字节副本；`pack` 会以它作为基础镜像。
- `payload\rawd.bin`：`RAWD` 主程序数据。
- `resources\...`：已识别资源的解码后副本，通常在这里修改资源。
- `tail\after_rawd.bin`：仅在 RAWD 后存在未识别附加数据时生成。

不要删除 `manifest.json`、`original.app.bin`、`payload\rawd.bin` 或 manifest 中记录的资源文件。
新增文件不会自动加入资源表。

### 回包规则

`pack` 采用保守策略：先读取 `original.app.bin`，再按 manifest 替换 RAWD、未识别尾部数据和资源内容。

- `payload\rawd.bin` 必须保持原始字节大小。
- `tail\after_rawd.bin` 如果存在，也必须保持原始字节大小。
- `packed` 和 `erpt` 资源目前必须保持原始字节大小。
- 当 manifest 中可回包资源全部是 `packed64` 时，`packed64` 资源可以变大或变小；工具会插入或删除字节，
  并更新后续表项偏移。
- 资源文件内容会按 manifest 中的 `xor_key` 重新编码；已知 `erpt` 资源使用 `0x40` 异或。
- 容器回包成功不代表目标程序一定能读取修改后的资源；资源内部格式仍需保持有效。

### STX 文本编辑

部分 `.stx` 文件包含界面文字或资源描述。工具可把已识别文本字段导出为 TSV，并导入替换文本：

```powershell
.\build\dingoo-app-tool.exe stx-info D:\Work\7Days-unpacked\resources\common\0897_dj_ken_ab.stx
.\build\dingoo-app-tool.exe stx-export D:\Work\7Days-unpacked\resources\common\0897_dj_ken_ab.stx D:\Work\0897.tsv
.\build\dingoo-app-tool.exe stx-import D:\Work\7Days-unpacked\resources\common\0897_dj_ken_ab.stx D:\Work\0897.tsv D:\Work\0897-edited.stx
```

TSV 列：

```text
id    encoding    offset_hex    byte_length    text    replacement
```

只编辑 `replacement` 列；留空表示该条目不变。当前支持：

- 带已观察到 STX 字符串记录头的 UTF-16LE 文本字段。
- Windows 构建中，OLE/BIFF 类 STX 资源里的 GBK 长度前缀文本字段。
- UTF-16LE 记录和已识别 GBK 长度前缀记录的变长导入。

导入生成新的 `.stx` 后，把它覆盖到解包目录中的对应资源，再用原始 `manifest.json` 执行 `pack`。

### 当前格式知识

已知顶层区块：

- `CCDL`：固定位置载入器区块。
- `IMPT`：导入表描述。
- `EXPT`：导出表描述。
- `RAWD`：原始可执行数据描述，包含入口、载入基址和程序大小。
- `ERPT`：部分样本中的可选资源表描述。

已识别资源表：

- `erpt`：0x1fc 字节记录，资源数据按 `0x40` 异或编码。
- `packed`：RAWD 主载荷之后按 0x1000 对齐扫描到的短名称资源表；记录为 32 字节名称加 32 位相对数据偏移。
- `packed64`：长路径资源表；记录为 32 位存储偏移加 64 字节相对路径，例如 `.\audio\name.sau`。

manifest 会同时记录十进制和十六进制偏移，便于排查格式问题。

### 使用边界

`.app` 文件格式和相关游戏资源归原权利方所有。请只处理你合法取得并有权使用的文件；
不要把解包后的第三方资源提交到项目仓库。

## English

`dingoo-app-tool` is a conservative unpack/repack utility for Dingoo Technology
`.app` files. It is intended for inspecting and editing resources from Dingoo
A320 and compatible Gemei X760+ software while preserving as much of the
original container layout as possible.

The build produces two executables:

- `dingoo-app-tool.exe`: command-line tool for inspection, unpacking, repacking,
  and STX text export/import.
- `dingoo-app-tool-gui.exe`: Windows GUI for the common unpack and pack
  workflow, with progress and log output.

### Scope

The command-line interface matches the usage in `src\main.cpp`:

- `info <input.app>`: parse an `.app` file and print a compact summary.
- `unpack <input.app> <output-dir>`: export the manifest, original image copy,
  RAWD payload, and detected resources.
- `pack <manifest.json> <output.app>`: rebuild an `.app` from the unpacked
  manifest and exported files.
- `stx-info <input.stx>`: scan detected text entries in an `.stx` file.
- `stx-export <input.stx> <output.tsv>`: export detected STX text to TSV.
- `stx-import <input.stx> <input.tsv> <output.stx>`: apply TSV replacements to
  a new `.stx` file.

The GUI currently covers only `unpack` and `pack`. STX text editing is done
through the command line.

### Build

Generic CMake build:

```powershell
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build -j 4
```

Windows outputs:

- `build\dingoo-app-tool.exe`
- `build\dingoo-app-tool-gui.exe`

`build_win64.ps1` looks for `w64devkit\bin\cmake.exe` two directories above the
script directory. If the tool directory is not under a repository that contains
`w64devkit`, pass the DingooPie root explicitly:

```powershell
.\build_win64.ps1 -DingooPieRoot D:\Project\C++\dingoo-pie
```

### Common Workflow

```powershell
.\build\dingoo-app-tool.exe info D:\Games\Snake.app
.\build\dingoo-app-tool.exe unpack D:\Games\Snake.app D:\Work\Snake-unpacked
.\build\dingoo-app-tool.exe pack D:\Work\Snake-unpacked\manifest.json D:\Work\Snake-repacked.app
```

When resources are not edited, compare hashes to confirm the round trip:

```powershell
$a = (Get-FileHash D:\Games\Snake.app -Algorithm SHA256).Hash
$b = (Get-FileHash D:\Work\Snake-repacked.app -Algorithm SHA256).Hash
$a -eq $b
```

Current local samples checked with `info`:

- `迪克蛇.app`: 746 `packed` resources.
- `糖果屋.app`: 192 `packed` resources.
- `七夜正式版.app`: 3574 `packed64` resources.

### Unpacked Files

`unpack` creates:

- `manifest.json`: parsed structure, offsets, sizes, resource kinds, XOR keys,
  and export paths.
- `original.app.bin`: byte-for-byte copy of the original `.app`; `pack` uses it
  as the base image.
- `payload\rawd.bin`: RAWD program payload.
- `resources\...`: decoded resource files, usually where edits are made.
- `tail\after_rawd.bin`: created only when unrecognized data exists after RAWD.

Do not delete `manifest.json`, `original.app.bin`, `payload\rawd.bin`, or any
resource file recorded in the manifest. Adding files does not add package-table
entries.

### Repacking Rules

`pack` is conservative: it reads `original.app.bin`, then replaces RAWD,
unparsed tail data, and resource content according to the manifest.

- `payload\rawd.bin` must keep its original byte size.
- `tail\after_rawd.bin`, when present, must keep its original byte size.
- `packed` and `erpt` resources must keep their original byte size.
- When every repackable resource in the manifest is `packed64`, `packed64`
  resources may grow or shrink; later table offsets are updated.
- Resource content is re-encoded with the manifest `xor_key`; known `erpt`
  resources use XOR key `0x40`.
- Successful container repacking does not guarantee that the target program can
  consume an edited resource. Each resource's internal format still has to stay
  valid.

### STX Text Editing

Some `.stx` files contain UI labels or resource descriptions. The tool exports
detected text fields to TSV and imports replacements:

```powershell
.\build\dingoo-app-tool.exe stx-info D:\Work\7Days-unpacked\resources\common\0897_dj_ken_ab.stx
.\build\dingoo-app-tool.exe stx-export D:\Work\7Days-unpacked\resources\common\0897_dj_ken_ab.stx D:\Work\0897.tsv
.\build\dingoo-app-tool.exe stx-import D:\Work\7Days-unpacked\resources\common\0897_dj_ken_ab.stx D:\Work\0897.tsv D:\Work\0897-edited.stx
```

TSV columns:

```text
id    encoding    offset_hex    byte_length    text    replacement
```

Edit only the `replacement` column. Leave it blank to keep an entry unchanged.
Current support:

- UTF-16LE text fields with the observed STX string record header.
- In Windows builds, GBK length-prefixed text fields seen in OLE/BIFF-like STX
  resources.
- Resizable import for UTF-16LE records and detected GBK length-prefixed records.

After importing a new `.stx`, replace the matching resource in the unpacked
directory and run `pack` with the original `manifest.json`.

### Current Format Knowledge

Known top-level chunks:

- `CCDL`: fixed-position loader chunk.
- `IMPT`: import table descriptor.
- `EXPT`: export table descriptor.
- `RAWD`: raw executable descriptor, including entry, origin, and program size.
- `ERPT`: optional resource table descriptor found in some samples.

Detected resource tables:

- `erpt`: 0x1fc-byte records, resource data XOR-encoded with `0x40`.
- `packed`: short-name tables scanned after RAWD on 0x1000 alignment; records
  are 32-byte names plus 32-bit relative data offsets.
- `packed64`: long-path tables; records are 32-bit stored offsets plus 64-byte
  relative paths such as `.\audio\name.sau`.

The manifest records offsets in both decimal and hexadecimal form for easier
format troubleshooting.

### Use Boundaries

The `.app` format and related game resources belong to their original rights
holders. Use only files that you legally obtained and have the right to process;
do not commit unpacked third-party resources to the project repository.
