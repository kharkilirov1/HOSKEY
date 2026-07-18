[CmdletBinding()]
param(
  [string]$Source = 'C:\Users\Kharki\Desktop\yandex_extracted\extracted_sections\neural_model',
  [string]$SplitDir = 'C:\Users\Kharki\Desktop\yandex_extracted\tflite_ynnlm_split',
  [string]$OutputDir = 'C:\Users\Kharki\Desktop\yandex_extracted\ms_models',
  [string]$MindSporeLiteDir = '/tmp/mslite_260/mindspore-lite-2.6.0-linux-x64',
  [switch]$DownloadMindSporeLite
)

$ErrorActionPreference = 'Stop'

if (-not (Get-Command wsl -ErrorAction SilentlyContinue)) {
  throw 'WSL is required: converter_lite is the Linux x64 MindSpore Lite tool.'
}
if (-not (Get-Command python -ErrorAction SilentlyContinue)) {
  throw 'python is required to split the ynnlm_v1 embedded TFL3 models.'
}

New-Item -ItemType Directory -Force -Path $SplitDir, $OutputDir | Out-Null

$extractor = @'
import re
import sys
from pathlib import Path

src = Path(sys.argv[1])
out = Path(sys.argv[2])
out.mkdir(parents=True, exist_ok=True)
data = src.read_bytes()
starts = [m.start() - 4 for m in re.finditer(b"TFL3", data) if m.start() >= 4]
if len(starts) not in (8, 10):
    raise SystemExit(
        f"expected legacy 8-model or Yandex 122.5 10-model wrapper, "
        f"found {len(starts)} at {starts}"
    )
for i, start in enumerate(starts):
    end = starts[i + 1] if i + 1 < len(starts) else len(data)
    blob = data[start:end]
    if len(blob) < 8 or blob[4:8] != b"TFL3":
        raise SystemExit(f"bad TFL3 split at index {i}, offset {start}")
    (out / f"ynnlm_m{i}.tflite").write_bytes(blob)
    print(f"split m{i}: offset={start} size={len(blob)}")
(out / "layout.txt").write_text(
    "live_1225_10\n" if len(starts) == 10 else "legacy_8\n",
    encoding="utf-8",
)
'@

$extractorPath = Join-Path $env:TEMP 'hoskey_extract_ynnlm_tfl3.py'
[System.IO.File]::WriteAllText($extractorPath, $extractor, [System.Text.Encoding]::UTF8)
python $extractorPath $Source $SplitDir
$charBuilder = Join-Path $PSScriptRoot 'build_ynnlm_char_decoder.py'
python $charBuilder `
  (Join-Path $SplitDir 'ynnlm_m0.tflite') `
  (Join-Path $OutputDir 'char_dec_exact.bin')
if ($LASTEXITCODE -ne 0) {
  throw "exact YNNLM char decoder build failed with exit code $LASTEXITCODE"
}

function ConvertTo-WslPath([string]$Path) {
  $full = [System.IO.Path]::GetFullPath($Path)
  if ($full -match '^([A-Za-z]):\\(.*)$') {
    return "/mnt/$($Matches[1].ToLower())/" + ($Matches[2] -replace '\\', '/')
  }
  $converted = wsl wslpath -a $full
  if (-not $converted) {
    throw "wslpath failed for '$Path'"
  }
  $converted.Trim()
}

$splitWsl = ConvertTo-WslPath $SplitDir
$outWsl = ConvertTo-WslPath $OutputDir
$rewriteWsl = ConvertTo-WslPath (Join-Path $PSScriptRoot 'rewrite_ynnlm_encoder_means.py')
$download = if ($DownloadMindSporeLite) { '1' } else { '0' }
$layout = (Get-Content -LiteralPath (Join-Path $SplitDir 'layout.txt') -Raw).Trim()
if ($layout -eq 'live_1225_10') {
  $encoderIndex = 9
  $modelSpecs = @(
    '0 char_dec', '1 subword_dec', '2 word_dec',
    '3 emb_age', '4 emb_app', '5 emb_sex', '6 word_emb',
    '7 subword_enc_dec', '8 word_enc_dec', '9 enc'
  )
} elseif ($layout -eq 'legacy_8') {
  $encoderIndex = 7
  $modelSpecs = @(
    '0 char_dec', '1 subword_dec', '2 word_dec', '3 emb_app',
    '4 word_emb', '5 subword_enc_dec', '6 word_enc_dec', '7 enc'
  )
} else {
  throw "unknown YNNLM layout '$layout'"
}
$modelSpecsBash = $modelSpecs -join "`n"
$modelNames = $modelSpecs | ForEach-Object { ($_ -split ' ', 2)[1] }
$modelNamesBash = $modelNames -join ' '

