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
    PPSSPP = @{
        File = 'ppsspp-master.zip'
        Url = 'https://github.com/hrydgard/ppsspp/archive/refs/heads/master.zip'
        Sha256 = ''
    }
}

function New-Directory($Path) {
    if (!(Test-Path -LiteralPath $Path)) {
        New-Item -ItemType Directory -Path $Path | Out-Null
    }
}

function Copy-CacheIfAvailable($Name, $Destination) {
    $workspaceCache = Join-Path $WorkspaceRoot "downloads\$Name"
    if ((Test-Path -LiteralPath $workspaceCache) -and (!(Test-Path -LiteralPath $Destination) -or $Force)) {
        Copy-Item -LiteralPath $workspaceCache -Destination $Destination -Force
    }
}

function Get-Dependency($Spec) {
    $dest = Join-Path $DownloadsDir $Spec.File
    Copy-CacheIfAvailable $Spec.File $dest
    if (!(Test-Path -LiteralPath $dest) -or $Force) {
        Write-Host "Downloading $($Spec.File)"
        Invoke-WebRequest -Uri $Spec.Url -OutFile $dest -UseBasicParsing
    }
    if ($Spec.Sha256) {
        $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $dest).Hash.ToLowerInvariant()
        if ($hash -ne $Spec.Sha256) {
            throw "SHA256 mismatch for $($Spec.File): $hash"
        }
    }
    return $dest
}

function Expand-Zip($Archive, $Destination) {
    if ((Test-Path -LiteralPath $Destination) -and !$Force) {
        $existing = Get-ChildItem -LiteralPath $Destination -Force -ErrorAction SilentlyContinue | Select-Object -First 1
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
            Set-Content -LiteralPath $marker -Value "Verified $(Split-Path -Leaf $PatchFile) on $(Get-Date -Format o)"
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
        git apply --check --ignore-space-change "--directory=$relativeRepo" $PatchFile
        if ($LASTEXITCODE -ne 0) {
            throw "Patch check failed: $PatchFile"
        }
        git apply --ignore-space-change "--directory=$relativeRepo" $PatchFile
        if ($LASTEXITCODE -ne 0) {
            throw "Patch apply failed: $PatchFile"
        }
        if (!(Test-DingooPatchApplied $RepoRoot)) {
            throw "Patch sentinel check failed after applying: $PatchFile"
        }
        Set-Content -LiteralPath $marker -Value "Applied $(Split-Path -Leaf $PatchFile) on $(Get-Date -Format o)"
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
$ppssppArchive = Get-Dependency $Dependencies.PPSSPP
Expand-Zip $sdlArchive (Join-Path $DepsDir 'SDL2')
Expand-Tar $capstoneArchive (Join-Path $DepsDir 'capstone')
Expand-Zip $ppssppArchive $ThirdPartyDir

$ppssppRoot = Join-Path $ThirdPartyDir 'ppsspp-master'
if (!(Test-Path -LiteralPath $ppssppRoot)) {
    $candidate = Get-ChildItem -LiteralPath $ThirdPartyDir -Directory | Where-Object { $_.Name -like 'ppsspp-*' } | Select-Object -First 1
    if (!$candidate) {
        throw 'PPSSPP source directory was not extracted.'
    }
    Rename-Item -LiteralPath $candidate.FullName -NewName 'ppsspp-master'
}

Apply-PatchIfNeeded $ppssppRoot (Join-Path $PatchDir 'ppsspp-irjit-dingoo.patch')

Write-Host 'Bootstrap complete.'
Write-Host "Compiler: $(Join-Path $ProjectRoot 'w64devkit\bin\gcc.exe')"
Write-Host "PPSSPP:   $ppssppRoot"
