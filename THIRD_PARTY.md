# Third-Party Sources

This project builds with the following third-party components.

| Component | Version / Source | Purpose |
| --- | --- | --- |
| w64devkit | `v2.8.0`, `w64devkit-x64-2.8.0.7z.exe` | MinGW-w64 GCC, CMake, Make, and core Windows build tools. |
| SDL2 | `2.26.5`, official development package `SDL2-devel-2.26.5-mingw.zip` | Window, input, timer, and audio backend. |
| Capstone | MSYS2 package `mingw-w64-x86_64-capstone 5.0.9-1` | MIPS disassembly diagnostics. |
| PPSSPP | GitHub source archive `ppsspp-master.zip` plus `patches/ppsspp-irjit-dingoo.patch` | IR/x64 MIPS JIT backend core. |
| Dingoo SDK references | `flatmush/dingoo-sdk` snapshot, when available | SDK import names and constants. |
| MinGW pthread runtime | `libwinpthread-1.dll` from the configured MinGW toolchain | Runtime support for host pthread-backed guest task scheduling. |

The package does not include game `.app` files. The `.app` package format
belongs to Dingoo Technology, and users must provide their own legally obtained
samples for Dingoo A320 or compatible Gemei X760+ software.
