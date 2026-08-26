param(
    [switch]$Force
)

$ErrorActionPreference = 'Stop'

$ProjectRoot = Resolve-Path (Join-Path $PSScriptRoot '..')
$WorkspaceRoot = Resolve-Path (Join-Path $ProjectRoot '..')
$DownloadsDir = Join-Path $ProjectRoot 'downloads'
$ThirdPartyDir = Join-Path $ProjectRoot 'third_party'
$DepsDir = Join-Path $ProjectRoot 'deps_extract'
$PatchDir = Join-Path $ProjectRoot 'patches'
$W64DevkitDir = Join-Path $ProjectRoot 'w64devkit'
$W64Bin = Join-Path $W64DevkitDir 'bin'

$Dependencies = @{
    W64Devkit = @{
        File = 'w64devkit-x64-2.8.0.7z.exe'
        Url = 'https://github.com/skeeto/w64devkit/releases/download/v2.8.0/w64devkit-x64-2.8.0.7z.exe'
        Sha256 = '6252bf34fe2231a55ac7f03d482b36d2c7c58697990551bba508102cfb3f342e'
    }
    SDL2 = @{
        File = 'SDL2-devel-mingw.zip'
        Url = 'https://github.com/libsdl-org/SDL/releases/download/release-2.26.5/SDL2-devel-2.26.5-mingw.zip'
        Sha256 = ''
    }
    Capstone = @{
        File = 'capstone.pkg.tar.zst'
        Url = 'https://repo.msys2.org/mingw/mingw64/mingw-w64-x86_64-capstone-5.0.9-1-any.pkg.tar.zst'
        Sha256 = ''
    }
    Winpthread = @{
        File = 'mingw-w64-x86_64-libwinpthread.pkg.tar.zst'
        Url = 'https://mirror.msys2.org/mingw/mingw64/mingw-w64-x86_64-libwinpthread-14.0.0.r98.g19f5121a2-1-any.pkg.tar.zst'
        Sha256 = '565a7ac155098633ce66096c02c98f274127111758fd25d3f0e5451c4cd21120'
    }
    PPSSPP = @{
        File = 'ppsspp-master.zip'
        Url = 'https://github.com/hrydgard/ppsspp/archive/refs/heads/master.zip'
        Sha256 = ''
    }
    Dynarmic = @{
        File = 'dynarmic-a41c380246d3d9f9874f0f792d234dc0cc17c180.zip'
        Url = 'https://gitlab.com/suyu-emu/dynarmic/-/archive/a41c380246d3d9f9874f0f792d234dc0cc17c180/dynarmic-a41c380246d3d9f9874f0f792d234dc0cc17c180.zip'
        Sha256 = ''
    }
    Boost = @{
        File = 'boost.1.84.0.nupkg'
        Url = 'https://api.nuget.org/v3-flatcontainer/boost/1.84.0/boost.1.84.0.nupkg'
        Sha256 = '557f9d9eae8e3ffb8aac36004ca6ac978c0bdfa28cc8f4dc8a5919f4b8293ea5'
    }
}

function New-Directory($Path) {
    if (!(Test-Path -LiteralPath $Path)) {
        New-Item -ItemType Directory -Path $Path | Out-Null
    }
}

function Copy-CacheIfAvailable($Name, $Destination) {
    $workspaceCache = Join-Path $WorkspaceRoot "downloads\$Name"
    $shouldCopy = (Test-Path -LiteralPath $workspaceCache) -and
        (!(Test-Path -LiteralPath $Destination) -or $Force)
    if ($shouldCopy) {
        Copy-Item -LiteralPath $workspaceCache -Destination $Destination -Force
    }
}

