param(
    [Parameter(Mandatory = $true)]
    [string]$SampleDir,

    [Parameter(Mandatory = $true)]
    [string]$BuildDir,

    [string]$OutputDir = "",
    [int]$TimeoutSeconds = 20,
    [ValidateSet("ppsspp_irjit", "interpreter", "both")]
    [string]$Backend = "ppsspp_irjit",
    [string]$DefaultAutoPressSequence = "",
    [string]$SequenceCsv = "",
    [string]$TitleHashCsv = "",
    [string]$DumpFrameStart = "120",
    [string]$DumpFrameEnd = "480",
    [string]$DumpFrameStep = "120"
)

$ErrorActionPreference = "Stop"

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectRoot = Split-Path -Parent $scriptRoot
$quitScript = Join-Path $scriptRoot "quit_sample.ps1"
if (!(Test-Path -LiteralPath $quitScript -PathType Leaf)) {
    throw "Missing quit script: $quitScript"
}

$SampleDir = (Resolve-Path -LiteralPath $SampleDir).Path
$BuildDir = (Resolve-Path -LiteralPath $BuildDir).Path
if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = Join-Path $projectRoot "test_artifacts\sample-quits"
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

function Format-Nullable($Value) {
    if ($null -eq $Value -or $Value -eq "") {
        return "-"
    }
    return [string]$Value
}

function Read-QuitJson($JsonPath) {
    if (!(Test-Path -LiteralPath $JsonPath -PathType Leaf)) {
        return $null
    }
    return Get-Content -LiteralPath $JsonPath -Raw | ConvertFrom-Json
}

function Get-FileSha256($Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()
}

function Copy-MatchingArtifacts($SourceDir, $Name, $DestinationDir) {
    New-Item -ItemType Directory -Path $DestinationDir -Force | Out-Null
    $pattern = "$Name*"
    Get-ChildItem -LiteralPath $SourceDir -Filter $pattern -File -ErrorAction SilentlyContinue |
        ForEach-Object {
            Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $DestinationDir $_.Name) -Force
        }
}

function Add-OptionalArg([System.Collections.ArrayList]$ArgList, $Name, $Value) {
    if (![string]::IsNullOrWhiteSpace($Value)) {
        [void]$ArgList.Add($Name)
        [void]$ArgList.Add($Value)
    }
}

function Load-SequenceMap($Path) {
    $map = @{}
    if ([string]::IsNullOrWhiteSpace($Path)) {
        return $map
    }

    $resolved = (Resolve-Path -LiteralPath $Path).Path
    foreach ($row in Import-Csv -LiteralPath $resolved) {
        $sequence = $row.sequence
        if ([string]::IsNullOrWhiteSpace($sequence)) {
            continue
        }
        foreach ($keyName in @("sample", "app", "name", "hash")) {
            $key = $row.$keyName
            if (![string]::IsNullOrWhiteSpace($key)) {
                $map[[string]$key] = $sequence
            }
        }
    }
    return $map
}

function Load-TitleHashMap($Path) {
    $map = @{}
    if ([string]::IsNullOrWhiteSpace($Path)) {
        return $map
    }

    $resolved = (Resolve-Path -LiteralPath $Path).Path
    foreach ($row in Import-Csv -LiteralPath $resolved) {
        $hash = $row.title_hash
        if ([string]::IsNullOrWhiteSpace($hash)) {
            $hash = $row.ahash
        }
        if ([string]::IsNullOrWhiteSpace($hash)) {
            continue
        }
        foreach ($keyName in @("sample", "app", "name", "hash")) {
            $key = $row.$keyName
            if (![string]::IsNullOrWhiteSpace($key)) {
                $map[[string]$key] = $hash
            }
        }
    }
    return $map
}

function Resolve-Sequence($Map, $App, $AppHash, $DefaultSequence) {
    $keys = @(
        $App.Name,
        [System.IO.Path]::GetFileNameWithoutExtension($App.Name),
        $App.FullName,
        $AppHash
    )
    foreach ($key in $keys) {
        if ($Map.ContainsKey($key)) {
            return $Map[$key]
        }
    }
    return $DefaultSequence
}

function Resolve-TitleHash($Map, $App, $AppHash) {
    $keys = @(
        $App.Name,
        [System.IO.Path]::GetFileNameWithoutExtension($App.Name),
        $App.FullName,
        $AppHash
    )
    foreach ($key in $keys) {
        if ($Map.ContainsKey($key)) {
            return $Map[$key]
        }
    }
    return ""
}

