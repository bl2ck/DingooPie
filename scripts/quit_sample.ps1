param(
    [Parameter(Mandatory = $true)]
    [string]$AppPath,

    [Parameter(Mandatory = $true)]
    [string]$BuildDir,

    [int]$TimeoutSeconds = 20,
    [string]$Name = "quit-sample",
    [ValidateSet("ppsspp_irjit", "interpreter")]
    [string]$Backend = "ppsspp_irjit",
    [string]$AutoPressSequence = "",
    [string]$CpuHz = "",
    [string]$RuntimeSpeedScale = "",
    [string]$OsTimeDlyScale = "",
    [string]$DisplayFps = "",
    [string]$DumpFramePattern = "",
    [string]$DumpFrameStart = "",
    [string]$DumpFrameEnd = "",
    [string]$DumpFrameStep = "",
    [string]$TitleFrameHash = "",
    [int]$CloseAfterSeconds = 0,
    [switch]$TraceInput,
    [switch]$TraceKbdCallers,
    [switch]$TraceHle,
    [switch]$RequireNaturalExit
)

$ErrorActionPreference = "Stop"

function Set-EnvValue($Name, $Value) {
    if ($null -eq $Value) {
        Remove-Item -Path "env:$Name" -ErrorAction SilentlyContinue
    } else {
        Set-Item -Path "env:$Name" -Value $Value
    }
}

function Wait-ForProcessExit($ProcessId, $TimeoutMilliseconds) {
    $deadline = (Get-Date).AddMilliseconds($TimeoutMilliseconds)
    do {
        $running = Get-Process -Id $ProcessId -ErrorAction SilentlyContinue
        if (!$running) {
            return $true
        }
        Start-Sleep -Milliseconds 100
    } while ((Get-Date) -lt $deadline)
    return [bool](!(Get-Process -Id $ProcessId -ErrorAction SilentlyContinue))
}

function First-LogValue($Lines, $Pattern, $Replacement) {
    $match = $Lines | Select-String -Pattern $Pattern | Select-Object -First 1
    if (!$match) {
        return ""
    }
    return ($match.Line -replace $Replacement, "")
}

function Parse-TaskStops($Lines) {
    $stops = @()
    foreach ($line in $Lines) {
        if ($line -match '^hle: task stop reason=([^ ]+) app_sha256=([0-9A-F]+) pc=0x([0-9a-fA-F]+) ra=0x([0-9a-fA-F]+) a0=0x([0-9a-fA-F]+) main=(\d+) promoted=(\d+)') {
            $stops += [pscustomobject]@{
                reason = $matches[1]
                app_hash = $matches[2]
                pc = "0x$($matches[3].ToLowerInvariant())"
                ra = "0x$($matches[4].ToLowerInvariant())"
                a0 = "0x$($matches[5].ToLowerInvariant())"
                main = [bool]([int]$matches[6])
                promoted = [bool]([int]$matches[7])
            }
        }
    }
    return @($stops)
}

function Parse-GuestExitReasons($Lines) {
    $reasons = @()
    foreach ($line in $Lines) {
        if ($line -match '^hle: guest exit requested by ([^ ]+) ra=0x([0-9a-fA-F]+)') {
            $reasons += [pscustomobject]@{
                reason = $matches[1]
                ra = "0x$($matches[2].ToLowerInvariant())"
            }
        }
    }
    return @($reasons)
}

function Get-ImageAverageHash($Path) {
    Add-Type -AssemblyName System.Drawing
    $image = [System.Drawing.Image]::FromFile($Path)
    $thumb = New-Object System.Drawing.Bitmap 8, 8
    $graphics = [System.Drawing.Graphics]::FromImage($thumb)
    try {
        $graphics.DrawImage($image, 0, 0, 8, 8)
        $values = @()
        $sum = 0
        for ($y = 0; $y -lt 8; ++$y) {
            for ($x = 0; $x -lt 8; ++$x) {
                $pixel = $thumb.GetPixel($x, $y)
                $luma = [int](($pixel.R * 299 + $pixel.G * 587 + $pixel.B * 114) / 1000)
                $values += $luma
                $sum += $luma
            }
        }
        $avg = $sum / 64.0
        $hash = [uint64]0
        for ($i = 0; $i -lt 64; ++$i) {
            if ($values[$i] -ge $avg) {
                $hash = $hash -bor ([uint64]1 -shl $i)
            }
        }
        return ("{0:x16}" -f $hash)
    } finally {
        $graphics.Dispose()
        $thumb.Dispose()
        $image.Dispose()
    }
}

