$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
$hvigor = 'C:\Program Files\Huawei\DevEco Studio\tools\hvigor\bin\hvigorw.bat'
$javaHome = 'C:\Program Files\Huawei\DevEco Studio\jbr'
# DEVECO_SDK_HOME points at the SDK manager root (the directory containing the
# versioned/default package), not directly at one extracted SDK component.
$sdkHome = 'C:\Program Files\Huawei\DevEco Studio\sdk'

$env:JAVA_HOME = $javaHome
$env:DEVECO_SDK_HOME = $sdkHome
$env:Path = "$javaHome\bin;$env:Path"

Push-Location $projectRoot
try {
  & python (Join-Path $projectRoot 'tools\build_hosdict.py')
  if ($LASTEXITCODE -ne 0) {
    throw "build_hosdict.py failed with exit code $LASTEXITCODE"
  }
  $dictPath = Join-Path $projectRoot 'entry\src\main\resources\rawfile\dict\ru.hoskey.hosdict'
  $dictSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $dictPath).Hash.ToLowerInvariant()
  & python (Join-Path $projectRoot 'tools\strict_hosdict_gate.py') `
      --hosdict $dictPath `
      --expected-sha256 $dictSha256 `
      --allow-missing-variant-manifest
  if ($LASTEXITCODE -ne 0) {
    throw "strict_hosdict_gate.py failed with exit code $LASTEXITCODE"
  }

  & $hvigor --stop-daemon
  & $hvigor assembleApp -p product=default -p buildMode=debug
  $exitCode = $LASTEXITCODE
  if ($exitCode -eq 0) {
    & python (Join-Path $projectRoot 'tools\package_asset_gate.py')
    if ($LASTEXITCODE -ne 0) {
      throw "package_asset_gate.py failed with exit code $LASTEXITCODE"
    }
    $builtHap = Join-Path $projectRoot 'entry\build\default\outputs\default\app\entry-default.hap'
    & python (Join-Path $projectRoot 'tools\strict_hosdict_gate.py') `
        --hap $builtHap `
        --expected-sha256 $dictSha256 `
        --allow-missing-variant-manifest
    if ($LASTEXITCODE -ne 0) {
      throw "strict HAP .hosdict gate failed with exit code $LASTEXITCODE"
    }
  }
  Write-Host ''
  Write-Host 'Artifacts:'
  Write-Host '  APP:  build\outputs\default\HOSKEY-default-unsigned.app'
  Write-Host '  HAP:  entry\build\default\outputs\default\app\entry-default.hap'
  Write-Host '  HAPU: entry\build\default\outputs\default\entry-default-unsigned.hap'
}
finally {
  Pop-Location
}

exit $exitCode