$apps = Get-ChildItem -LiteralPath $SampleDir -Filter "*.app" -File | Sort-Object Name
if ($apps.Count -eq 0) {
    throw "No .app samples found under $SampleDir"
}

$sequenceMap = Load-SequenceMap $SequenceCsv
$titleHashMap = Load-TitleHashMap $TitleHashCsv
$backends = if ($Backend -eq "both") { @("ppsspp_irjit", "interpreter") } else { @($Backend) }
$runStamp = Get-Date -Format "yyyyMMdd-HHmmss"
$results = @()

for ($appIndex = 0; $appIndex -lt $apps.Count; ++$appIndex) {
    $app = $apps[$appIndex]
    $baseName = ConvertTo-SafeName ([System.IO.Path]::GetFileNameWithoutExtension($app.Name))
    if ([string]::IsNullOrWhiteSpace($baseName)) {
        $baseName = "sample{0:D2}" -f ($appIndex + 1)
    }

    $appHash = Get-FileSha256 $app.FullName
    $sequence = Resolve-Sequence $sequenceMap $app $appHash $DefaultAutoPressSequence
    foreach ($backendName in $backends) {
        $runName = "$runStamp-$baseName-$backendName"
        $artifactDir = Join-Path $OutputDir $runName
        New-Item -ItemType Directory -Path $artifactDir -Force | Out-Null
        $dumpPattern = Join-Path $BuildDir "$runName-frame-%u.bmp"

        $status = "pass"
        $errorMessage = ""
        try {
            $titleHash = Resolve-TitleHash $titleHashMap $app $appHash
            $quitArgs = [System.Collections.ArrayList]@(
                "-NoProfile",
                "-ExecutionPolicy", "Bypass",
                "-File", $quitScript,
                "-AppPath", $app.FullName,
                "-BuildDir", $BuildDir,
                "-TimeoutSeconds", $TimeoutSeconds,
                "-Name", $runName,
                "-Backend", $backendName,
                "-DumpFramePattern", $dumpPattern
            )
            Add-OptionalArg $quitArgs "-AutoPressSequence" $sequence
            Add-OptionalArg $quitArgs "-DumpFrameStart" $DumpFrameStart
            Add-OptionalArg $quitArgs "-DumpFrameEnd" $DumpFrameEnd
            Add-OptionalArg $quitArgs "-DumpFrameStep" $DumpFrameStep
            Add-OptionalArg $quitArgs "-TitleFrameHash" $titleHash
            & powershell @quitArgs | Out-Null
            if ($LASTEXITCODE -ne 0) {
                throw "quit_sample.ps1 exited with code $LASTEXITCODE"
            }
        } catch {
            $status = "fail"
            $errorMessage = $_.Exception.Message
        }

        $jsonPath = Join-Path $BuildDir "$runName.quit.json"
        $quit = Read-QuitJson $jsonPath
        Copy-MatchingArtifacts $BuildDir $runName $artifactDir

        $results += [pscustomobject]@{
            sample = $app.Name
            app_path = $app.FullName
            backend = $backendName
            status = $status
            error = $errorMessage
            artifact_dir = $artifactDir
            result_json = Join-Path $artifactDir "$runName.quit.json"
            log = Join-Path $artifactDir "$runName.quit.log"
            auto_press_sequence = $sequence
            app_hash = if ($quit -and $quit.app_hash) { $quit.app_hash } else { $appHash }
            compat_profile = if ($quit) { $quit.compat_profile } else { "" }
            effective_backend = if ($quit) { $quit.effective_backend } else { "" }
            saw_runtime_start = if ($quit) { $quit.saw_runtime_start } else { $false }
            natural_exit = if ($quit) { $quit.natural_exit } else { $false }
            clean_guest_exit = if ($quit) { $quit.clean_guest_exit } else { $false }
            return_to_title = if ($quit) { $quit.return_to_title } else { $false }
            title_frame_hash = if ($quit) { $quit.title_frame_hash } else { "" }
            last_frame_hash = if ($quit -and $quit.last_dumped_frame) { $quit.last_dumped_frame.ahash } else { "" }
            stopped_by_harness = if ($quit) { $quit.stopped_by_harness } else { $null }
            no_residual_process = if ($quit) { $quit.no_residual_process } else { $false }
            saw_guest_exit = if ($quit) { $quit.saw_guest_exit } else { $false }
            saw_task_stop = if ($quit) { $quit.saw_task_stop } else { $false }
            promoted_task_stop_ras = if ($quit -and $quit.promoted_task_stop_ras) { ($quit.promoted_task_stop_ras -join ",") } else { "" }
            unpromoted_task_stop_ras = if ($quit -and $quit.unpromoted_task_stop_ras) { ($quit.unpromoted_task_stop_ras -join ",") } else { "" }
        }
    }
}

