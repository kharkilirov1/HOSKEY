$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$cpp = Join-Path $repo 'entry\src\main\cpp'
$test = Join-Path $cpp 'build_test\test_neural_ranker_features.cpp'
$build = Join-Path $env:TEMP 'hoskey-neural-ranker-features'
$exe = Join-Path $build 'test_neural_ranker_features.exe'
$dict = Join-Path $repo 'entry\src\main\resources\rawfile\dict\ru.hoskey.hosdict'

New-Item -ItemType Directory -Force -Path $build | Out-Null

& g++ -std=gnu++17 -O0 -g `
    "-I$cpp" "-I$(Join-Path $cpp 'hosdict_engine')" `
    $test `
    (Join-Path $cpp 'hosdict_engine\hosdict_container.cpp') `
    (Join-Path $cpp 'hosdict_engine\char_trigram_model.cpp') `
    (Join-Path $cpp 'hosdict_engine\ml_ranker.cpp') `
    (Join-Path $cpp 'hosdict_engine\rnnt_swipe_model.cpp') `
    (Join-Path $cpp 'hosdict_engine\tfmtrie_decoder.cpp') `
    (Join-Path $cpp 'hosdict_engine\text_utils.cpp') `
    (Join-Path $cpp 'hosdict_engine\swipe_decoder.cpp') `
    (Join-Path $cpp 'hosdict_engine\rwkv_encoder.cpp') `
    (Join-Path $cpp 'hosdict_engine\ynnlm_char_decoder.cpp') `
    (Join-Path $cpp 'hosdict_engine\ynnlm_bundle.cpp') `
    (Join-Path $cpp 'hosdict_engine\ynnlm_word_chain.cpp') `
    -o $exe -lstdc++fs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $exe $dict
exit $LASTEXITCODE
