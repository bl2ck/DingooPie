param(
    [Parameter(Mandatory = $true)]
    [string]$SampleDir,

    [Parameter(Mandatory = $true)]
    [string]$BuildDir,

    [string]$OutputDir = "",
    [int]$BeforeSaveSeconds = 6,
    [int]$AfterSaveSeconds = 4,
    [int]$AfterLoadSeconds = 3,
    [ValidateSet("ppsspp_irjit", "interpreter")]
    [string]$Backend = "ppsspp_irjit",
    [string]$AutoPressSequence = "A@1200:120,A@2400:120,Start@3600:120"
)

$ErrorActionPreference = "Stop"

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectRoot = Split-Path -Parent $scriptRoot
$SampleDir = (Resolve-Path -LiteralPath $SampleDir).Path
$BuildDir = (Resolve-Path -LiteralPath $BuildDir).Path
if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = Join-Path $projectRoot "test_artifacts\savestate-samples"
}
New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
$OutputDir = (Resolve-Path -LiteralPath $OutputDir).Path

$exe = Join-Path $BuildDir "DingooPie.exe"
if (!(Test-Path -LiteralPath $exe -PathType Leaf)) {
    throw "Missing emulator executable: $exe"
}

function ConvertTo-SafeName($Name) {
    $safe = $Name -replace '[^A-Za-z0-9_.-]', '_'
    $safe = $safe.Trim('_')
    if ([string]::IsNullOrWhiteSpace($safe)) {
        return "sample"
    }
    return $safe
}

function Read-LogLines($Path) {
    if (Test-Path -LiteralPath $Path -PathType Leaf) {
        return @(Get-Content -LiteralPath $Path)
    }
    return @()
}

function Get-FrameDumps($Lines) {
    foreach ($line in $Lines) {
        if ($line -match '^framebuffer: dumped frame (\d+) to (.+)$') {
            [pscustomobject]@{
                frame = [int]$matches[1]
                path = $matches[2]
            }
        }
    }
}

function Get-FirstLineIndex($Lines, $Pattern) {
    for ($i = 0; $i -lt $Lines.Count; ++$i) {
        if ($Lines[$i] -match $Pattern) {
            return $i
        }
    }
    return -1
}

function Measure-BmpDiff($PathA, $PathB) {
    if (!(Test-Path -LiteralPath $PathA -PathType Leaf) -or
        !(Test-Path -LiteralPath $PathB -PathType Leaf)) {
        return $null
    }

    $a = [System.IO.File]::ReadAllBytes($PathA)
    $b = [System.IO.File]::ReadAllBytes($PathB)
    if ($a.Length -ne $b.Length) {
        return $null
    }

    $begin = 66
    if ($a.Length -le $begin) {
        $begin = 0
    }
    [int64]$sum = 0
    $count = $a.Length - $begin
    for ($i = $begin; $i -lt $a.Length; ++$i) {
        $sum += [math]::Abs([int]$a[$i] - [int]$b[$i])
    }
    return [math]::Round($sum / [double]$count, 4)
}

function Select-FrameNearLine($Frames, $Lines, [int]$LineIndex, [string]$Direction) {
    if ($LineIndex -lt 0 -or !$Frames -or $Frames.Count -eq 0) {
        return $null
    }

    $candidates = @()
    foreach ($frame in $Frames) {
        $frameLine = Get-FirstLineIndex $Lines ("^framebuffer: dumped frame $($frame.frame) ")
        if ($frameLine -lt 0) {
            continue
        }
        if ($Direction -eq "before" -and $frameLine -lt $LineIndex) {
            $candidates += [pscustomobject]@{ frame = $frame; distance = $LineIndex - $frameLine }
        } elseif ($Direction -eq "after" -and $frameLine -gt $LineIndex) {
            $candidates += [pscustomobject]@{ frame = $frame; distance = $frameLine - $LineIndex }
        }
    }

    $selected = $candidates | Sort-Object distance | Select-Object -First 1
    if ($selected) {
        return $selected.frame
    }
    return $null
}

function Copy-RuntimeFiles($BuildDir, $RunDir) {
    foreach ($name in @("DingooPie.exe", "SDL2.dll", "libwinpthread-1.dll", "libcapstone.dll")) {
        $source = Join-Path $BuildDir $name
        if (Test-Path -LiteralPath $source -PathType Leaf) {
            Copy-Item -LiteralPath $source -Destination (Join-Path $RunDir $name) -Force
        }
    }
}