$csvPath = Join-Path $OutputDir "$runStamp-summary.csv"
$jsonSummaryPath = Join-Path $OutputDir "$runStamp-summary.json"
$markdownPath = Join-Path $OutputDir "$runStamp-summary.md"

$results | Export-Csv -LiteralPath $csvPath -NoTypeInformation -Encoding UTF8
$results | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $jsonSummaryPath -Encoding UTF8

$lines = @()
$lines += "# Dingoo Sample Quit Baseline"
$lines += ""
$lines += "- Date: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')"
$lines += "- Sample directory: ``$SampleDir``"
$lines += "- Build directory: ``$BuildDir``"
$lines += "- Timeout per run: ${TimeoutSeconds}s"
$lines += "- Backends: $($backends -join ', ')"
$defaultSequenceText = if ($DefaultAutoPressSequence) { "``$DefaultAutoPressSequence``" } else { "(none)" }
$sequenceCsvText = if ($SequenceCsv) { "``$SequenceCsv``" } else { "(none)" }
$lines += "- Default AutoPressSequence: $defaultSequenceText"
$lines += "- Sequence CSV: $sequenceCsvText"
$lines += ""
$lines += "| Sample | Backend | Status | Natural exit | Return to title | Guest exit | Task stop RA | Sequence | Artifact | Notes |"
$lines += "|---|---|---|---|---|---|---|---|---|---|"
foreach ($row in $results) {
    $guestExit = if ($row.saw_guest_exit) { "yes" } else { "no" }
    $natural = if ($row.natural_exit) { "yes" } else { "no" }
    $returnToTitle = if ($row.return_to_title) { "yes" } else { "no" }
    $taskRa = if ($row.promoted_task_stop_ras) {
        "promoted $($row.promoted_task_stop_ras)"
    } elseif ($row.unpromoted_task_stop_ras) {
        "unpromoted $($row.unpromoted_task_stop_ras)"
    } else {
        "-"
    }
    $artifactName = Split-Path -Leaf $row.artifact_dir
    $notes = if ($row.error) {
        $row.error.Replace("|", "/")
    } elseif ($row.return_to_title) {
        "quit path returned to title"
    } elseif ($row.stopped_by_harness) {
        "stopped by harness after timeout"
    } elseif (!$row.no_residual_process) {
        "residual process detected"
    } else {
        ""
    }
    $sequenceText = if ($row.auto_press_sequence) { "``$($row.auto_press_sequence)``" } else { "(none)" }
    $lines += "| $($row.sample) | $($row.backend) | $($row.status) | $natural | $returnToTitle | $guestExit | $taskRa | $sequenceText | ``$artifactName`` | $notes |"
}

$lines += ""
$lines += "## Interpretation"
$lines += ""
$lines += "- `Natural exit=yes` means the emulator process ended before the harness timeout."
$lines += "- `Return to title=yes` means the final dumped frame matched the supplied title-screen hash. This is a captured game-level quit, not a process exit."
$lines += "- `Guest exit=yes` means the game reached an HLE guest-exit path such as `vxGoHome`, `abort`, or a promoted `OSTaskDel`."
$lines += "- `stopped by harness after timeout` is a failure for in-game quit validation; it only proves the test cleaned up the process."
$lines += "- Add a hash + return-address exit promotion only after a run shows the final quit action reaches an unpromoted `hle: task stop` line."

Set-Content -LiteralPath $markdownPath -Value $lines -Encoding UTF8

[pscustomobject]@{
    samples = $apps.Count
    backends = $backends
    results = $results.Count
    csv = $csvPath
    json = $jsonSummaryPath
    markdown = $markdownPath
} | ConvertTo-Json
