param(
    [string]$PackageName = 'DingooPie-win64-source-release'
)

$ErrorActionPreference = 'Stop'

$ProjectRoot = Resolve-Path (Join-Path $PSScriptRoot '..')
$DistDir = Join-Path $ProjectRoot 'dist'
$StageRoot = Join-Path $DistDir $PackageName
$ArchivePath = Join-Path $DistDir "$PackageName.zip"
$VerifyRoot = Join-Path $DistDir '_verify'

$ExcludedFilePatterns = @(
    '*.app',
    '*.apk',
    '*.bmp',
    '*.err',
    '*.log',
    'codex_*.png',
    'dingoo-screenshot-*.png'
)

function Copy-Tree($Source, $Destination) {
    if (!(Test-Path -LiteralPath $Source)) {
        throw "Missing required path: $Source"
    }
    New-Item -ItemType Directory -Path (Split-Path -Parent $Destination) -Force | Out-Null
    Copy-Item -LiteralPath $Source -Destination $Destination -Recurse -Force
}

function Copy-File($Source, $Destination) {
    if (!(Test-Path -LiteralPath $Source -PathType Leaf)) {
        throw "Missing required file: $Source"
    }
    New-Item -ItemType Directory -Path (Split-Path -Parent $Destination) -Force | Out-Null
    Copy-Item -LiteralPath $Source -Destination $Destination -Force
}

