[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$baseline = Join-Path $root 'reverse\R3_keyboard_engine_diag_2026-04-10.txt'
$overlay = Join-Path $root 'reverse\R3_keyboard_engine_diag_overlay_split_2026-04-10.txt'
$compare = Join-Path $root 'reverse\R3_keyboard_engine_compare_split_2026-04-10.txt'

& (Join-Path $PSScriptRoot 'run_keyboard_engine_diag.ps1')
& (Join-Path $PSScriptRoot 'run_keyboard_engine_diag.ps1') -SplitOnlyOverlay
& python -X utf8 (Join-Path $PSScriptRoot 'compare_keyboard_engine_diag.py') `
  --baseline $baseline `
  --overlay $overlay `
  --output $compare `
  --top 30
