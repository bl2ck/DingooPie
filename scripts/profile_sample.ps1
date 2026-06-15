param(
    [Parameter(Mandatory = $true)]
    [string]$AppPath,

    [Parameter(Mandatory = $true)]
    [string]$BuildDir,

    [int]$Seconds = 20,
    [string]$Name = "profile-sample",
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
    [int]$SkipProfileSamples = 0,
    [int]$MinFrontendSamples = 0,
    [int]$MinIrJitSamples = 0,
    [int]$MinInterpreterSamples = 0,
    [double]$MinAvgPresentedFps = 0,
    [double]$MinAvgInterpreterIps = 0,
    [double]$MaxAvgFrameIntervalUs = 0,
    [double]$MaxAvgFrameIntervalMaxUs = 0,
    [double]$MaxPeakFrameIntervalUs = 0,
    [double]$MaxAvgOver25 = 0,
    [double]$MaxPeakOver25 = 0,
    [double]$MaxAvgOver33 = 0,
    [double]$MaxPeakOver33 = 0,
    [switch]$ShowFps,
    [switch]$TraceIrJit
)

$ErrorActionPreference = "Stop"

$BuildDir = (Resolve-Path -LiteralPath $BuildDir).Path
$AppPath = (Resolve-Path -LiteralPath $AppPath).Path
$exe = Join-Path $BuildDir "DingooPie.exe"
if (!(Test-Path -LiteralPath $exe)) {
    throw "Missing emulator executable: $exe"
}
if (!(Test-Path -LiteralPath $AppPath)) {
    throw "Missing app file: $AppPath"
}

$safeName = $Name -replace '[^A-Za-z0-9_.-]', '_'
$rawLog = Join-Path $BuildDir "DingooPie-debug.log"
$savedLog = Join-Path $BuildDir "$safeName.debug.log"
$resultJson = Join-Path $BuildDir "$safeName.profile.json"
$resolvedDumpFramePattern = $DumpFramePattern
if (![string]::IsNullOrWhiteSpace($resolvedDumpFramePattern) -and
    ![System.IO.Path]::IsPathRooted($resolvedDumpFramePattern)) {
    $resolvedDumpFramePattern = Join-Path $BuildDir $resolvedDumpFramePattern
}
$ini = Join-Path $BuildDir "DingooPie.ini"
$backup = Join-Path $BuildDir "DingooPie.ini.profile-bak"

$mutexBytes = [System.Text.Encoding]::UTF8.GetBytes($BuildDir.ToLowerInvariant())
$mutexHash = [System.BitConverter]::ToString(
    [System.Security.Cryptography.SHA256]::Create().ComputeHash($mutexBytes)
).Replace("-", "")
$profileMutex = New-Object System.Threading.Mutex($false, "DingooPieProfileSample-$mutexHash")
$profileMutexHeld = $false
try {
    $profileMutexHeld = $profileMutex.WaitOne([TimeSpan]::FromMinutes(5))
} catch [System.Threading.AbandonedMutexException] {
    $profileMutexHeld = $true
}
if (!$profileMutexHeld) {
    throw "Timed out waiting for profile_sample lock on BuildDir: $BuildDir"
}

