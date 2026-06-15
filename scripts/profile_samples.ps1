param(
    [Parameter(Mandatory = $true)]
    [string]$SampleDir,

    [Parameter(Mandatory = $true)]
    [string]$BuildDir,

    [string]$OutputDir = "",
    [int]$Seconds = 12,
    [int]$SkipProfileSamples = 2,
    [string]$AutoPressSequence = "",
    [string]$DumpFrameStart = "120",
    [string]$DumpFrameEnd = "360",
    [string]$DumpFrameStep = "120",
    [ValidateSet("ppsspp_irjit", "interpreter", "both")]
    [string]$Backend = "both"
)

$ErrorActionPreference = "Stop"

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectRoot = Split-Path -Parent $scriptRoot
$profileScript = Join-Path $scriptRoot "profile_sample.ps1"
if (!(Test-Path -LiteralPath $profileScript -PathType Leaf)) {
    throw "Missing profile script: $profileScript"
}

$SampleDir = (Resolve-Path -LiteralPath $SampleDir).Path
$BuildDir = (Resolve-Path -LiteralPath $BuildDir).Path
if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = Join-Path $projectRoot "test_artifacts\sample-profiles"
}
New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null

function ConvertTo-SafeName($Name) {
    $safe = $Name -replace '[^A-Za-z0-9_.-]', '_'
    $safe = $safe.Trim('_')
    if ([string]::IsNullOrWhiteSpace($safe)) {
        return ""
    }
    return $safe
}

function Read-ProfileJson($JsonPath) {
    if (!(Test-Path -LiteralPath $JsonPath -PathType Leaf)) {
        return $null
    }
    return Get-Content -LiteralPath $JsonPath -Raw | ConvertFrom-Json
}

function Format-Nullable($Value) {
    if ($null -eq $Value -or $Value -eq "") {
        return "-"
    }
    return [string]$Value
}

function Copy-MatchingArtifacts($SourceDir, $Name, $DestinationDir) {
    New-Item -ItemType Directory -Path $DestinationDir -Force | Out-Null
    $pattern = "$Name*"
    Get-ChildItem -LiteralPath $SourceDir -Filter $pattern -File -ErrorAction SilentlyContinue |
        ForEach-Object {
            Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $DestinationDir $_.Name) -Force
        }
}

$apps = Get-ChildItem -LiteralPath $SampleDir -Filter "*.app" -File | Sort-Object Name
if ($apps.Count -eq 0) {
    throw "No .app samples found under $SampleDir"
}

$backends = if ($Backend -eq "both") { @("ppsspp_irjit", "interpreter") } else { @($Backend) }
$results = @()
$runStamp = Get-Date -Format "yyyyMMdd-HHmmss"

for ($appIndex = 0; $appIndex -lt $apps.Count; ++$appIndex) {
    $app = $apps[$appIndex]
    $baseName = ConvertTo-SafeName ([System.IO.Path]::GetFileNameWithoutExtension($app.Name))
    if ([string]::IsNullOrWhiteSpace($baseName)) {
        $baseName = "sample{0:D2}" -f ($appIndex + 1)
    }
    foreach ($backendName in $backends) {
        $runName = "$runStamp-$baseName-$backendName"
        $artifactDir = Join-Path $OutputDir $runName
        New-Item -ItemType Directory -Path $artifactDir -Force | Out-Null
        $dumpPattern = Join-Path $BuildDir "$runName-frame-%u.bmp"

        $status = "pass"
        $errorMessage = ""
        try {
            $profileArgs = @(
                "-NoProfile",
                "-ExecutionPolicy", "Bypass",
                "-File", $profileScript,
                "-AppPath", $app.FullName,
                "-BuildDir", $BuildDir,
                "-Seconds", $Seconds,
                "-Name", $runName,
                "-Backend", $backendName,
                "-DumpFramePattern", $dumpPattern,
                "-DumpFrameStart", $DumpFrameStart,
                "-DumpFrameEnd", $DumpFrameEnd,
                "-DumpFrameStep", $DumpFrameStep,
                "-SkipProfileSamples", $SkipProfileSamples
            )
            if (![string]::IsNullOrWhiteSpace($AutoPressSequence)) {
                $profileArgs += @("-AutoPressSequence", $AutoPressSequence)
            }
            & powershell @profileArgs | Out-Null
            if ($LASTEXITCODE -ne 0) {
                throw "profile_sample.ps1 exited with code $LASTEXITCODE"
            }
        } catch {
            $status = "fail"
            $errorMessage = $_.Exception.Message
        }

        $jsonPath = Join-Path $BuildDir "$runName.profile.json"
        $profile = Read-ProfileJson $jsonPath
        Copy-MatchingArtifacts $BuildDir $runName $artifactDir

        $results += [pscustomobject]@{
            sample = $app.Name
            app_path = $app.FullName
            backend = $backendName
            status = $status
            error = $errorMessage
            artifact_dir = $artifactDir
            result_json = Join-Path $artifactDir "$runName.profile.json"
            log = Join-Path $artifactDir "$runName.debug.log"
            app_hash = if ($profile) { $profile.app_hash } else { "" }
            compat_profile = if ($profile) { $profile.compat_profile } else { "" }
            effective_backend = if ($profile) { $profile.effective_backend } else { "" }
            saw_runtime_start = if ($profile) { $profile.saw_runtime_start } else { $false }
            failed_to_open_app = if ($profile) { $profile.failed_to_open_app } else { $false }
            process_alive_after_wait = if ($profile) { $profile.process_alive_after_wait } else { $null }
            stopped_by_script = if ($profile) { $profile.stopped_by_script } else { $null }
            frontend_samples = if ($profile) { $profile.frontend_samples } else { $null }
            cpu_samples = if ($profile) {
                if ($backendName -eq "interpreter") { $profile.interpreter_samples } else { $profile.irjit_samples }
            } else { $null }
            avg_presented_fps = if ($profile) { $profile.avg_presented_fps } else { $null }
            avg_content_fps = if ($profile) { $profile.avg_content_fps } else { $null }
            avg_fb_interval_us = if ($profile) { $profile.avg_fb_interval_us } else { $null }
            peak_fb_interval_max_us = if ($profile) { $profile.peak_fb_interval_max_us } else { $null }
            avg_over25 = if ($profile) { $profile.avg_over25 } else { $null }
            peak_over25 = if ($profile) { $profile.peak_over25 } else { $null }
            avg_over33 = if ($profile) { $profile.avg_over33 } else { $null }
            peak_over33 = if ($profile) { $profile.peak_over33 } else { $null }
            avg_interpreter_ips = if ($profile) { $profile.avg_interpreter_ips } else { $null }
            avg_guest_mhz = if ($profile) { $profile.avg_guest_mhz } else { $null }
        }
    }
}

