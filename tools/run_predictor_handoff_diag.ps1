$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$src = Join-Path $PSScriptRoot 'predictor_handoff_diag.cpp'
$engineDir = Join-Path $root 'entry\src\main\cpp\hosdict_engine'
$exe = Join-Path $PSScriptRoot 'predictor_handoff_diag.exe'
$out = Join-Path $root 'reverse\R3_predictor_handoff_diag_2026-04-10.txt'

g++ -std=c++20 -I $engineDir $src (Join-Path $engineDir 'predictor_engine.cpp') -o $exe
if ($LASTEXITCODE -ne 0) {
  throw "g++ build failed"
}

try {
  & $exe $out
} finally {
  Remove-Item -LiteralPath $exe -ErrorAction SilentlyContinue
}
