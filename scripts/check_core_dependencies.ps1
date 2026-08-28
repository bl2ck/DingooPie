param(
    [string]$ProjectRoot = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = 'Stop'
$coreRoot = Join-Path $ProjectRoot 'native\core'
$violations = New-Object System.Collections.Generic.List[string]
$allowedIncludes = @{
    'native/core/shared/game/game_runtime.cpp' = @('app/', 'cc/', 'frontend/')
}

function Test-Includes {
    param([string]$RelativeRoot, [string[]]$ForbiddenPrefixes)
    $root = Join-Path $coreRoot $RelativeRoot
    if (!(Test-Path -LiteralPath $root)) {
        return
    }

    Get-ChildItem $root -Recurse -File -Include *.h,*.hpp,*.c,*.cc,*.cpp |
        ForEach-Object {
            $file = $_
            $relativeFile = $file.FullName.Substring(
                $ProjectRoot.Length + 1).Replace('\', '/')
            $lineNumber = 0
            Get-Content -LiteralPath $file.FullName | ForEach-Object {
                $lineNumber++
                if ($_ -notmatch '^\s*#\s*include\s*[<"]([^>"]+)[>"]') {
                    return
                }
                $include = $Matches[1].Replace('\', '/')
                foreach ($prefix in $ForbiddenPrefixes) {
                    if (!$include.StartsWith($prefix,
                            [System.StringComparison]::OrdinalIgnoreCase)) {
                        continue
                    }
                    $allowed = $allowedIncludes[$relativeFile]
                    if ($allowed -and ($allowed -contains $include -or
                            $allowed -contains $prefix)) {
                        continue
                    }
                    $violations.Add(
                        "${relativeFile}:$lineNumber forbids include '$include'")
                }
            }
        }
}

Test-Includes 'app' @('cc/')
Test-Includes 'cc' @('app/')
Test-Includes 'shared' @('app/', 'cc/', 'frontend/')
Test-Includes 'frontend' @('app/', 'cc/')
Test-Includes 'config' @('app/', 'cc/', 'frontend/')

Get-ChildItem $coreRoot -Recurse -File -Include *.h,*.hpp,*.c,*.cc,*.cpp |
    ForEach-Object {
        $file = $_
        $lineNumber = 0
        Get-Content -LiteralPath $file.FullName | ForEach-Object {
            $lineNumber++
            if ($_ -match '^\s*#\s*include\s*[<"]([^>"]+\.cpp)[>"]') {
                $relative = $file.FullName.Substring($ProjectRoot.Length + 1)
                $violations.Add(
                    "${relative}:$lineNumber must not include source file '$($Matches[1])'")
            }
        }
    }

if ($violations.Count -gt 0) {
    $violations | ForEach-Object { Write-Error $_ }
    exit 1
}

Write-Output 'Core dependency validation passed.'
