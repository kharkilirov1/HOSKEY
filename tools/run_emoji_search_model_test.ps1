$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$cpp = Join-Path $repo 'entry\src\main\cpp'
$test = Join-Path $cpp 'build_test\test_emoji_search_model.cpp'
$model = Join-Path $cpp 'hosdict_engine\emoji_search_model.cpp'
$text = Join-Path $cpp 'hosdict_engine\text_utils.cpp'
$asset = 'C:\Users\Kharki\Desktop\yandex_extracted\extracted_sections\search_emoji_model'
$build = Join-Path $env:TEMP 'hoskey-emoji-search-model'
$exe = Join-Path $build 'test_emoji_search_model.exe'

New-Item -ItemType Directory -Force -Path $build | Out-Null

& g++ -std=gnu++17 -O2 `
    "-I$cpp" "-I$(Join-Path $cpp 'hosdict_engine')" `
    $test $model $text -o $exe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $exe $asset
exit $LASTEXITCODE
