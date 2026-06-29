param(
    [string]$BuildDir = "",
    [string]$OutputDir = "",
    [int]$Seconds = 2
)

$ErrorActionPreference = "Stop"

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectRoot = Split-Path -Parent $scriptRoot
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $projectRoot "release"
}
if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = Join-Path $projectRoot "test_artifacts\debug_output_regression"
}

$BuildDir = (Resolve-Path -LiteralPath $BuildDir).Path
New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
$OutputDir = (Resolve-Path -LiteralPath $OutputDir).Path

$runtimeFiles = @("DingooPie.exe", "SDL2.dll", "libcapstone.dll", "libwinpthread-1.dll")
foreach ($fileName in $runtimeFiles) {
    $path = Join-Path $BuildDir $fileName
    if (!(Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Missing runtime file: $path"
    }
}

function Copy-RuntimeFiles {
    param(
        [Parameter(Mandatory = $true)]
        [string]$SourceDir,
        [Parameter(Mandatory = $true)]
        [string]$DestinationDir
    )

    New-Item -ItemType Directory -Path $DestinationDir -Force | Out-Null
    foreach ($fileName in $script:runtimeFiles) {
        Copy-Item -LiteralPath (Join-Path $SourceDir $fileName) -Destination $DestinationDir -Force
    }
}

function Set-TestEnv {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Name,
        [string]$Value
    )

    if ([string]::IsNullOrEmpty($Value)) {
        [Environment]::SetEnvironmentVariable($Name, $null, "Process")
    } else {
        [Environment]::SetEnvironmentVariable($Name, $Value, "Process")
    }
}

function Stop-TestProcess {
    param([System.Diagnostics.Process]$Process)

    if (!$Process) {
        return
    }

    $Process.Refresh()
    if (!$Process.HasExited) {
        Stop-Process -Id $Process.Id -Force
        try {
            Wait-Process -Id $Process.Id -Timeout 5 -ErrorAction SilentlyContinue
        } catch {
            # The process can exit between Stop-Process and Wait-Process.
        }
    }
}

function Test-Patterns {
    param(
        [string[]]$Lines,
        [string[]]$Required,
        [string[]]$Rejected
    )

    $missing = @()
    foreach ($pattern in $Required) {
        if (!($Lines | Select-String -Pattern $pattern -Quiet)) {
            $missing += $pattern
        }
    }

    $unexpected = @()
    foreach ($pattern in $Rejected) {
        if ($Lines | Select-String -Pattern $pattern -Quiet) {
            $unexpected += $pattern
        }
    }

    @{
        Missing = $missing
        Unexpected = $unexpected
    }
}

function Invoke-LogCase {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Name,
        [string]$IniText = "",
        [bool]$EnableEnvLog = $false,
        [bool]$ExpectDebugLog = $true,
        [string[]]$RequiredPatterns = @(),
        [string[]]$RejectedPatterns = @()
    )

    $runDir = Join-Path $script:OutputDir $Name
    if (Test-Path -LiteralPath $runDir) {
        Remove-Item -LiteralPath $runDir -Recurse -Force
    }
    Copy-RuntimeFiles $script:BuildDir $runDir

    if (![string]::IsNullOrWhiteSpace($IniText)) {
        Set-Content -LiteralPath (Join-Path $runDir "DingooPie.ini") -Value $IniText -Encoding ASCII
    }

    $rawLog = Join-Path $runDir "DingooPie-debug.log"
    $previousLogEnv = [Environment]::GetEnvironmentVariable("DINGOO_PIE_LOG_FILE", "Process")
    $process = $null
    try {
        Set-TestEnv "DINGOO_PIE_LOG_FILE" $(if ($EnableEnvLog) { "1" } else { "" })
        $process = Start-Process -FilePath (Join-Path $runDir "DingooPie.exe") `
            -WorkingDirectory $runDir `
            -WindowStyle Hidden `
            -PassThru
        Start-Sleep -Seconds $script:Seconds
    }
    finally {
        Stop-TestProcess $process
        Set-TestEnv "DINGOO_PIE_LOG_FILE" $previousLogEnv
    }

    $logExists = Test-Path -LiteralPath $rawLog -PathType Leaf
    $lines = if ($logExists) { Get-Content -LiteralPath $rawLog } else { @() }
    $patternResult = Test-Patterns $lines $RequiredPatterns $RejectedPatterns
    $passed = ($logExists -eq $ExpectDebugLog) -and
        ($patternResult.Missing.Count -eq 0) -and
        ($patternResult.Unexpected.Count -eq 0)

    [ordered]@{
        name = $Name
        type = "debug-log"
        run_dir = $runDir
        log = $rawLog
        passed = $passed
        log_exists = $logExists
        expected_log = $ExpectDebugLog
        line_count = $lines.Count
        missing = $patternResult.Missing
        unexpected = $patternResult.Unexpected
        first_lines = (($lines | Select-Object -First 8) -join " | ")
    }
}

function Invoke-RedirectCase {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Name,
        [string[]]$RequiredPatterns = @(),
        [string[]]$RejectedPatterns = @()
    )

    $runDir = Join-Path $script:OutputDir $Name
    if (Test-Path -LiteralPath $runDir) {
        Remove-Item -LiteralPath $runDir -Recurse -Force
    }
    Copy-RuntimeFiles $script:BuildDir $runDir

    $stdoutLog = Join-Path $runDir "stdout.log"
    $stderrLog = Join-Path $runDir "stderr.log"
    $previousLogEnv = [Environment]::GetEnvironmentVariable("DINGOO_PIE_LOG_FILE", "Process")
    $process = $null
    try {
        Set-TestEnv "DINGOO_PIE_LOG_FILE" ""
        $process = Start-Process -FilePath (Join-Path $runDir "DingooPie.exe") `
            -WorkingDirectory $runDir `
            -RedirectStandardOutput $stdoutLog `
            -RedirectStandardError $stderrLog `
            -WindowStyle Hidden `
            -PassThru
        Start-Sleep -Seconds $script:Seconds
    }
    finally {
        Stop-TestProcess $process
        Set-TestEnv "DINGOO_PIE_LOG_FILE" $previousLogEnv
    }

    $lines = if (Test-Path -LiteralPath $stdoutLog -PathType Leaf) {
        Get-Content -LiteralPath $stdoutLog
    } else {
        @()
    }
    $stderrLines = if (Test-Path -LiteralPath $stderrLog -PathType Leaf) {
        Get-Content -LiteralPath $stderrLog
    } else {
        @()
    }
    $patternResult = Test-Patterns $lines $RequiredPatterns $RejectedPatterns
    $passed = ($patternResult.Missing.Count -eq 0) -and
        ($patternResult.Unexpected.Count -eq 0) -and
        ($stderrLines.Count -eq 0)

    [ordered]@{
        name = $Name
        type = "stdout-redirect"
        run_dir = $runDir
        stdout_log = $stdoutLog
        stderr_log = $stderrLog
        passed = $passed
        stdout_line_count = $lines.Count
        stderr_line_count = $stderrLines.Count
        missing = $patternResult.Missing
        unexpected = $patternResult.Unexpected
        first_lines = (($lines | Select-Object -First 8) -join " | ")
    }
}

