[CmdletBinding()]
param(
  [string]$PackageDir
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$srcDir = Join-Path $root 'entry\src\main\cpp'
$engineDir = Join-Path $srcDir 'hosdict_engine'
$src = Join-Path $PSScriptRoot 'one_nc7_trie_acceptance.cpp'
$exe = Join-Path $PSScriptRoot 'one_nc7_trie_acceptance.exe'

if (-not $PackageDir) {
  $PackageDir = Join-Path $root 'entry\src\main\resources\rawfile\dict'
}

$sources = @(
  $src,
  (Join-Path $engineDir 'hosdict_container.cpp'),
  (Join-Path $engineDir 'one_nc7_trie.cpp')
)

g++ -std=c++20 -I $srcDir -I $engineDir @sources -o $exe
if ($LASTEXITCODE -ne 0) {
  throw "g++ OneNc7Trie acceptance build failed"
}

try {
  & $exe $PackageDir
  if ($LASTEXITCODE -ne 0) {
    throw "OneNc7Trie acceptance failed with exit code $LASTEXITCODE"
  }
} finally {
  Remove-Item -LiteralPath $exe -ErrorAction SilentlyContinue
}
