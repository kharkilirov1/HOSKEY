$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$cpp = Join-Path $repo 'entry\src\main\cpp'
$test = Join-Path $cpp 'build_test\test_oblivious_tree_model.cpp'
$model = Join-Path $cpp 'hosdict_engine\oblivious_tree_model.cpp'
$build = Join-Path $env:TEMP 'hoskey-oblivious-tree-model'
$exe = Join-Path $build 'test_oblivious_tree_model.exe'

New-Item -ItemType Directory -Force -Path $build | Out-Null

& g++ -std=gnu++17 -O0 -g `
    "-I$cpp" "-I$(Join-Path $cpp 'hosdict_engine')" `
    $test $model -o $exe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $exe @args
exit $LASTEXITCODE