$baseRequired = @(
    '^main: settings loaded ',
    '^main: no startup app; frontend is waiting for File/Open Game$',
    '^main: initializing frontend$',
    '^sdl-log: info application: Audio buffer samples set to 2048$'
)

$results = @()
$results += Invoke-LogCase `
    -Name "env-log-only" `
    -EnableEnvLog $true `
    -RequiredPatterns ($baseRequired + @(
        '^debug-log: opened path=DingooPie-debug\.log routing=pipe$',
        '^main: settings loaded .*debug\.show_console=0 debug\.profile=0 external_log=1$',
        '^settings-trace: loaded debug\.show_console=0 debug\.profile=0$'
    )) `
    -RejectedPatterns @('^debug-console: opened ')

$results += Invoke-LogCase `
    -Name "ini-performance-log" `
    -IniText "[debug]`r`nshow_console=0`r`nprofile=1`r`n" `
    -RequiredPatterns ($baseRequired + @(
        '^debug-log: opened path=DingooPie-debug\.log routing=pipe$',
        '^main: settings loaded .*debug\.show_console=0 debug\.profile=1 external_log=0$',
        '^settings-trace: loaded debug\.show_console=0 debug\.profile=1$'
    )) `
    -RejectedPatterns @('^debug-console: opened ')

$results += Invoke-LogCase `
    -Name "console-and-log" `
    -IniText "[debug]`r`nshow_console=1`r`nprofile=0`r`n" `
    -EnableEnvLog $true `
    -RequiredPatterns ($baseRequired + @(
        '^debug-log: opened path=DingooPie-debug\.log routing=pipe$',
        '^debug-console: opened encoding=utf-8 log=1$',
        '^main: settings loaded .*debug\.show_console=1 debug\.profile=0 external_log=1$',
        '^settings-trace: loaded debug\.show_console=1 debug\.profile=0$'
    ))

$results += Invoke-LogCase `
    -Name "console-only" `
    -IniText "[debug]`r`nshow_console=1`r`nprofile=0`r`n" `
    -ExpectDebugLog $false

$results += Invoke-RedirectCase `
    -Name "stdout-redirect" `
    -RequiredPatterns ($baseRequired + @(
        '^main: settings loaded .*debug\.show_console=0 debug\.profile=0 external_log=0$',
        '^frontend: video settings '
    )) `
    -RejectedPatterns @('^debug-log: opened ', '^debug-console: opened ')

$failed = @($results | Where-Object { -not $_.passed })
$summary = [ordered]@{
    build_dir = $BuildDir
    output_dir = $OutputDir
    seconds = $Seconds
    passed = $failed.Count -eq 0
    case_count = $results.Count
    failed_count = $failed.Count
    cases = $results
}

$summary | ConvertTo-Json -Depth 6
if ($failed.Count -gt 0) {
    throw "Debug output regression failed: $($failed.Count) case(s)"
}