function Parse-DumpedFrames($Lines) {
    $frames = @()
    foreach ($line in $Lines) {
        if ($line -match '^framebuffer: dumped frame (\d+) to (.+)$') {
            $path = $matches[2]
            $hash = ""
            if (Test-Path -LiteralPath $path -PathType Leaf) {
                try {
                    $hash = Get-ImageAverageHash $path
                } catch {
                    $hash = ""
                }
            }
            $frames += [pscustomobject]@{
                frame = [int]$matches[1]
                path = $path
                ahash = $hash
            }
        }
    }
    return @($frames)
}

$BuildDir = (Resolve-Path -LiteralPath $BuildDir).Path
$AppPath = (Resolve-Path -LiteralPath $AppPath).Path
$exe = Join-Path $BuildDir "DingooPie.exe"
if (!(Test-Path -LiteralPath $exe -PathType Leaf)) {
    throw "Missing emulator executable: $exe"
}
if (!(Test-Path -LiteralPath $AppPath -PathType Leaf)) {
    throw "Missing app file: $AppPath"
}

$safeName = $Name -replace '[^A-Za-z0-9_.-]', '_'
$savedLog = Join-Path $BuildDir "$safeName.quit.log"
$resultJson = Join-Path $BuildDir "$safeName.quit.json"
$resolvedDumpFramePattern = $DumpFramePattern
if (![string]::IsNullOrWhiteSpace($resolvedDumpFramePattern) -and
    ![System.IO.Path]::IsPathRooted($resolvedDumpFramePattern)) {
    $resolvedDumpFramePattern = Join-Path $BuildDir $resolvedDumpFramePattern
}
$ini = Join-Path $BuildDir "DingooPie.ini"
$backup = Join-Path $BuildDir "DingooPie.ini.quit-bak"
$lockPath = Join-Path $BuildDir ".quit-sample.lock"
$lockStream = $null

$lockDeadline = (Get-Date).AddSeconds(30)
do {
    try {
        $lockStream = [System.IO.File]::Open($lockPath,
            [System.IO.FileMode]::OpenOrCreate,
            [System.IO.FileAccess]::ReadWrite,
            [System.IO.FileShare]::None)
    } catch {
        if ((Get-Date) -ge $lockDeadline) {
            throw "Timed out waiting for quit_sample lock: $lockPath"
        }
        Start-Sleep -Milliseconds 200
    }
} while (!$lockStream)

Get-Process DingooPie,dingoo-pie -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 250
Get-ChildItem -LiteralPath $BuildDir -Filter "DingooPie-debug-*.log" -File -ErrorAction SilentlyContinue |
    Remove-Item -Force
Remove-Item -LiteralPath $savedLog, $resultJson -ErrorAction SilentlyContinue

$hadConfig = Test-Path -LiteralPath $ini
Remove-Item -LiteralPath $backup -ErrorAction SilentlyContinue
if ($hadConfig) {
    Move-Item -LiteralPath $ini -Destination $backup -Force
}

@"
[debug]
show_console=0
profile=1
resource_monitor_auto_open=0

[video]
show_fps=0

[runtime]
backend=$Backend
cpu_hz=$CpuHz
speed_scale=$RuntimeSpeedScale
ostimedly_scale=$OsTimeDlyScale
"@ | Set-Content -LiteralPath $ini -Encoding ASCII

$previousEnv = @{
    DINGOO_PIE_LOG_FILE = $env:DINGOO_PIE_LOG_FILE
    DINGOO_PIE_PROFILE = $env:DINGOO_PIE_PROFILE
    DINGOO_PIE_TRACE_TASKS = $env:DINGOO_PIE_TRACE_TASKS
    DINGOO_PIE_TRACE_HLE = $env:DINGOO_PIE_TRACE_HLE
    DINGOO_PIE_INPUT_TRACE = $env:DINGOO_PIE_INPUT_TRACE
    DINGOO_PIE_TRACE_KBD_CALLERS = $env:DINGOO_PIE_TRACE_KBD_CALLERS
    DINGOO_PIE_AUTOPRESS_SEQUENCE = $env:DINGOO_PIE_AUTOPRESS_SEQUENCE
    DINGOO_PIE_IRJIT_CLOCK_HZ = $env:DINGOO_PIE_IRJIT_CLOCK_HZ
    DINGOO_PIE_RUNTIME_SPEED_SCALE = $env:DINGOO_PIE_RUNTIME_SPEED_SCALE
    DINGOO_PIE_OSTIMEDLY_SCALE = $env:DINGOO_PIE_OSTIMEDLY_SCALE
    DINGOO_PIE_DISPLAY_FPS = $env:DINGOO_PIE_DISPLAY_FPS
    DINGOO_PIE_DUMP_FRAME_BMP = $env:DINGOO_PIE_DUMP_FRAME_BMP
    DINGOO_PIE_DUMP_FRAME_START = $env:DINGOO_PIE_DUMP_FRAME_START
    DINGOO_PIE_DUMP_FRAME_END = $env:DINGOO_PIE_DUMP_FRAME_END
    DINGOO_PIE_DUMP_FRAME_STEP = $env:DINGOO_PIE_DUMP_FRAME_STEP
}

