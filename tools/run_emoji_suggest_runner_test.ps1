$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$cpp = Join-Path $repo 'entry\src\main\cpp'
$test = Join-Path $cpp 'build_test\test_emoji_suggest_runner.cpp'
$engine = Join-Path $cpp 'hosdict_engine'
$build = Join-Path $env:TEMP 'hoskey-emoji-suggest-runner'
$exe = Join-Path $build 'test_emoji_suggest_runner.exe'

New-Item -ItemType Directory -Force -Path $build | Out-Null

& g++ -std=gnu++17 -O2 `
    "-I$cpp" "-I$engine" `
    $test `
    (Join-Path $engine 'emoji_suggest_runner.cpp') `
    (Join-Path $engine 'context_emoji_model.cpp') `
    (Join-Path $engine 'ynnlm_bundle.cpp') `
    (Join-Path $engine 'text_utils.cpp') `
    -o $exe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $exe
exit $LASTEXITCODE
