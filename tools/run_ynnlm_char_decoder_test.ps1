$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$cpp = Join-Path $repo 'entry\src\main\cpp'
$test = Join-Path $cpp 'build_test\test_ynnlm_char_decoder.cpp'
$build = Join-Path $env:TEMP 'hoskey-ynnlm-char-decoder'
$exe = Join-Path $build 'test_ynnlm_char_decoder.exe'
$model = Join-Path $build 'char_dec_exact.bin'
$tflite = Join-Path $repo 'build\context_emoji_exact\k13321_nnlm_tflite\ynnlm_m0_char_dec.tflite'
$state = Join-Path $repo 'build\reverse\tflite_bridge_zero.bin'

New-Item -ItemType Directory -Force -Path $build | Out-Null
python (Join-Path $repo 'tools\build_ynnlm_char_decoder.py') $tflite $model
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& g++ -std=gnu++17 -O2 `
    "-I$cpp" "-I$(Join-Path $cpp 'hosdict_engine')" `
    $test `
    (Join-Path $cpp 'hosdict_engine\ynnlm_char_decoder.cpp') `
    -o $exe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $exe $model $state
exit $LASTEXITCODE
