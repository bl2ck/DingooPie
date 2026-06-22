# Packaging

This project has two packaging paths: a runnable Windows release and a source
archive. Game `.app` files are never packaged.

## Runnable Release

Run:

```powershell
Set-ExecutionPolicy -Scope Process Bypass -Force
.\scripts\bootstrap_windows.ps1
.\scripts\build_release.ps1
```

`build_release.ps1` builds `Release` into `build\win64` and writes the runnable
package to `release\`. The release contains:

- `DingooPie.exe`
- `SDL2.dll`
- `libcapstone.dll`
- `libwinpthread-1.dll`
- `README.md`
- `manifest.sha256`

`README.md` is copied from `RELEASE_README.md` when that file exists; otherwise
the root `README.md` is used. `manifest.sha256` records SHA256 hashes for the
release files. Stale files in `release\` are removed after the manifest is
written.

Required runtime DLLs must exist before release generation. `libwinpthread-1.dll`
is resolved from `w64devkit\bin` first, then from
`deps_extract\winpthread\mingw64\bin`. Missing required files fail the script.

## Source Archive

Run:

```powershell
.\scripts\package_project.ps1
```

`package_project.ps1` writes `dist\DingooPie-source.zip`, extracts it to
`dist\_verify`, and validates the extracted manifest. In a Git worktree it
copies tracked project files from:

- `dingoo_pie\`
- `docs\`
- `patches\`
- `resources\`
- `scripts\`
- `tools\`

It also includes the root project files required to rebuild the source package,
such as `CMakeLists.txt`, `LICENSE`, `README.md`, `THIRD_PARTY.md`,
`.gitignore`, and `configure_win64.bat`. `RELEASE_README.md` is included when
present.

The source archive excludes local workspaces and generated artifacts such as
`downloads\`, `third_party\`, `deps_extract\`, `w64devkit\`, `build\`,
`release\`, and `dist\`. The script fails if the staged or verified package
contains forbidden files such as `.app`, `.apk`, `.exe`, `.dll`, logs, debug
screenshots, or framebuffer dumps.

## Third-Party Code

PPSSPP is downloaded and patched by `bootstrap_windows.ps1`. Do not edit
`third_party\ppsspp-master` as source. Keep Dingoo-specific PPSSPP changes in
`patches\ppsspp-irjit-dingoo.patch`, then rebuild and run the smoke/profile
commands from `docs\DEBUGGING.md`.
