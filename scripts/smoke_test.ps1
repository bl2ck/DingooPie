param(
    [Parameter(Mandatory = $true)]
    [string]$AppPath,

    [string]$BuildDir = "",
    [int]$Seconds = 8,
    [string]$Name = "smoke-test",
    [switch]$NoConfig,
    [switch]$TraceIrJit
)

$ErrorActionPreference = "Stop"

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectRoot = Split-Path -Parent $scriptRoot
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path (Split-Path -Parent $projectRoot) "build-dingoo-pie"
}

$exe = Join-Path $BuildDir "DingooPie.exe"
if (!(Test-Path -LiteralPath $exe)) {
    throw "Missing emulator executable: $exe"
}
if (!(Test-Path -LiteralPath $AppPath)) {
    throw "Missing app file: $AppPath"
}

$ini = Join-Path $BuildDir "DingooPie.ini"
$backup = Join-Path $BuildDir "DingooPie.ini.smoke-bak"
$safeName = $Name -replace '[^A-Za-z0-9_.-]', '_'
$log = Join-Path $BuildDir "$safeName.log"
$errLog = Join-Path $BuildDir "$safeName.err.log"

# Smoke tests own the emulator process during the run. Keep them sequential;
# concurrent runs can terminate each other during this cleanup step.
Get-Process DingooPie,dingoo-pie -ErrorAction SilentlyContinue | Stop-Process -Force
Remove-Item -LiteralPath $log, $errLog -ErrorAction SilentlyContinue

$hadConfig = Test-Path -LiteralPath $ini
if ($NoConfig) {
    Remove-Item -LiteralPath $backup -ErrorAction SilentlyContinue
    if ($hadConfig) {
        Move-Item -LiteralPath $ini -Destination $backup -Force
    }
}

try {
    $env:DINGOO_PIE_PROFILE = "1"
    $env:DINGOO_PIE_IRJIT_TRACE = if ($TraceIrJit) { "1" } else { "" }

    $process = Start-Process -FilePath $exe `
        -ArgumentList @($AppPath) `
        -WorkingDirectory $BuildDir `
        -RedirectStandardOutput $log `
        -RedirectStandardError $errLog `
        -PassThru

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
            # Some PowerShell versions throw if the process disappears before
            # Wait-Process attaches. The pre-stop alive flag is the smoke result.
        }
    }

    $configVisibleAfterRun = Test-Path -LiteralPath $ini
    $lines = if (Test-Path -LiteralPath $log) { Get-Content -LiteralPath $log } else { @() }
    $errLines = if (Test-Path -LiteralPath $errLog) { Get-Content -LiteralPath $errLog } else { @() }
    $summary = [ordered]@{
        name = $Name
        app = $AppPath
        seconds = $Seconds
        stdout_log = $log
        stderr_log = $errLog
        exit_code = $exitCode
        process_alive_after_wait = $aliveAfterWait
        stopped_by_script = $stoppedByScript
        config_existed_before = $hadConfig
        config_visible_after_run = $configVisibleAfterRun
        config_created_during_run = (-not $hadConfig) -and $configVisibleAfterRun
        saw_app_hash = [bool]($lines | Select-String -Pattern "DingooPie: app sha256:" -Quiet)
        saw_compat_profile = [bool]($lines | Select-String -Pattern "DingooPie: compat profile:" -Quiet)
        saw_video_settings = [bool]($lines | Select-String -Pattern "frontend: video settings" -Quiet)
        saw_runtime_speed = [bool]($lines | Select-String -Pattern "runtime speed scale|speed_scale=0\\.500" -Quiet)
        saw_hle_profile = [bool]($lines | Select-String -Pattern "^profile:hle " -Quiet)
        saw_fsys_profile = [bool]($lines | Select-String -Pattern "^profile:fsys " -Quiet)
        saw_irjit_profile = [bool]($lines | Select-String -Pattern "^profile:irjit " -Quiet)
        stderr_nonempty = [bool]($errLines.Count -gt 0)
    }

    $summary | ConvertTo-Json
}
finally {
    if ($NoConfig) {
        Remove-Item -LiteralPath $ini -ErrorAction SilentlyContinue
        if (Test-Path -LiteralPath $backup) {
            Move-Item -LiteralPath $backup -Destination $ini -Force
        }
    }
}