function Set-EnvValue($Name, $Value) {
    if ($null -eq $Value) {
        Remove-Item "Env:$Name" -ErrorAction SilentlyContinue
    } else {
        Set-Item "Env:$Name" $Value
    }
}

function Stop-RunDingooPieProcesses($OutputDir) {
    $errors = @()
    $root = ""
    try {
        $root = (Resolve-Path -LiteralPath $OutputDir).Path
    } catch {
        return @("cannot resolve output directory: $($_.Exception.Message)")
    }

    $processes = @(Get-Process DingooPie,dingoo-pie -ErrorAction SilentlyContinue)
    foreach ($p in $processes) {
        $path = ""
        try {
            $path = $p.Path
        } catch {
            continue
        }
        if ([string]::IsNullOrWhiteSpace($path) -or
            !$path.StartsWith($root, [System.StringComparison]::OrdinalIgnoreCase)) {
            continue
        }

        try {
            Stop-Process -Id $p.Id -Force -ErrorAction Stop
            Wait-Process -Id $p.Id -Timeout 5 -ErrorAction SilentlyContinue
        } catch {
            $errors += $_.Exception.Message
        }
    }
    return $errors
}

$apps = Get-ChildItem -LiteralPath $SampleDir -Filter "*.app" -File | Sort-Object Name
if ($apps.Count -eq 0) {
    throw "No .app samples found under $SampleDir"
}

$runStamp = Get-Date -Format "yyyyMMdd-HHmmss"
$results = @()