$bash = @"
set -euo pipefail
MSL="$MindSporeLiteDir"
CONV="`$MSL/tools/converter/converter/converter_lite"
if [ ! -x "`$CONV" ]; then
  if [ "$download" != "1" ]; then
    echo "converter_lite not found at `$CONV" >&2
    echo "Pass -DownloadMindSporeLite or set -MindSporeLiteDir." >&2
    exit 2
  fi
  mkdir -p /tmp/mslite_260
  cd /tmp/mslite_260
  if [ ! -f mindspore-lite-2.6.0-linux-x64.tar.gz ]; then
    curl -L --retry 3 -o mindspore-lite-2.6.0-linux-x64.tar.gz \
      https://ms-release.obs.cn-north-4.myhuaweicloud.com/2.6.0/MindSpore/lite/release/linux/x86_64/mindspore-lite-2.6.0-linux-x64.tar.gz
  fi
  if [ ! -d mindspore-lite-2.6.0-linux-x64 ]; then
    tar -xzf mindspore-lite-2.6.0-linux-x64.tar.gz
  fi
fi
export LD_LIBRARY_PATH="`$MSL/tools/converter/lib:`$MSL/runtime/lib:`$MSL/runtime/third_party/dnnl:`$MSL/runtime/third_party/glog:`$MSL/runtime/third_party/libjpeg-turbo/lib:`$MSL/tools/converter/third_party/protobuf/lib:`$MSL/tools/converter/third_party/securec/lib:`${LD_LIBRARY_PATH:-}"
mkdir -p "$outWsl"
TF_CPP_MIN_LOG_LEVEL=3 python3 "$rewriteWsl" \
  "$splitWsl/ynnlm_m$encoderIndex.tflite" "$splitWsl/ynnlm_m${encoderIndex}_ohos.tflite"
while IFS= read -r spec; do
  [ -n "`$spec" ] || continue
  set -- `$spec
  idx=`$1
  name=`$2
  input="$splitWsl/ynnlm_m`$idx.tflite"
  if [ "`$name" = "enc" ]; then
    input="$splitWsl/ynnlm_m${encoderIndex}_ohos.tflite"
  fi
  echo "convert `$input -> `$name.ms"
  "`$CONV" --fmk=TFLITE --modelFile="`$input" \
    --outputFile="$outWsl/`$name" --saveType=MINDIR_LITE --optimize=general
  test -f "$outWsl/`$name.ms"
done <<'HOSKEY_YNNLM_SPECS'
$modelSpecsBash
HOSKEY_YNNLM_SPECS

# Converter success alone is insufficient: the old encoder converted but failed
# at NNACL PrepareKernels.  Compile and execute every emitted graph once.
BENCH="`$MSL/tools/benchmark/benchmark"
if [ ! -x "`$BENCH" ]; then
  echo "benchmark not found at `$BENCH" >&2
  exit 3
fi
for name in $modelNamesBash; do
  echo "benchmark `$name.ms"
  "`$BENCH" --modelFile="$outWsl/`$name.ms" --loopCount=1 \
    --warmUpLoopCount=0 --numThreads=2 >/tmp/hoskey_benchmark_`$name.log 2>&1
  grep -q "Run Benchmark .* Success" /tmp/hoskey_benchmark_`$name.log
done
"@

$bashPath = Join-Path $env:TEMP 'hoskey_convert_ynnlm_models.sh'
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText($bashPath, $bash.Replace("`r`n", "`n"), $utf8NoBom)
wsl bash (ConvertTo-WslPath $bashPath)
if ($LASTEXITCODE -ne 0) {
  throw "ynnlm conversion or benchmark failed with exit code $LASTEXITCODE"
}

Get-ChildItem $OutputDir -Filter *.ms |
  Where-Object { $_.BaseName -in $modelNames } |
  Sort-Object Name |
  Select-Object Name, Length, LastWriteTime