function Require-File($Path) {
    if (!(Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Missing required file: $Path"
    }
}

function Write-Manifest($Root) {
    $manifestFiles = Join-Path $Root 'manifest.files.txt'
    $manifestHash = Join-Path $Root 'manifest.sha256'
    $files = Get-ChildItem -LiteralPath $Root -Recurse -File |
        Where-Object { $_.FullName -notlike "*manifest.files.txt" -and $_.FullName -notlike "*manifest.sha256" } |
        Sort-Object FullName

    $relativeFiles = foreach ($file in $files) {
        $file.FullName.Substring($Root.Length + 1).Replace('\', '/')
    }
    Set-Content -LiteralPath $manifestFiles -Value $relativeFiles -Encoding ASCII

    $hashLines = foreach ($file in $files) {
        $relative = $file.FullName.Substring($Root.Length + 1).Replace('\', '/')
        $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $file.FullName).Hash.ToLowerInvariant()
        "$hash  $relative"
    }
    Set-Content -LiteralPath $manifestHash -Value $hashLines -Encoding ASCII
}

function Assert-NoForbiddenFiles($Root) {
    foreach ($pattern in $ExcludedFilePatterns) {
        $matches = Get-ChildItem -LiteralPath $Root -Recurse -File -Filter $pattern -ErrorAction SilentlyContinue
        if ($matches.Count -gt 0) {
            throw "Package contains forbidden files matching ${pattern}: $($matches.FullName -join ', ')"
        }
    }
}

function Test-Manifest($Root) {
    $manifestHash = Join-Path $Root 'manifest.sha256'
    Require-File $manifestHash
    foreach ($line in Get-Content -LiteralPath $manifestHash) {
        if (!$line.Trim()) {
            continue
        }
        $parts = $line -split '  ', 2
        if ($parts.Count -ne 2) {
            throw "Bad manifest line: $line"
        }
        $expected = $parts[0]
        $relative = $parts[1].Replace('/', '\')
        $path = Join-Path $Root $relative
        Require-File $path
        $actual = (Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash.ToLowerInvariant()
        if ($actual -ne $expected) {
            throw "Hash mismatch for $relative"
        }
    }
}

if (Test-Path -LiteralPath $StageRoot) {
    Remove-Item -LiteralPath $StageRoot -Recurse -Force
}
if (Test-Path -LiteralPath $ArchivePath) {
    Remove-Item -LiteralPath $ArchivePath -Force
}
New-Item -ItemType Directory -Path $StageRoot -Force | Out-Null

Copy-Tree (Join-Path $ProjectRoot 'dingoo_pie') (Join-Path $StageRoot 'dingoo_pie')
Copy-Tree (Join-Path $ProjectRoot 'docs') (Join-Path $StageRoot 'docs')
Copy-Tree (Join-Path $ProjectRoot 'patches') (Join-Path $StageRoot 'patches')
Copy-Tree (Join-Path $ProjectRoot 'resources') (Join-Path $StageRoot 'resources')
Copy-Tree (Join-Path $ProjectRoot 'scripts') (Join-Path $StageRoot 'scripts')
New-Item -ItemType Directory -Path (Join-Path $StageRoot 'release') -Force | Out-Null
Copy-File (Join-Path $ProjectRoot 'release\DingooPie.exe') (Join-Path $StageRoot 'release\DingooPie.exe')
Copy-File (Join-Path $ProjectRoot 'release\SDL2.dll') (Join-Path $StageRoot 'release\SDL2.dll')
Copy-File (Join-Path $ProjectRoot 'release\libcapstone.dll') (Join-Path $StageRoot 'release\libcapstone.dll')
Copy-File (Join-Path $ProjectRoot 'release\libwinpthread-1.dll') (Join-Path $StageRoot 'release\libwinpthread-1.dll')
Copy-File (Join-Path $ProjectRoot 'release\README.md') (Join-Path $StageRoot 'release\README.md')
if (Test-Path -LiteralPath (Join-Path $ProjectRoot 'release\manifest.sha256')) {
    Copy-File (Join-Path $ProjectRoot 'release\manifest.sha256') (Join-Path $StageRoot 'release\manifest.sha256')
}

Copy-Item -LiteralPath (Join-Path $ProjectRoot 'CMakeLists.txt') -Destination $StageRoot -Force
Copy-Item -LiteralPath (Join-Path $ProjectRoot 'README.md') -Destination $StageRoot -Force
Copy-Item -LiteralPath (Join-Path $ProjectRoot 'RELEASE_README.md') -Destination $StageRoot -Force
Copy-Item -LiteralPath (Join-Path $ProjectRoot 'THIRD_PARTY.md') -Destination $StageRoot -Force
Copy-Item -LiteralPath (Join-Path $ProjectRoot '.gitignore') -Destination $StageRoot -Force
Copy-Item -LiteralPath (Join-Path $ProjectRoot 'configure_win64.bat') -Destination $StageRoot -Force

Assert-NoForbiddenFiles $StageRoot

$required = @(
    'CMakeLists.txt',
    'README.md',
    'THIRD_PARTY.md',
    'dingoo_pie\main.cpp',
    'dingoo_pie\compat_profile.cpp',
    'dingoo_pie\sdk_hle.cpp',
    'dingoo_pie\guest_filesystem.cpp',
    'dingoo_pie\guest_format.cpp',
    'dingoo_pie\guest_audio.cpp',
    'dingoo_pie\instruction_compat.cpp',
    'dingoo_pie\runtime_debug.cpp',
    'dingoo_pie\platform_win32.cpp',
    'dingoo_pie\app_icon.rc',
    'dingoo_pie\resource_ids.h',
    'dingoo_pie\ui_strings.cpp',
    'dingoo_pie\ui_strings.h',
    'dingoo_pie\ppsspp_shim.cpp',
    'docs\ARCHITECTURE.md',
    'docs\A320_X760_PLUS_3D_BASELINES.md',
    'docs\DEBUGGING.md',
    'scripts\bootstrap_windows.ps1',
    'scripts\build_release.ps1',
    'scripts\profile_sample.ps1',
    'scripts\profile_samples.ps1',
    'scripts\smoke_test.ps1',
    'patches\ppsspp-irjit-dingoo.patch',
    'resources\app_icon.ico',
    'resources\app_icon.png',
    'release\DingooPie.exe',
    'release\SDL2.dll',
    'release\libcapstone.dll',
    'release\libwinpthread-1.dll',
    'release\README.md'
)
foreach ($item in $required) {
    Require-File (Join-Path $StageRoot $item)
}

Write-Manifest $StageRoot
Compress-Archive -LiteralPath $StageRoot -DestinationPath $ArchivePath -Force

if (Test-Path -LiteralPath $VerifyRoot) {
    Remove-Item -LiteralPath $VerifyRoot -Recurse -Force
}
New-Item -ItemType Directory -Path $VerifyRoot -Force | Out-Null
Expand-Archive -LiteralPath $ArchivePath -DestinationPath $VerifyRoot -Force
$VerifyPackageRoot = Join-Path $VerifyRoot $PackageName
Test-Manifest $VerifyPackageRoot
Assert-NoForbiddenFiles $VerifyPackageRoot

Write-Host "Package verified: $ArchivePath"
Write-Host "Manifest: $(Join-Path $StageRoot 'manifest.sha256')"