for ($i = 0; $i -lt $apps.Count; ++$i) {
    $app = $apps[$i]
    $baseName = ConvertTo-SafeName ([System.IO.Path]::GetFileNameWithoutExtension($app.Name))
    if ($baseName -eq "sample") {
        $baseName = "sample{0:D2}" -f ($i + 1)
    }

    $runName = "$runStamp-$baseName"
    $runDir = Join-Path $OutputDir $runName
    New-Item -ItemType Directory -Path $runDir -Force | Out-Null
    Copy-RuntimeFiles $BuildDir $runDir
    $runApp = Join-Path $runDir $app.Name
    Copy-Item -LiteralPath $app.FullName -Destination $runApp -Force

    $ini = Join-Path $runDir "DingooPie.ini"
    @"
[debug]
show_console=0
profile=1

[runtime]
backend=$Backend
"@ | Set-Content -LiteralPath $ini -Encoding ASCII

    $rawLog = Join-Path $runDir "DingooPie-debug.log"
    $framePattern = Join-Path $runDir "frame-%u.bmp"
    $previousEnv = @{
        DINGOO_PIE_LOG_FILE = $env:DINGOO_PIE_LOG_FILE
        DINGOO_PIE_PROFILE = $env:DINGOO_PIE_PROFILE
        DINGOO_PIE_AUTOPRESS_SEQUENCE = $env:DINGOO_PIE_AUTOPRESS_SEQUENCE
        DINGOO_PIE_AUTOTEST_SAVE_STATE_MS = $env:DINGOO_PIE_AUTOTEST_SAVE_STATE_MS
        DINGOO_PIE_AUTOTEST_LOAD_STATE_MS = $env:DINGOO_PIE_AUTOTEST_LOAD_STATE_MS
        DINGOO_PIE_DUMP_FRAME_BMP = $env:DINGOO_PIE_DUMP_FRAME_BMP
        DINGOO_PIE_DUMP_FRAME_START = $env:DINGOO_PIE_DUMP_FRAME_START
        DINGOO_PIE_DUMP_FRAME_END = $env:DINGOO_PIE_DUMP_FRAME_END
        DINGOO_PIE_DUMP_FRAME_STEP = $env:DINGOO_PIE_DUMP_FRAME_STEP
    }

    $errorMessage = ""
    $processAliveAfterLoad = $false
    $processAliveAfterStability = $false
    $exitCode = $null
    $totalSeconds = $BeforeSaveSeconds + $AfterSaveSeconds + $AfterLoadSeconds

    try {
        $cleanupErrors = Stop-RunDingooPieProcesses $OutputDir
        if ($cleanupErrors.Count -gt 0) {
            $errorMessage = ($cleanupErrors -join "; ")
        }
        Remove-Item -LiteralPath $rawLog -ErrorAction SilentlyContinue
        $env:DINGOO_PIE_LOG_FILE = "1"
        $env:DINGOO_PIE_PROFILE = "1"
        $env:DINGOO_PIE_AUTOPRESS_SEQUENCE = $AutoPressSequence
        $env:DINGOO_PIE_AUTOTEST_SAVE_STATE_MS = [string]($BeforeSaveSeconds * 1000)
        $env:DINGOO_PIE_AUTOTEST_LOAD_STATE_MS = [string](($BeforeSaveSeconds + $AfterSaveSeconds) * 1000)
        $env:DINGOO_PIE_DUMP_FRAME_BMP = $framePattern
        $env:DINGOO_PIE_DUMP_FRAME_START = "1"
        $env:DINGOO_PIE_DUMP_FRAME_END = "900"
        $env:DINGOO_PIE_DUMP_FRAME_STEP = "20"

        $process = Start-Process -FilePath (Join-Path $runDir "DingooPie.exe") `
            -ArgumentList @($runApp) `
            -WorkingDirectory $runDir `
            -PassThru

        Start-Sleep -Seconds ($BeforeSaveSeconds + $AfterSaveSeconds)
        Start-Sleep -Milliseconds 800
        $process.Refresh()
        $processAliveAfterLoad = -not $process.HasExited

        Start-Sleep -Seconds $AfterLoadSeconds
        $process.Refresh()
        $processAliveAfterStability = -not $process.HasExited
        if (!$process.HasExited) {
            try {
                Stop-Process -Id $process.Id -Force -ErrorAction Stop
                Wait-Process -Id $process.Id -Timeout 5 -ErrorAction SilentlyContinue
            } catch {
                $errorMessage = (@($errorMessage, $_.Exception.Message) | Where-Object { $_ }) -join "; "
            }
        }
        $process.Refresh()
        $exitCode = if ($process.HasExited) { $process.ExitCode } else { $null }
    } catch {
        $errorMessage = $_.Exception.Message
        if ($process -and !$process.HasExited) {
            try {
                Stop-Process -Id $process.Id -Force -ErrorAction Stop
                Wait-Process -Id $process.Id -Timeout 5 -ErrorAction SilentlyContinue
            } catch {
                $errorMessage = (@($errorMessage, $_.Exception.Message) | Where-Object { $_ }) -join "; "
            }
        }
    } finally {
        foreach ($key in $previousEnv.Keys) {
            Set-EnvValue $key $previousEnv[$key]
        }
    }

    $lines = Read-LogLines $rawLog
    $frames = @(Get-FrameDumps $lines)
    $saveLine = Get-FirstLineIndex $lines '^frontend: save state slot 1 saved'
    $loadLine = Get-FirstLineIndex $lines '^frontend: load state slot 1 loaded'
    $saveFrame = Select-FrameNearLine $frames $lines $saveLine "before"
    $beforeLoadFrame = Select-FrameNearLine $frames $lines $loadLine "before"
    $afterLoadFrame = Select-FrameNearLine $frames $lines $loadLine "after"

    $saveRestoredDiff = if ($saveFrame -and $afterLoadFrame) { Measure-BmpDiff $saveFrame.path $afterLoadFrame.path } else { $null }
    $changedRestoredDiff = if ($beforeLoadFrame -and $afterLoadFrame) { Measure-BmpDiff $beforeLoadFrame.path $afterLoadFrame.path } else { $null }
    $saveChangedDiff = if ($saveFrame -and $beforeLoadFrame) { Measure-BmpDiff $saveFrame.path $beforeLoadFrame.path } else { $null }

    $saveStateFiles = @()
    $saveDir = Join-Path $runDir "savestates"
    if (Test-Path -LiteralPath $saveDir -PathType Container) {
        $saveStateFiles = @(Get-ChildItem -LiteralPath $saveDir -Filter "*.dps" -File | ForEach-Object {
            [pscustomobject]@{ name = $_.Name; length = $_.Length }
        })
    }

    $logHasInvalidMemory = [bool]($lines | Select-String -Pattern 'invalid memory|RuntimeError|RUNTIME_ERROR' -Quiet)
    $logHasGuestException = [bool]($lines | Select-String -Pattern 'Guest exception|guest exception|native runtime returned err=.*exception' -Quiet)
    $logHasNoMemory = [bool]($lines | Select-String -Pattern 'no memory|out of memory|malloc failed' -Quiet)
    $logHasAbort = [bool]($lines | Select-String -Pattern 'abort|Assertion|assert' -Quiet)
    $logHasFailure = [bool]($lines | Select-String -Pattern 'save state slot 1 failed|load state slot 1 failed|failed to restore|failed to write save-state|failed to finalize save-state' -Quiet)

    $status = "pass"
    $notes = @()
    if ($errorMessage) { $status = "fail"; $notes += $errorMessage }
    if ($saveLine -lt 0) { $status = "fail"; $notes += "save log missing" }
    if ($loadLine -lt 0) { $status = "fail"; $notes += "load log missing" }
    if (!$processAliveAfterLoad -or !$processAliveAfterStability) { $status = "fail"; $notes += "process not alive after load" }
    if ($logHasInvalidMemory -or $logHasGuestException -or $logHasNoMemory -or $logHasAbort -or $logHasFailure) {
        $status = "fail"
        $notes += "error pattern in log"
    }
    if ($saveStateFiles.Count -eq 0) { $status = "fail"; $notes += "save-state file missing" }

    $results += [pscustomobject]@{
        sample = $app.Name
        status = $status
        notes = ($notes -join "; ")
        run_dir = $runDir
        log = $rawLog
        save_logged = [bool]($saveLine -ge 0)
        load_logged = [bool]($loadLine -ge 0)
        process_alive_after_load = $processAliveAfterLoad
        process_alive_after_stability = $processAliveAfterStability
        exit_code = $exitCode
        total_seconds = $totalSeconds
        frame_count = $frames.Count
        save_frame = if ($saveFrame) { $saveFrame.frame } else { $null }
        before_load_frame = if ($beforeLoadFrame) { $beforeLoadFrame.frame } else { $null }
        after_load_frame = if ($afterLoadFrame) { $afterLoadFrame.frame } else { $null }
        save_changed_diff = $saveChangedDiff
        save_restored_diff = $saveRestoredDiff
        changed_restored_diff = $changedRestoredDiff
        log_has_invalid_memory = $logHasInvalidMemory
        log_has_guest_exception = $logHasGuestException
        log_has_no_memory = $logHasNoMemory
        log_has_abort = $logHasAbort
        log_has_failure = $logHasFailure
        save_state_files = $saveStateFiles
    }
}

$csvPath = Join-Path $OutputDir "$runStamp-savestate-summary.csv"
$jsonPath = Join-Path $OutputDir "$runStamp-savestate-summary.json"
$markdownPath = Join-Path $OutputDir "$runStamp-savestate-summary.md"

$results | Export-Csv -LiteralPath $csvPath -NoTypeInformation -Encoding UTF8
$results | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $jsonPath -Encoding UTF8

$lines = @()
$lines += "# Dingoo Sample Save-State Regression"
$lines += ""
$lines += "- Date: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')"
$lines += "- Sample directory: ``$SampleDir``"
$lines += "- Build directory: ``$BuildDir``"
$lines += "- Backend: ``$Backend``"
$lines += ""
$lines += "| Sample | Status | Save | Load | Alive | Save/Restored Diff | Notes |"
$lines += "|---|---|---:|---:|---:|---:|---|"
foreach ($row in $results) {
    $aliveText = if ($row.process_alive_after_load -and $row.process_alive_after_stability) { "yes" } else { "no" }
    $diffText = if ($null -ne $row.save_restored_diff) { [string]$row.save_restored_diff } else { "-" }
    $notesText = if ($row.notes) { $row.notes.Replace("|", "/") } else { "" }
    $lines += "| $($row.sample) | $($row.status) | $($row.save_logged) | $($row.load_logged) | $aliveText | $diffText | $notesText |"
}
$lines | Set-Content -LiteralPath $markdownPath -Encoding UTF8

[pscustomobject]@{
    samples = $apps.Count
    results = $results.Count
    pass = @($results | Where-Object { $_.status -eq "pass" }).Count
    fail = @($results | Where-Object { $_.status -ne "pass" }).Count
    csv = $csvPath
    json = $jsonPath
    markdown = $markdownPath
} | ConvertTo-Json