function Get-Dependency($Spec) {
    $dest = Join-Path $DownloadsDir $Spec.File
    Copy-CacheIfAvailable $Spec.File $dest

    # Download into a sibling temporary file first. A failed or interrupted
    # download must not replace a previously verified dependency archive.
    if ((Test-Path -LiteralPath $dest) -and !$Force) {
        if (!$Spec.Sha256) {
            return $dest
        }

        $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $dest).Hash.ToLowerInvariant()
        if ($hash -eq $Spec.Sha256) {
            return $dest
        }

        Write-Host "SHA256 mismatch for $($Spec.File): $hash. Redownloading."
    }

    $tempDest = "$dest.download"
    Remove-Item -LiteralPath $tempDest -Force -ErrorAction SilentlyContinue
    try {
        Write-Host "Downloading $($Spec.File)"
        Invoke-WebRequest -Uri $Spec.Url -OutFile $tempDest -UseBasicParsing

        if ($Spec.Sha256) {
            $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $tempDest).Hash.ToLowerInvariant()
            if ($hash -ne $Spec.Sha256) {
                throw "SHA256 mismatch for $($Spec.File): $hash"
            }
        }

        Move-Item -LiteralPath $tempDest -Destination $dest -Force
    } finally {
        Remove-Item -LiteralPath $tempDest -Force -ErrorAction SilentlyContinue
    }

    return $dest
}