try {
Get-Process DingooPie,dingoo-pie -ErrorAction SilentlyContinue | Stop-Process -Force
Remove-Item -LiteralPath $rawLog, $savedLog, $resultJson -ErrorAction SilentlyContinue

$hadConfig = Test-Path -LiteralPath $ini
Remove-Item -LiteralPath $backup -ErrorAction SilentlyContinue
if ($hadConfig) {
    Move-Item -LiteralPath $ini -Destination $backup -Force
}

@"
[debug]
show_console=0
profile=1

[video]
show_fps=$([int]$ShowFps.IsPresent)

[runtime]
backend=$Backend
cpu_hz=$CpuHz
speed_scale=$RuntimeSpeedScale
ostimedly_scale=$OsTimeDlyScale
"@ | Set-Content -LiteralPath $ini -Encoding ASCII

$previousEnv = @{
    DINGOO_PIE_LOG_FILE = $env:DINGOO_PIE_LOG_FILE
    DINGOO_PIE_PROFILE = $env:DINGOO_PIE_PROFILE
    DINGOO_PIE_IRJIT_TRACE = $env:DINGOO_PIE_IRJIT_TRACE
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
    $env:DINGOO_PIE_LOG_FILE = "1"
    $env:DINGOO_PIE_PROFILE = "1"
    $env:DINGOO_PIE_IRJIT_TRACE = if ($TraceIrJit) { "1" } else { "" }
    $env:DINGOO_PIE_AUTOPRESS_SEQUENCE = $AutoPressSequence
    $env:DINGOO_PIE_IRJIT_CLOCK_HZ = $CpuHz
    $env:DINGOO_PIE_RUNTIME_SPEED_SCALE = $RuntimeSpeedScale
    $env:DINGOO_PIE_OSTIMEDLY_SCALE = $OsTimeDlyScale
    $env:DINGOO_PIE_DISPLAY_FPS = $DisplayFps
    $env:DINGOO_PIE_DUMP_FRAME_BMP = $resolvedDumpFramePattern
    $env:DINGOO_PIE_DUMP_FRAME_START = $DumpFrameStart
    $env:DINGOO_PIE_DUMP_FRAME_END = $DumpFrameEnd
    $env:DINGOO_PIE_DUMP_FRAME_STEP = $DumpFrameStep

    $process = Start-Process -FilePath $exe `
        -ArgumentList @($AppPath) `
        -WorkingDirectory $BuildDir `
        -PassThru `
        -WindowStyle Hidden

    Start-Sleep -Seconds $Seconds
    $process.Refresh()
    $exitCode = if ($process.HasExited) { $process.ExitCode } else { $null }
    $aliveAfterWait = -not $process.HasExited
    $stoppedByScript = $false
    if ($aliveAfterWait) {
        Stop-Process -Id $process.Id -Force
        $stoppedByScript = $true
        try {
            Wait-Process -Id $process.Id -Timeout 5 -ErrorAction SilentlyContinue
        } catch {
        }
    }

    if (Test-Path -LiteralPath $rawLog) {
        Copy-Item -LiteralPath $rawLog -Destination $savedLog -Force
    }

    $lines = if (Test-Path -LiteralPath $savedLog) { Get-Content -LiteralPath $savedLog } else { @() }
    $frontend = @()
    $irjit = @()
    $interpreter = @()
    $hle = @()

    foreach ($line in $lines) {
        if ($line -match 'profile frontend:.*draws=(\d+)/s.*presented_fps=(\d+).*submitted_fps=(\d+).*content_fps=(\d+)') {
            $frontend += [pscustomobject]@{
                draws = [int]$matches[1]
                presented_fps = [int]$matches[2]
                submitted_fps = [int]$matches[3]
                content_fps = [int]$matches[4]
            }
        } elseif ($line -match 'profile frontend:.*draws=(\d+)/s.*submitted_fps=(\d+).*content_fps=(\d+)') {
            $frontend += [pscustomobject]@{
                draws = [int]$matches[1]
                presented_fps = [int]$matches[2]
                submitted_fps = [int]$matches[2]
                content_fps = [int]$matches[3]
            }
        } elseif ($line -match 'profile irjit:.*hooks=(\d+)/s.*fast_lcd=(\d+)/s.*guest_mhz=(\d+).*clock_hz=(\d+).*fb_submit=(\d+).*fb_copy_us=(\d+).*fb_interval_us=(\d+)/(\d+).*over25=(\d+).*over33=(\d+)') {
            $irjit += [pscustomobject]@{
                hooks = [int]$matches[1]
                fast_lcd = [int]$matches[2]
                guest_mhz = [int]$matches[3]
                clock_hz = [int]$matches[4]
                fb_submit = [int]$matches[5]
                fb_copy_us = [int]$matches[6]
                fb_interval_avg_us = [int]$matches[7]
                fb_interval_max_us = [int]$matches[8]
                over25 = [int]$matches[9]
                over33 = [int]$matches[10]
            }
        } elseif ($line -match 'profile interpreter:.*ips=(\d+).*hooks=(\d+)/s.*fb_submit=(\d+).*fb_copy_us=(\d+).*fb_interval_us=(\d+)/(\d+).*over25=(\d+).*over33=(\d+)') {
            $interpreter += [pscustomobject]@{
                ips = [int64]$matches[1]
                hooks = [int]$matches[2]
                fb_submit = [int]$matches[3]
                fb_copy_us = [int]$matches[4]
                fb_interval_avg_us = [int]$matches[5]
                fb_interval_max_us = [int]$matches[6]
                over25 = [int]$matches[7]
                over33 = [int]$matches[8]
            }
        } elseif ($line -match 'profile hle:.*time=(\d+).*ostimedly=(\d+)/(\d+)ticks') {
            $hle += [pscustomobject]@{
                time = [int]$matches[1]
                ostimedly = [int]$matches[2]
                ostimedly_ticks = [int]$matches[3]
            }
        }
    }

    function Average-Field($items, $field) {
        if (!$items -or $items.Count -eq 0) {
            return $null
        }
        return [math]::Round((($items | Measure-Object -Property $field -Average).Average), 2)
    }

    function Max-Field($items, $field) {
        if (!$items -or $items.Count -eq 0) {
            return $null
        }
        return [math]::Round((($items | Measure-Object -Property $field -Maximum).Maximum), 2)
    }

    function Skip-Samples($items, $count) {
        if (!$items -or $items.Count -eq 0) {
            return @()
        }
        if ($count -le 0) {
            return @($items)
        }
        if ($items.Count -le $count) {
            return @()
        }
        return @($items | Select-Object -Skip $count)
    }

    function First-LogValue($items, $pattern, $replacement) {
        $match = $items | Select-String -Pattern $pattern | Select-Object -First 1
        if (!$match) {
            return ""
        }
        return ($match.Line -replace $replacement, "")
    }

    $frontendRawCount = $frontend.Count
    $irjitRawCount = $irjit.Count
    $interpreterRawCount = $interpreter.Count
    $hleRawCount = $hle.Count
    $frontend = @(Skip-Samples $frontend $SkipProfileSamples)
    $irjit = @(Skip-Samples $irjit $SkipProfileSamples)
    $interpreter = @(Skip-Samples $interpreter $SkipProfileSamples)
    $hle = @(Skip-Samples $hle $SkipProfileSamples)

    $cpu = if ($Backend -eq "interpreter") { $interpreter } else { $irjit }

    $result = [pscustomobject]@{
        name = $Name
        app = $AppPath
        backend = $Backend
        seconds = $Seconds
        log = $savedLog
        result_json = $resultJson
        dump_frame_pattern = $resolvedDumpFramePattern
        exit_code = $exitCode
        process_alive_after_wait = $aliveAfterWait
        stopped_by_script = $stoppedByScript
        app_hash = First-LogValue $lines '^DingooPie: app sha256: ' '^.*sha256: '
        compat_profile = First-LogValue $lines '^DingooPie: compat profile: ' '^.*profile: '
        effective_backend = First-LogValue $lines '^DingooPie: execution backend effective: ' '^.*effective: '
        saw_app_start = [bool]($lines | Select-String -Pattern '^DingooPie: start app: ' -Quiet)
        saw_app_hash = [bool]($lines | Select-String -Pattern '^DingooPie: app sha256: ' -Quiet)
        saw_runtime_start = [bool]($lines | Select-String -Pattern '^DingooPie: native runtime start ' -Quiet)
        failed_to_open_app = [bool]($lines | Select-String -Pattern '^DingooPie: failed to open app: ' -Quiet)
        skipped_profile_samples = $SkipProfileSamples
        frontend_raw_samples = $frontendRawCount
        irjit_raw_samples = $irjitRawCount
        interpreter_raw_samples = $interpreterRawCount
        hle_raw_samples = $hleRawCount
        frontend_samples = $frontend.Count
        irjit_samples = $irjit.Count
        interpreter_samples = $interpreter.Count
        hle_samples = $hle.Count
        avg_draws = Average-Field $frontend "draws"
        avg_presented_fps = Average-Field $frontend "presented_fps"
        avg_submitted_fps = Average-Field $frontend "submitted_fps"
        avg_content_fps = Average-Field $frontend "content_fps"
        avg_guest_mhz = Average-Field $irjit "guest_mhz"
        avg_clock_hz = Average-Field $irjit "clock_hz"
        avg_interpreter_ips = Average-Field $interpreter "ips"
        avg_fb_interval_us = Average-Field $cpu "fb_interval_avg_us"
        avg_fb_interval_max_us = Average-Field $cpu "fb_interval_max_us"
        peak_fb_interval_max_us = Max-Field $cpu "fb_interval_max_us"
        avg_fb_copy_us = Average-Field $cpu "fb_copy_us"
        avg_over25 = Average-Field $cpu "over25"
        peak_over25 = Max-Field $cpu "over25"
        avg_over33 = Average-Field $cpu "over33"
        peak_over33 = Max-Field $cpu "over33"
        avg_ostimedly = Average-Field $hle "ostimedly"
        saw_profile = [bool]($frontend.Count -gt 0 -or $irjit.Count -gt 0 -or $interpreter.Count -gt 0 -or $hle.Count -gt 0)
    }

    $failures = @()
    if ($MinFrontendSamples -gt 0 -and $result.frontend_samples -lt $MinFrontendSamples) {
        $failures += "frontend_samples $($result.frontend_samples) < $MinFrontendSamples"
    }
    if ($MinIrJitSamples -gt 0 -and $result.irjit_samples -lt $MinIrJitSamples) {
        $failures += "irjit_samples $($result.irjit_samples) < $MinIrJitSamples"
    }
    if ($MinInterpreterSamples -gt 0 -and $result.interpreter_samples -lt $MinInterpreterSamples) {
        $failures += "interpreter_samples $($result.interpreter_samples) < $MinInterpreterSamples"
    }
    if ($MinAvgPresentedFps -gt 0 -and ($null -eq $result.avg_presented_fps -or $result.avg_presented_fps -lt $MinAvgPresentedFps)) {
        $failures += "avg_presented_fps $($result.avg_presented_fps) < $MinAvgPresentedFps"
    }
    if ($MinAvgInterpreterIps -gt 0 -and ($null -eq $result.avg_interpreter_ips -or $result.avg_interpreter_ips -lt $MinAvgInterpreterIps)) {
        $failures += "avg_interpreter_ips $($result.avg_interpreter_ips) < $MinAvgInterpreterIps"
    }
    if ($MaxAvgFrameIntervalUs -gt 0 -and ($null -eq $result.avg_fb_interval_us -or $result.avg_fb_interval_us -gt $MaxAvgFrameIntervalUs)) {
        $failures += "avg_fb_interval_us $($result.avg_fb_interval_us) > $MaxAvgFrameIntervalUs"
    }
    if ($MaxAvgFrameIntervalMaxUs -gt 0 -and ($null -eq $result.avg_fb_interval_max_us -or $result.avg_fb_interval_max_us -gt $MaxAvgFrameIntervalMaxUs)) {
        $failures += "avg_fb_interval_max_us $($result.avg_fb_interval_max_us) > $MaxAvgFrameIntervalMaxUs"
    }
    if ($MaxPeakFrameIntervalUs -gt 0 -and ($null -eq $result.peak_fb_interval_max_us -or $result.peak_fb_interval_max_us -gt $MaxPeakFrameIntervalUs)) {
        $failures += "peak_fb_interval_max_us $($result.peak_fb_interval_max_us) > $MaxPeakFrameIntervalUs"
    }
    if ($MaxAvgOver25 -gt 0 -and ($null -eq $result.avg_over25 -or $result.avg_over25 -gt $MaxAvgOver25)) {
        $failures += "avg_over25 $($result.avg_over25) > $MaxAvgOver25"
    }
    if ($MaxPeakOver25 -gt 0 -and ($null -eq $result.peak_over25 -or $result.peak_over25 -gt $MaxPeakOver25)) {
        $failures += "peak_over25 $($result.peak_over25) > $MaxPeakOver25"
    }
    if ($MaxAvgOver33 -gt 0 -and ($null -eq $result.avg_over33 -or $result.avg_over33 -gt $MaxAvgOver33)) {
        $failures += "avg_over33 $($result.avg_over33) > $MaxAvgOver33"
    }
    if ($MaxPeakOver33 -gt 0 -and ($null -eq $result.peak_over33 -or $result.peak_over33 -gt $MaxPeakOver33)) {
        $failures += "peak_over33 $($result.peak_over33) > $MaxPeakOver33"
    }

    $json = $result | ConvertTo-Json
    Set-Content -LiteralPath $resultJson -Value $json -Encoding UTF8
    $json
    if ($failures.Count -gt 0) {
        throw "Profile thresholds failed: $($failures -join '; ')"
    }
}
finally {
    foreach ($key in $previousEnv.Keys) {
        Set-Item -Path "env:$key" -Value $previousEnv[$key]
    }
    Remove-Item -LiteralPath $ini -ErrorAction SilentlyContinue
    if (Test-Path -LiteralPath $backup) {
        Move-Item -LiteralPath $backup -Destination $ini -Force
    }
}
}
finally {
    if ($profileMutexHeld) {
        $profileMutex.ReleaseMutex() | Out-Null
    }
    if ($profileMutex) {
        $profileMutex.Dispose()
    }
}
