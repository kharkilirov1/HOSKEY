[CmdletBinding()]
param(
  [string]$PackageDir
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$srcDir = Join-Path $root 'entry\src\main\cpp'
$engineDir = Join-Path $srcDir 'hosdict_engine'
$src = Join-Path $PSScriptRoot 'engine_acceptance.cpp'
$exe = Join-Path $PSScriptRoot 'engine_acceptance.exe'

if (-not $PackageDir) {
  $PackageDir = Join-Path $root 'entry\src\main\resources\rawfile\dict'
}

$sources = @(
  $src,
  (Join-Path $engineDir 'asset_manifest.cpp'),
  (Join-Path $engineDir 'loaders.cpp'),
  (Join-Path $engineDir 'hosdict_container.cpp'),
  (Join-Path $engineDir 'hosdict_package.cpp'),
  (Join-Path $engineDir 'char_trigram_model.cpp'),
  (Join-Path $engineDir 'context_emoji_model.cpp'),
  (Join-Path $engineDir 'emoji_suggest_runner.cpp'),
  (Join-Path $engineDir 'emoji_search_model.cpp'),
  (Join-Path $engineDir 'keyboard_engine.cpp'),
  (Join-Path $engineDir 'predictor_engine.cpp'),
  (Join-Path $engineDir 'text_utils.cpp'),
  (Join-Path $engineDir 'swipe_decoder.cpp'),
  (Join-Path $engineDir 'policy_store.cpp'),
  (Join-Path $engineDir 'normdict_reader.cpp'),
  (Join-Path $engineDir 'lemmer.cpp'),
  (Join-Path $engineDir 'tfmtrie_decoder.cpp'),
  (Join-Path $engineDir 'one_nc7_trie.cpp'),
  (Join-Path $engineDir 'oblivious_tree_model.cpp'),
  (Join-Path $engineDir 'autocc_model_runner.cpp'),
  (Join-Path $engineDir 'tap_model_bundle.cpp'),
  (Join-Path $engineDir 'tap_model_runner.cpp'),
  (Join-Path $engineDir 'ml_ranker.cpp'),
  (Join-Path $engineDir 'neural_lm.cpp'),
  (Join-Path $engineDir 'rnnt_swipe_model.cpp'),
  (Join-Path $engineDir 'ynnlm_bundle.cpp'),
    (Join-Path $engineDir 'rwkv_encoder.cpp'),
    (Join-Path $engineDir 'ynnlm_char_decoder.cpp'),
    (Join-Path $engineDir 'ynnlm_word_chain.cpp'),
  (Join-Path $engineDir 'succinct_trie.cpp')
)

g++ -std=c++20 -I $srcDir -I $engineDir @sources -o $exe
if ($LASTEXITCODE -ne 0) {
  throw "g++ acceptance build failed"
}

try {
  & $exe $PackageDir
  if ($LASTEXITCODE -ne 0) {
    throw "engine acceptance failed with exit code $LASTEXITCODE"
  }
} finally {
  Remove-Item -LiteralPath $exe -ErrorAction SilentlyContinue
}
