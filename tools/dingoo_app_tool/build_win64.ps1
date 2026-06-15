param(
    [string]$DingooPieRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
)

$ErrorActionPreference = "Stop"

$CompilerBin = Join-Path $DingooPieRoot "w64devkit\bin"
if (!(Test-Path -LiteralPath (Join-Path $CompilerBin "cmake.exe"))) {
    throw "Missing bundled CMake. Build DingooPie dependencies first or pass -DingooPieRoot."
}

$env:PATH = "$CompilerBin;$env:PATH"

cmake -S $PSScriptRoot -B (Join-Path $PSScriptRoot "build") -G "MinGW Makefiles"
if ($LASTEXITCODE -ne 0) {
    throw "CMake configure failed."
}

cmake --build (Join-Path $PSScriptRoot "build") -j 4
if ($LASTEXITCODE -ne 0) {
    throw "Build failed."
}

$CliExe = Join-Path $PSScriptRoot "build\dingoo-app-tool.exe"
$GuiExe = Join-Path $PSScriptRoot "build\dingoo-app-tool-gui.exe"
Write-Host "Built: $CliExe"
if (Test-Path -LiteralPath $GuiExe) {
    Write-Host "Built: $GuiExe"
}
