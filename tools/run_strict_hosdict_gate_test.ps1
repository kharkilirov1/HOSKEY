$ErrorActionPreference = 'Stop'

$tools = Split-Path -Parent $MyInvocation.MyCommand.Path
$repo = Split-Path -Parent $tools
$dict = Join-Path $repo 'entry\src\main\resources\rawfile\dict\ru.hoskey.hosdict'

Push-Location $tools
try {
  python -B -m unittest -v test_strict_hosdict_gate.py
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

  python -B strict_hosdict_gate.py `
      --hosdict $dict `
      --allow-missing-variant-manifest
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

  $previousErrorActionPreference = $ErrorActionPreference
  $ErrorActionPreference = 'Continue'
  try {
    $strictOutput = & python -B strict_hosdict_gate.py --hosdict $dict 2>&1
    $strictExit = $LASTEXITCODE
  } finally {
    $ErrorActionPreference = $previousErrorActionPreference
  }
  $strictOutput | Write-Output
  if ($strictExit -ne 2) {
    Write-Error "Expected strict provenance blocker exit 2, got $strictExit"
  }
  if (($strictOutput -join "`n") -notmatch 'variant manifest is absent') {
    Write-Error 'Strict blocker output did not identify the absent variant manifest'
  }

  Write-Output 'STRICT HOSDICT TEST OK: structural checks pass; provenance blocker is explicit'
} finally {
  Pop-Location
}

exit 0
