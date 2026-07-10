[CmdletBinding()]
param(
  [switch]$Overlay,
  [switch]$SplitOnlyOverlay,
  [switch]$VerifiedAllowlist
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$srcDir = Join-Path $root 'entry\src\main\cpp'
$engineDir = Join-Path $srcDir 'hosdict_engine'
$src = Join-Path $PSScriptRoot 'keyboard_engine_diag.cpp'
$exe = Join-Path $PSScriptRoot 'keyboard_engine_diag.exe'
$reportName = if ($SplitOnlyOverlay) {
  'R3_keyboard_engine_diag_overlay_split_2026-04-10.txt'
} elseif ($Overlay) {
  'R3_keyboard_engine_diag_overlay_2026-04-10.txt'
} else {
  'R3_keyboard_engine_diag_2026-04-10.txt'
}
$report = Join-Path $root "reverse\\$reportName"
$packageDir = Join-Path $root 'entry\src\main\resources\rawfile\dict'

$sources = @(
  $src,
  (Join-Path $engineDir 'asset_manifest.cpp'),
  (Join-Path $engineDir 'loaders.cpp'),
  (Join-Path $engineDir 'hosdict_container.cpp'),
  (Join-Path $engineDir 'hosdict_package.cpp'),
  (Join-Path $engineDir 'keyboard_engine.cpp'),
  (Join-Path $engineDir 'predictor_engine.cpp'),
  (Join-Path $engineDir 'text_utils.cpp'),
  (Join-Path $engineDir 'policy_store.cpp'),
  (Join-Path $engineDir 'normdict_reader.cpp'),
  (Join-Path $engineDir 'lemmer.cpp'),
  (Join-Path $engineDir 'tfmtrie_decoder.cpp'),
  (Join-Path $engineDir 'one_nc7_trie.cpp'),
  (Join-Path $engineDir 'ml_ranker.cpp'),
  (Join-Path $engineDir 'neural_lm.cpp'),
  (Join-Path $engineDir 'succinct_trie.cpp')
)

g++ -std=c++20 -I $srcDir -I $engineDir @sources -o $exe
if ($LASTEXITCODE -ne 0) {
  throw "g++ build failed"
}

try {
  $mode = if ($SplitOnlyOverlay) {
    'split'
  } elseif ($Overlay) {
    'all'
  } else {
    'disabled'
  }
  $allowlist = if ($VerifiedAllowlist) { '1' } else { '0' }
  & $exe $packageDir $report $mode $allowlist
} finally {
  Remove-Item -LiteralPath $exe -ErrorAction SilentlyContinue
}