$csvPath = Join-Path $OutputDir "$runStamp-summary.csv"
$jsonSummaryPath = Join-Path $OutputDir "$runStamp-summary.json"
$markdownPath = Join-Path $OutputDir "$runStamp-summary.md"

$results | Export-Csv -LiteralPath $csvPath -NoTypeInformation -Encoding UTF8
$results | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $jsonSummaryPath -Encoding UTF8

$lines = @()
$lines += "# Dingoo Sample Profile Baseline"
$lines += ""
$lines += "- Date: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')"
$lines += "- Sample directory: ``$SampleDir``"
$lines += "- Build directory: ``$BuildDir``"
$lines += "- Duration per run: ${Seconds}s"
$lines += "- Backends: $($backends -join ', ')"
$autoPressText = if ($AutoPressSequence) { "``$AutoPressSequence``" } else { "(none)" }
$lines += "- AutoPressSequence: $autoPressText"
$lines += ""
$lines += "| Sample | Backend | Status | Runtime | FPS | Content FPS | FB avg us | FB peak us | over25 avg/peak | over33 avg/peak | Artifact | Notes |"
$lines += "|---|---|---|---|---:|---:|---:|---:|---:|---:|---|---|"
foreach ($row in $results) {
    $runtime = if ($row.saw_runtime_start) { "started" } elseif ($row.failed_to_open_app) { "open failed" } else { "not started" }
    $notes = if ($row.error) { $row.error.Replace("|", "/") } elseif ($row.stopped_by_script) { "stopped by harness after timed run" } else { "" }
    $artifactName = Split-Path -Leaf $row.artifact_dir
    $lines += "| $($row.sample) | $($row.backend) | $($row.status) | $runtime | $(Format-Nullable $row.avg_presented_fps) | $(Format-Nullable $row.avg_content_fps) | $(Format-Nullable $row.avg_fb_interval_us) | $(Format-Nullable $row.peak_fb_interval_max_us) | $(Format-Nullable $row.avg_over25)/$(Format-Nullable $row.peak_over25) | $(Format-Nullable $row.avg_over33)/$(Format-Nullable $row.peak_over33) | ``$artifactName`` | $notes |"
}

$lines += ""
$lines += "## Manual Verification Checklist"
$lines += ""
$lines += "For each sample, open the artifact directory and compare the dumped frames with the log:"
$lines += ""
$lines += "1. Startup: title screen or first playable scene is visible, not black or corrupted."
$lines += "2. Stability: no crash, no endless modal error, and the runtime reaches profile samples."
$lines += "3. Playability: when an AutoPress sequence is provided, input changes menu selection or player state."
$lines += "4. Timing: JIT should generally keep `avg_presented_fps` near the configured display cadence; interpreter is acceptable only when gameplay remains visually stable."
$lines += "5. Exit behavior: if a sample has a known in-game quit path, run that sequence and confirm no `DingooPie` process remains."

Set-Content -LiteralPath $markdownPath -Value $lines -Encoding UTF8

[pscustomobject]@{
    samples = $apps.Count
    backends = $backends
    results = $results.Count
    csv = $csvPath
    json = $jsonSummaryPath
    markdown = $markdownPath
} | ConvertTo-Json
