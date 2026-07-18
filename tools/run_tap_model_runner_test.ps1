$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$cpp = Join-Path $repo 'entry\src\main\cpp'
$test = Join-Path $cpp 'build_test\test_tap_model_runner.cpp'
$bundle = Join-Path $repo 'build\reverse\tap_models_default_1225'
$schema = Join-Path $repo 'build\reverse\features_tap_model_1225.json'
$ranker = Join-Path $repo 'build\neural_oracle_1225\tap.default.ranker.hobl'
$classifier = Join-Path $repo 'build\neural_oracle_1225\tap.default.classifier.hobl'
$build = Join-Path $env:TEMP 'hoskey-tap-model-runner'
$exe = Join-Path $build 'test_tap_model_runner.exe'

New-Item -ItemType Directory -Force -Path $build | Out-Null

& g++ -std=gnu++17 -O0 -g `
    "-I$cpp" "-I$(Join-Path $cpp 'hosdict_engine')" `
    $test `
    (Join-Path $cpp 'hosdict_engine\tap_model_bundle.cpp') `
    (Join-Path $cpp 'hosdict_engine\tap_model_runner.cpp') `
    (Join-Path $cpp 'hosdict_engine\oblivious_tree_model.cpp') `
    -o $exe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $exe $bundle $schema $ranker $classifier
exit $LASTEXITCODE