try {
    Set-EnvValue "DINGOO_PIE_LOG_FILE" "1"
    Set-EnvValue "DINGOO_PIE_PROFILE" "1"
    Set-EnvValue "DINGOO_PIE_TRACE_TASKS" "1"
    $traceHleValue = if ($TraceHle) { "1" } else { "" }
    $traceInputValue = if ($TraceInput) { "1" } else { "" }
    $traceKbdCallersValue = if ($TraceKbdCallers) { "1" } else { "" }
    Set-EnvValue "DINGOO_PIE_TRACE_HLE" $traceHleValue
    Set-EnvValue "DINGOO_PIE_INPUT_TRACE" $traceInputValue
    Set-EnvValue "DINGOO_PIE_TRACE_KBD_CALLERS" $traceKbdCallersValue
    Set-EnvValue "DINGOO_PIE_AUTOPRESS_SEQUENCE" $AutoPressSequence
    Set-EnvValue "DINGOO_PIE_IRJIT_CLOCK_HZ" $CpuHz
    Set-EnvValue "DINGOO_PIE_RUNTIME_SPEED_SCALE" $RuntimeSpeedScale
    Set-EnvValue "DINGOO_PIE_OSTIMEDLY_SCALE" $OsTimeDlyScale
    Set-EnvValue "DINGOO_PIE_DISPLAY_FPS" $DisplayFps
    Set-EnvValue "DINGOO_PIE_DUMP_FRAME_BMP" $resolvedDumpFramePattern
    Set-EnvValue "DINGOO_PIE_DUMP_FRAME_START" $DumpFrameStart
    Set-EnvValue "DINGOO_PIE_DUMP_FRAME_END" $DumpFrameEnd
    Set-EnvValue "DINGOO_PIE_DUMP_FRAME_STEP" $DumpFrameStep

    $startedAt = Get-Date
    $windowStyle = if ($CloseAfterSeconds -gt 0) { "Normal" } else { "Hidden" }
    $process = Start-Process -FilePath $exe `
        -ArgumentList @($AppPath) `
        -WorkingDirectory $BuildDir `
        -PassThru `
        -WindowStyle $windowStyle

    $deadline = $startedAt.AddSeconds($TimeoutSeconds)
    $closeAt = if ($CloseAfterSeconds -gt 0) { $startedAt.AddSeconds($CloseAfterSeconds) } else { $null }
    $gracefulCloseSent = $false
    $gracefulCloseResult = $false
    do {
        Start-Sleep -Milliseconds 100
        $process.Refresh()
        if (!$process.HasExited -and $closeAt -and !$gracefulCloseSent -and (Get-Date) -ge $closeAt) {
            $gracefulCloseSent = $true
            $gracefulCloseResult = $process.CloseMainWindow()
        }
    } while (!$process.HasExited -and (Get-Date) -lt $deadline)

    $endedAt = Get-Date
    $naturalExit = $process.HasExited
    $exitCode = if ($process.HasExited) { $process.ExitCode } else { $null }
    $stoppedByHarness = $false
    if (!$process.HasExited) {
        Stop-Process -Id $process.Id -Force
        $stoppedByHarness = $true
        $null = Wait-ForProcessExit $process.Id 5000
        $process.Refresh()
        $endedAt = Get-Date
    }

    $rawLog = Get-ChildItem -LiteralPath $BuildDir -Filter "DingooPie-debug-*.log" -File -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1
    if ($rawLog) {
        Copy-Item -LiteralPath $rawLog.FullName -Destination $savedLog -Force
    }

    $lines = if (Test-Path -LiteralPath $savedLog -PathType Leaf) { Get-Content -LiteralPath $savedLog } else { @() }
    $taskStops = @(Parse-TaskStops $lines)
    $guestExits = @(Parse-GuestExitReasons $lines)
    $dumpedFrames = @(Parse-DumpedFrames $lines)
    $firstDumpedFrame = if ($dumpedFrames.Count -gt 0) { $dumpedFrames[0] } else { $null }
    $lastDumpedFrame = if ($dumpedFrames.Count -gt 0) { $dumpedFrames[-1] } else { $null }
    $returnToTitle = $false
    $leftTitleBeforeReturn = $false
    if (![string]::IsNullOrWhiteSpace($TitleFrameHash) -and $lastDumpedFrame -and $lastDumpedFrame.ahash) {
        foreach ($frame in $dumpedFrames) {
            if ($frame.ahash -and
                ![string]::Equals($frame.ahash, $TitleFrameHash, [System.StringComparison]::OrdinalIgnoreCase)) {
                $leftTitleBeforeReturn = $true
                break
            }
        }
        $returnToTitle = $leftTitleBeforeReturn -and
            [string]::Equals($lastDumpedFrame.ahash, $TitleFrameHash, [System.StringComparison]::OrdinalIgnoreCase)
    }
    $residualProcesses = @(Get-Process DingooPie,dingoo-pie -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Id)

    $sawShutdownComplete = [bool]($lines | Select-String -Pattern '^frontend: shutdown complete' -Quiet)

    $result = [pscustomobject]@{
        name = $Name
        app = $AppPath
        backend = $Backend
        timeout_seconds = $TimeoutSeconds
        close_after_seconds = $CloseAfterSeconds
        graceful_close_sent = $gracefulCloseSent
        graceful_close_result = $gracefulCloseResult
        elapsed_ms = [int][math]::Round(($endedAt - $startedAt).TotalMilliseconds)
        auto_press_sequence = $AutoPressSequence
        log = $savedLog
        result_json = $resultJson
        dump_frame_pattern = $resolvedDumpFramePattern
        dumped_frames = $dumpedFrames
        first_dumped_frame = $firstDumpedFrame
        last_dumped_frame = $lastDumpedFrame
        title_frame_hash = $TitleFrameHash
        left_title_before_return = $leftTitleBeforeReturn
        return_to_title = $returnToTitle
        natural_exit = $naturalExit
        exit_code = $exitCode
        stopped_by_harness = $stoppedByHarness
        process_alive_after_timeout = !$naturalExit
        residual_process_ids = $residualProcesses
        no_residual_process = [bool]($residualProcesses.Count -eq 0)
        app_hash = First-LogValue $lines '^DingooPie: app sha256: ' '^.*sha256: '
        compat_profile = First-LogValue $lines '^DingooPie: compat profile: ' '^.*profile: '
        effective_backend = First-LogValue $lines '^DingooPie: execution backend effective: ' '^.*effective: '
        saw_app_start = [bool]($lines | Select-String -Pattern '^DingooPie: start app: ' -Quiet)
        saw_runtime_start = [bool]($lines | Select-String -Pattern '^DingooPie: native runtime start ' -Quiet)
        failed_to_open_app = [bool]($lines | Select-String -Pattern '^DingooPie: failed to open app: ' -Quiet)
        saw_frontend_quit = [bool]($lines | Select-String -Pattern '^frontend: quit requested' -Quiet)
        saw_shutdown_complete = $sawShutdownComplete
        saw_runtime_return = [bool]($lines | Select-String -Pattern '^DingooPie: native runtime returned err=' -Quiet)
        saw_runtime_destroyed = [bool]($lines | Select-String -Pattern '^DingooPie: native runtime destroyed' -Quiet)
        saw_stop_requested = [bool]($lines | Select-String -Pattern '^DingooPie: stop requested runtime=' -Quiet)
        saw_guest_exit = [bool]($guestExits.Count -gt 0)
        guest_exit_reasons = $guestExits
        saw_task_stop = [bool]($taskStops.Count -gt 0)
        task_stops = $taskStops
        unpromoted_task_stop_ras = @($taskStops | Where-Object { !$_.promoted } | Select-Object -ExpandProperty ra -Unique)
        promoted_task_stop_ras = @($taskStops | Where-Object { $_.promoted } | Select-Object -ExpandProperty ra -Unique)
        saw_promoted_task_stop = [bool](@($taskStops | Where-Object { $_.promoted }).Count -gt 0)
        clean_guest_exit = [bool]($naturalExit -and ($guestExits.Count -gt 0) -and $sawShutdownComplete)
    }

    $json = $result | ConvertTo-Json -Depth 6
    Set-Content -LiteralPath $resultJson -Value $json -Encoding UTF8
    $json

    if ($RequireNaturalExit -and !$naturalExit) {
        throw "Game did not exit naturally before ${TimeoutSeconds}s; log: $savedLog"
    }
}
finally {
    foreach ($key in $previousEnv.Keys) {
        Set-EnvValue $key $previousEnv[$key]
    }
    Remove-Item -LiteralPath $ini -ErrorAction SilentlyContinue
    if (Test-Path -LiteralPath $backup -PathType Leaf) {
        Move-Item -LiteralPath $backup -Destination $ini -Force
    }
    if ($lockStream) {
        $lockStream.Dispose()
    }
}
