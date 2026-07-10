$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
$hvigor = 'C:\Program Files\Huawei\DevEco Studio\tools\hvigor\bin\hvigorw.bat'
$javaHome = 'C:\Program Files\Huawei\DevEco Studio\jbr'
$sdkHome = 'C:\Users\Kharki\DevEcoSDK\default'

$env:JAVA_HOME = $javaHome
$env:DEVECO_SDK_HOME = $sdkHome
$env:Path = "$javaHome\bin;$env:Path"

Push-Location $projectRoot
try {
  & python (Join-Path $projectRoot 'tools\build_hosdict.py')
  if ($LASTEXITCODE -ne 0) {
    throw "build_hosdict.py failed with exit code $LASTEXITCODE"
  }

  & $hvigor --stop-daemon
  & $hvigor assembleApp -p product=default -p buildMode=debug
  $exitCode = $LASTEXITCODE
  if ($exitCode -eq 0) {
    & python (Join-Path $projectRoot 'tools\package_asset_gate.py')
    if ($LASTEXITCODE -ne 0) {
      throw "package_asset_gate.py failed with exit code $LASTEXITCODE"
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
