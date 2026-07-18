$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$cpp = Join-Path $repo 'entry\src\main\cpp'
$test = Join-Path $cpp 'build_test\test_tap_model_package.cpp'
$package = Join-Path $repo 'entry\src\main\resources\rawfile\dict\ru.hoskey.hosdict'
$build = Join-Path $env:TEMP 'hoskey-tap-model-package'
$exe = Join-Path $build 'test_tap_model_package.exe'

New-Item -ItemType Directory -Force -Path $build | Out-Null

& g++ -std=gnu++17 -O0 -g `
    "-I$cpp" "-I$(Join-Path $cpp 'hosdict_engine')" `
    $test `
    (Join-Path $cpp 'hosdict_engine\hosdict_container.cpp') `
    (Join-Path $cpp 'hosdict_engine\tap_model_bundle.cpp') `
    (Join-Path $cpp 'hosdict_engine\tap_model_runner.cpp') `
    (Join-Path $cpp 'hosdict_engine\oblivious_tree_model.cpp') `
    -o $exe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $exe $package
exit $LASTEXITCODE
