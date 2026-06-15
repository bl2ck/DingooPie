param(
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'

$ProjectRoot = Resolve-Path (Join-Path $PSScriptRoot '..')
$BuildDir = Join-Path $ProjectRoot 'build\win64'
$ReleaseDir = Join-Path $ProjectRoot 'release'
$W64Bin = Join-Path $ProjectRoot 'w64devkit\bin'
$CMake = Join-Path $W64Bin 'cmake.exe'

if (!(Test-Path -LiteralPath $CMake)) {
    throw "CMake was not found at $CMake. Run scripts\bootstrap_windows.ps1 first."
}

function Invoke-NativeCommand {
    param(
        [Parameter(Mandatory = $true)]
        [string]$FilePath,

        [Parameter(ValueFromRemainingArguments = $true)]
        [string[]]$Arguments
    )

    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code ${LASTEXITCODE}: $FilePath $($Arguments -join ' ')"
    }
}

$env:PATH = "$W64Bin;$env:PATH"
New-Item -ItemType Directory -Path $BuildDir -Force | Out-Null

Write-Host "Configuring DingooPie ($Configuration)"
Invoke-NativeCommand $CMake -S $ProjectRoot -B $BuildDir -G 'MinGW Makefiles' `
    "-DCMAKE_BUILD_TYPE=$Configuration" `
    "-DDINGOO_PIE_DEPS_ROOT=$(Join-Path $ProjectRoot 'deps_extract')" `
    "-DPPSSPP_SOURCE_ROOT=$(Join-Path $ProjectRoot 'third_party\ppsspp-master')" `
    -DDINGOO_PIE_ENABLE_PPSSPP_IRJIT=ON
Write-Host "Building DingooPie ($Configuration)"
Invoke-NativeCommand $CMake --build $BuildDir --config $Configuration -j 4

New-Item -ItemType Directory -Path $ReleaseDir -Force | Out-Null
Remove-Item -LiteralPath (Join-Path $ReleaseDir 'DingooPie.exe') -ErrorAction SilentlyContinue
Remove-Item -LiteralPath (Join-Path $ReleaseDir 'dingoo-pie.exe') -ErrorAction SilentlyContinue
Get-ChildItem -LiteralPath $ReleaseDir -Filter '*.exe' -File -ErrorAction SilentlyContinue | Remove-Item -Force
Get-ChildItem -LiteralPath $ReleaseDir -Filter '*-debug.log' -File -ErrorAction SilentlyContinue | Remove-Item -Force
Get-ChildItem -LiteralPath $ReleaseDir -Filter 'Dingoo*.ini' -File -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -ne 'DingooPie.ini' } |
    Remove-Item -Force
Copy-Item -LiteralPath (Join-Path $BuildDir 'DingooPie.exe') -Destination (Join-Path $ReleaseDir 'DingooPie.exe') -Force
Copy-Item -LiteralPath (Join-Path $ProjectRoot 'deps_extract\SDL2\SDL2-2.26.5\x86_64-w64-mingw32\bin\SDL2.dll') -Destination (Join-Path $ReleaseDir 'SDL2.dll') -Force
Copy-Item -LiteralPath (Join-Path $ProjectRoot 'deps_extract\capstone\mingw64\bin\libcapstone.dll') -Destination (Join-Path $ReleaseDir 'libcapstone.dll') -Force
Copy-Item -LiteralPath (Join-Path $W64Bin 'libwinpthread-1.dll') -Destination (Join-Path $ReleaseDir 'libwinpthread-1.dll') -Force
Copy-Item -LiteralPath (Join-Path $ProjectRoot 'RELEASE_README.md') -Destination (Join-Path $ReleaseDir 'README.md') -Force

$ReleaseFiles = @(
    'DingooPie.exe',
    'SDL2.dll',
    'libcapstone.dll',
    'libwinpthread-1.dll',
    'README.md'
)
$ManifestPath = Join-Path $ReleaseDir 'manifest.sha256'
$HashLines = foreach ($fileName in $ReleaseFiles) {
    $path = Join-Path $ReleaseDir $fileName
    if (!(Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Missing release file: $path"
    }
    $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash.ToLowerInvariant()
    "$hash  $fileName"
}
Set-Content -LiteralPath $ManifestPath -Value $HashLines -Encoding ASCII

Write-Host "Release written to $ReleaseDir"
Write-Host "Release manifest written to $ManifestPath"
