$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$cpp = Join-Path $repo 'entry\src\main\cpp'
$test = Join-Path $cpp 'build_test\test_autocc_model_runner.cpp'
$assets = 'C:\Users\Kharki\Desktop\yandex_extracted\extracted_sections'
$schema = Join-Path $assets 'features_autocc.json'
$default = Join-Path $assets 'autocc_default.hobl'
$k13321 = Join-Path $assets 'autocc_KEYBOARD-13321.hobl'
$k13421 = Join-Path $assets 'autocc_KEYBOARD-13421.hobl'
$build = Join-Path $env:TEMP 'hoskey-autocc-model-runner'
$exe = Join-Path $build 'test_autocc_model_runner.exe'

New-Item -ItemType Directory -Force -Path $build | Out-Null

& g++ -std=gnu++17 -O0 -g `
    "-I$cpp" "-I$(Join-Path $cpp 'hosdict_engine')" `
    $test `
    (Join-Path $cpp 'hosdict_engine\autocc_model_runner.cpp') `
    (Join-Path $cpp 'hosdict_engine\oblivious_tree_model.cpp') `
    -o $exe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $exe $schema $default $k13321 $k13421
exit $LASTEXITCODE