function Expand-Zip($Archive, $Destination) {
    if ((Test-Path -LiteralPath $Destination) -and !$Force) {
        $existing = Get-ChildItem -LiteralPath $Destination -Force `
            -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if ($existing) {
            return
        }
    }
    if (Test-Path -LiteralPath $Destination) {
        Remove-Item -LiteralPath $Destination -Recurse -Force
    }
    New-Directory $Destination
    Expand-Archive -LiteralPath $Archive -DestinationPath $Destination -Force
}

function Expand-Tar($Archive, $Destination) {
    if ((Test-Path -LiteralPath $Destination) -and !$Force) {
        return
    }
    if (Test-Path -LiteralPath $Destination) {
        Remove-Item -LiteralPath $Destination -Recurse -Force
    }
    New-Directory $Destination
    $tarExe = Join-Path $W64Bin 'tar.exe'
    if (!(Test-Path -LiteralPath $tarExe)) {
        $tarExe = 'tar.exe'
    }
    $env:PATH = "$W64Bin;$env:PATH"
    & $tarExe -xf $Archive -C $Destination
}

function Expand-W64Devkit($Archive, $Destination) {
    if ((Test-Path -LiteralPath (Join-Path $Destination 'bin\gcc.exe')) -and !$Force) {
        return
    }
    if (Test-Path -LiteralPath $Destination) {
        Remove-Item -LiteralPath $Destination -Recurse -Force
    }
    New-Directory $Destination
    & $Archive -y "-o$Destination" | Out-Host
    $nested = Join-Path $Destination 'w64devkit'
    if (Test-Path -LiteralPath $nested) {
        Get-ChildItem -LiteralPath $nested -Force | Move-Item -Destination $Destination -Force
        Remove-Item -LiteralPath $nested -Recurse -Force
    }
}

function Install-ZipSourceTree {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Archive,

        [Parameter(Mandatory = $true)]
        [string]$Destination,

        [Parameter(Mandatory = $true)]
        [string]$ExtractDirectory,

        [Parameter(Mandatory = $true)]
        [string]$DirectoryPattern,

        [string]$Sentinel = 'CMakeLists.txt'
    )

    $destinationSentinel = Join-Path $Destination $Sentinel
    if ((Test-Path -LiteralPath $destinationSentinel -PathType Leaf) -and !$Force) {
        return
    }

    if (Test-Path -LiteralPath $Destination) {
        Remove-Item -LiteralPath $Destination -Recurse -Force
    }
    if (Test-Path -LiteralPath $ExtractDirectory) {
        Remove-Item -LiteralPath $ExtractDirectory -Recurse -Force
    }

    try {
        Expand-Zip $Archive $ExtractDirectory
        $candidates = @(
            Get-ChildItem -LiteralPath $ExtractDirectory -Directory |
                Where-Object {
                    $_.Name -like $DirectoryPattern -and
                    (Test-Path -LiteralPath (Join-Path $_.FullName $Sentinel) -PathType Leaf)
                }
        )
        if ($candidates.Count -ne 1) {
            $candidateCount = $candidates.Count
            throw "Expected one source directory matching '$DirectoryPattern' " +
                "in $Archive; found $candidateCount."
        }

        Move-Item -LiteralPath $candidates[0].FullName -Destination $Destination
        if (!(Test-Path -LiteralPath $destinationSentinel -PathType Leaf)) {
            throw "Source tree sentinel was not found after extraction: $destinationSentinel"
        }
    } finally {
        if (Test-Path -LiteralPath $ExtractDirectory) {
            Remove-Item -LiteralPath $ExtractDirectory -Recurse -Force
        }
    }
}

function Test-DingooPatchApplied($RepoRoot) {
    $sentinels = @(
        @{
            File = 'Core\MIPS\x86\X64IRAsm.cpp'
            Pattern = 'ppssppShimRead32'
        },
        @{
            File = 'Core\MIPS\x86\X64IRAsm.cpp'
            Pattern = 'ppssppShimRunCodeHook'
        },
        @{
            File = 'Core\MemMap.h'
            Pattern = 'DINGOO_PIE_DINGOO_MEMORY'
        },
        @{
            File = 'Core\MIPS\x86\X64IRCompSystem.cpp'
            Pattern = 'DINGOO_PIE_DINGOO_MEMORY'
        },
        @{
            File = 'Core\MIPS\x86\X64IRJit.cpp'
            Pattern = 'DINGOO_PIE_IRJIT_TRACE'
        },
        @{
            File = 'Core\MIPS\IR\IRInst.h'
            Pattern = 'MulLow'
        }
    )

    foreach ($sentinel in $sentinels) {
        $path = Join-Path $RepoRoot $sentinel.File
        if (!(Test-Path -LiteralPath $path -PathType Leaf)) {
            return $false
        }
        if (!(Select-String -LiteralPath $path -Pattern $sentinel.Pattern -SimpleMatch -Quiet)) {
            return $false
        }
    }
    return $true
}

function Apply-PatchIfNeeded($RepoRoot, $PatchFile) {
    $marker = Join-Path $RepoRoot '.dingoopie-patch-applied'
    if ((Test-DingooPatchApplied $RepoRoot) -and !$Force) {
        if (!(Test-Path -LiteralPath $marker)) {
            $markerValue = "Verified $(Split-Path -Leaf $PatchFile) " +
                "on $(Get-Date -Format o)"
            Set-Content -LiteralPath $marker -Value $markerValue
        }
        return
    }

    if (Test-Path -LiteralPath $marker) {
        Remove-Item -LiteralPath $marker -Force
    }

    $trimChars = [char[]]@('\', '/')
    $projectFull = (Resolve-Path -LiteralPath $ProjectRoot).Path.TrimEnd($trimChars)
    $repoFull = (Resolve-Path -LiteralPath $RepoRoot).Path.TrimEnd($trimChars)
    if (!$repoFull.StartsWith($projectFull, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Patch target is outside the project root: $RepoRoot"
    }
    $relativeRepo = $repoFull.Substring($projectFull.Length).TrimStart($trimChars).Replace('\', '/')
    Push-Location $ProjectRoot
    try {
        git apply --check --unidiff-zero --ignore-space-change --whitespace=nowarn "--directory=$relativeRepo" $PatchFile
        if ($LASTEXITCODE -ne 0) {
            throw "Patch check failed: $PatchFile"
        }
        git apply --unidiff-zero --ignore-space-change --whitespace=nowarn "--directory=$relativeRepo" $PatchFile
        if ($LASTEXITCODE -ne 0) {
            throw "Patch apply failed: $PatchFile"
        }
        if (!(Test-DingooPatchApplied $RepoRoot)) {
            throw "Patch sentinel check failed after applying: $PatchFile"
        }
        $markerValue = "Applied $(Split-Path -Leaf $PatchFile) " +
            "on $(Get-Date -Format o)"
        Set-Content -LiteralPath $marker -Value $markerValue
    } finally {
        Pop-Location
    }
}

function Apply-PatchWithSentinelIfNeeded(
    $RepoRoot,
    $PatchFile,
    $SentinelFile,
    $SentinelPattern
) {
    $sentinelPath = Join-Path $RepoRoot $SentinelFile
    if ((Test-Path -LiteralPath $sentinelPath -PathType Leaf) -and
        (Select-String -LiteralPath $sentinelPath -Pattern $SentinelPattern `
            -SimpleMatch -Quiet)) {
        return
    }

    $trimChars = [char[]]@('\', '/')
    $projectFull = (Resolve-Path -LiteralPath $ProjectRoot).Path.TrimEnd($trimChars)
    $repoFull = (Resolve-Path -LiteralPath $RepoRoot).Path.TrimEnd($trimChars)
    if (!$repoFull.StartsWith($projectFull, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Patch target is outside the project root: $RepoRoot"
    }
    $relativeRepo = $repoFull.Substring($projectFull.Length).TrimStart($trimChars).Replace('\', '/')
    Push-Location $ProjectRoot
    try {
        git apply --check --unidiff-zero --ignore-space-change --whitespace=nowarn `
            "--directory=$relativeRepo" $PatchFile
        if ($LASTEXITCODE -ne 0) {
            throw "Patch check failed: $PatchFile"
        }
        git apply --unidiff-zero --ignore-space-change --whitespace=nowarn `
            "--directory=$relativeRepo" $PatchFile
        if ($LASTEXITCODE -ne 0) {
            throw "Patch apply failed: $PatchFile"
        }
        if (!(Select-String -LiteralPath $sentinelPath -Pattern $SentinelPattern `
            -SimpleMatch -Quiet)) {
            throw "Patch sentinel check failed after applying: $PatchFile"
        }
    } finally {
        Pop-Location
    }
}

New-Directory $DownloadsDir
New-Directory $ThirdPartyDir
New-Directory $DepsDir

$w64Archive = Get-Dependency $Dependencies.W64Devkit
Expand-W64Devkit $w64Archive $W64DevkitDir
$env:PATH = "$W64Bin;$env:PATH"

$sdlArchive = Get-Dependency $Dependencies.SDL2
$capstoneArchive = Get-Dependency $Dependencies.Capstone
$winpthreadArchive = Get-Dependency $Dependencies.Winpthread
$ppssppArchive = Get-Dependency $Dependencies.PPSSPP
$dynarmicArchive = Get-Dependency $Dependencies.Dynarmic
$boostArchive = Get-Dependency $Dependencies.Boost
Expand-Zip $sdlArchive (Join-Path $DepsDir 'SDL2')
Expand-Tar $capstoneArchive (Join-Path $DepsDir 'capstone')
Expand-Tar $winpthreadArchive (Join-Path $DepsDir 'winpthread')
Expand-Zip $boostArchive (Join-Path $DepsDir 'boost')

$ppssppRoot = Join-Path $ThirdPartyDir 'ppsspp-master'
$ppssppExtract = Join-Path $ThirdPartyDir 'ppsspp-extract'
Install-ZipSourceTree $ppssppArchive $ppssppRoot $ppssppExtract 'ppsspp-*'

$dynarmicRoot = Join-Path $ThirdPartyDir 'dynarmic'
$dynarmicExtract = Join-Path $ThirdPartyDir 'dynarmic-extract'
Install-ZipSourceTree $dynarmicArchive $dynarmicRoot $dynarmicExtract 'dynarmic-*'

Apply-PatchIfNeeded $ppssppRoot (Join-Path $PatchDir 'ppsspp-irjit-dingoo.patch')
Apply-PatchWithSentinelIfNeeded `
    $ppssppRoot `
    (Join-Path $PatchDir 'ppsspp-irjit-vfpu-bounds.patch') `
    'Core\MIPS\IR\IRCompVFPU.cpp' `
    'ApplyVoffset(regs, GetNumVectorElements(N));'

Write-Host 'Bootstrap complete.'
Write-Host "Compiler: $(Join-Path $ProjectRoot 'w64devkit\bin\gcc.exe')"
Write-Host "PPSSPP:   $ppssppRoot"
Write-Host "Dynarmic: $dynarmicRoot"
