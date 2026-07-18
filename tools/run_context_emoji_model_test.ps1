$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$cpp = Join-Path $repo 'entry\src\main\cpp'
$test = Join-Path $cpp 'build_test\test_context_emoji_model.cpp'
$model = Join-Path $cpp 'hosdict_engine\context_emoji_model.cpp'
$build = Join-Path $env:TEMP 'hoskey-context-emoji-model'
$exe = Join-Path $build 'test_context_emoji_model.exe'
$asset = 'C:\Users\Kharki\Desktop\yandex_extracted\extracted_sections\emoji_model'

New-Item -ItemType Directory -Force -Path $build | Out-Null

& g++ -std=gnu++17 -O2 `
    "-I$cpp" "-I$(Join-Path $cpp 'hosdict_engine')" `
    $test $model -o $exe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $exe $asset
exit $LASTEXITCODE
