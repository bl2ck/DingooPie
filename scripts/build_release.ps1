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

function Copy-ReleaseFile {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Source,

        [Parameter(Mandatory = $true)]
        [string]$Destination,

        [switch]$Optional
    )

    if (!(Test-Path -LiteralPath $Source -PathType Leaf)) {
        if ($Optional) {
            Write-Host "Skipping optional release file: $Source"
            return $false
        }
        throw "Missing release source file: $Source"
    }

    if (Test-Path -LiteralPath $Destination -PathType Leaf) {
        $sourceHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $Source).Hash
        $destinationHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $Destination).Hash
        if ($sourceHash -eq $destinationHash) {
            return $true
        }
    }

    try {
        Copy-Item -LiteralPath $Source -Destination $Destination -Force -ErrorAction Stop
    }
    catch {
        throw "Failed to copy release file from $Source to $Destination`: $($_.Exception.Message)"
    }
    return $true
}

function Copy-ReleaseTree {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Source,

        [Parameter(Mandatory = $true)]
        [string]$Destination
    )

    if (!(Test-Path -LiteralPath $Source -PathType Container)) {
        return
    }

    if (Test-Path -LiteralPath $Destination) {
        Remove-Item -LiteralPath $Destination -Recurse -Force
    }
    Copy-Item -LiteralPath $Source -Destination $Destination -Recurse -Force
}

function Resolve-RequiredFile {
    param(
        [Parameter(Mandatory = $true)]
        [string]$FileName,

        [Parameter(Mandatory = $true)]
        [string[]]$CandidateDirectories
    )

    foreach ($directory in $CandidateDirectories) {
        if (!$directory) {
            continue
        }
        $candidate = Join-Path $directory $FileName
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    $searched = ($CandidateDirectories | Where-Object { $_ } | ForEach-Object { Join-Path $_ $FileName }) -join ', '
    throw "Missing required release file ${FileName}. Searched: $searched. Run scripts\bootstrap_windows.ps1 first."
}

function Resolve-ReleaseRuntimeDlls {
    @{
        SDL2 = Join-Path $ProjectRoot 'deps_extract\SDL2\SDL2-2.26.5\x86_64-w64-mingw32\bin\SDL2.dll'
        Capstone = Join-Path $ProjectRoot 'deps_extract\capstone\mingw64\bin\libcapstone.dll'
        Winpthread = Resolve-RequiredFile -FileName 'libwinpthread-1.dll' -CandidateDirectories @(
            $W64Bin,
            (Join-Path $ProjectRoot 'deps_extract\winpthread\mingw64\bin')
        )
    }
}

function New-DingooPieRelease {
    New-Item -ItemType Directory -Path $ReleaseDir -Force | Out-Null

    $releaseFiles = New-Object System.Collections.Generic.List[string]
    $runtimeDlls = Resolve-ReleaseRuntimeDlls
    $cheatSourceDir = Join-Path $ProjectRoot 'cheats'
    $includeCheats = (Test-Path -LiteralPath $cheatSourceDir -PathType Container) -and
        ($null -ne (Get-ChildItem -LiteralPath $cheatSourceDir -Filter '*.cht' -File -ErrorAction SilentlyContinue | Select-Object -First 1))

    if (Copy-ReleaseFile -Source (Join-Path $BuildDir 'DingooPie.exe') -Destination (Join-Path $ReleaseDir 'DingooPie.exe')) {
        $releaseFiles.Add('DingooPie.exe')
    }
    if (Copy-ReleaseFile -Source $runtimeDlls.SDL2 -Destination (Join-Path $ReleaseDir 'SDL2.dll')) {
        $releaseFiles.Add('SDL2.dll')
    }
    if (Copy-ReleaseFile -Source $runtimeDlls.Capstone -Destination (Join-Path $ReleaseDir 'libcapstone.dll')) {
        $releaseFiles.Add('libcapstone.dll')
    }
    if (Copy-ReleaseFile -Source $runtimeDlls.Winpthread -Destination (Join-Path $ReleaseDir 'libwinpthread-1.dll')) {
        $releaseFiles.Add('libwinpthread-1.dll')
    }

    $readmeSource = Join-Path $ProjectRoot 'RELEASE_README.md'
    if (!(Test-Path -LiteralPath $readmeSource -PathType Leaf)) {
        $readmeSource = Join-Path $ProjectRoot 'README.md'
    }
    if (Copy-ReleaseFile -Source $readmeSource -Destination (Join-Path $ReleaseDir 'README.md')) {
        $releaseFiles.Add('README.md')
    }
    if ($includeCheats) {
        Copy-ReleaseTree -Source $cheatSourceDir -Destination (Join-Path $ReleaseDir 'cheats')
    }

    $ManifestPath = Join-Path $ReleaseDir 'manifest.sha256'
    $HashLines = foreach ($fileName in $releaseFiles) {
        $path = Join-Path $ReleaseDir $fileName
        if (!(Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Missing release file: $path"
        }
        $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash.ToLowerInvariant()
        "$hash  $fileName"
    }
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllLines($ManifestPath, [string[]]$HashLines, $utf8NoBom)

    $keepFiles = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($fileName in $releaseFiles) {
        [void]$keepFiles.Add($fileName)
    }
    [void]$keepFiles.Add('manifest.sha256')
    Get-ChildItem -LiteralPath $ReleaseDir -File -ErrorAction SilentlyContinue |
        Where-Object { -not $keepFiles.Contains($_.Name) } |
        Remove-Item -Force
    Get-ChildItem -LiteralPath $ReleaseDir -Directory -ErrorAction SilentlyContinue |
        Where-Object { $includeCheats -eq $false -or $_.Name -ne 'cheats' } |
        Remove-Item -Recurse -Force

    Write-Host "Release written to $ReleaseDir"
    Write-Host "Release manifest written to $ManifestPath"
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

New-DingooPieRelease
