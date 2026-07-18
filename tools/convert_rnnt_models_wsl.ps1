[CmdletBinding()]
param(
  [string]$Source = 'C:\Users\Kharki\Desktop\yandex_extracted\extracted_sections\rnnt_model',
  [string]$SplitDir = 'C:\Users\Kharki\Desktop\yandex_extracted\tflite_rnnt_split',
  [string]$OutputDir = 'C:\Users\Kharki\Desktop\yandex_extracted\ms_models',
  [string]$MindSporeLiteDir = '',
  [switch]$DownloadMindSporeLite
)

$ErrorActionPreference = 'Stop'

if (-not (Get-Command wsl -ErrorAction SilentlyContinue)) {
  throw 'WSL is required: converter_lite is a Linux x64 tool.'
}
if (-not (Get-Command python -ErrorAction SilentlyContinue)) {
  throw 'python is required to split the concatenated RNNT FlatBuffers.'
}
if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) {
  throw "RNNT source bundle not found: $Source"
}

New-Item -ItemType Directory -Force -Path $SplitDir, $OutputDir | Out-Null

$extractor = @'
import re
import sys
from pathlib import Path

source = Path(sys.argv[1])
output = Path(sys.argv[2])
data = source.read_bytes()
starts = [match.start() - 4 for match in re.finditer(b"TFL3", data)
          if match.start() >= 4]
names = [
    "joint", "pos_embedding",
    "post0", "post1", "post2", "post3",
    "pre0", "pre1", "pre2", "pre3",
    "sampling",
]
if len(starts) != len(names):
    raise SystemExit(f"expected 11 RNNT TFL3 models, found {len(starts)} at {starts}")
output.mkdir(parents=True, exist_ok=True)
for index, (start, name) in enumerate(zip(starts, names)):
    end = starts[index + 1] if index + 1 < len(starts) else len(data)
    blob = data[start:end]
    if len(blob) < 8 or blob[4:8] != b"TFL3":
        raise SystemExit(f"bad RNNT TFL3 split at index {index}, offset {start}")
    path = output / f"rnnt_{name}.tflite"
    path.write_bytes(blob)
    print(f"split {name}: offset={start} size={len(blob)}")
'@

$extractorPath = Join-Path $env:TEMP 'hoskey_extract_rnnt_tfl3.py'
[System.IO.File]::WriteAllText(
  $extractorPath, $extractor, [System.Text.Encoding]::UTF8)
python $extractorPath $Source $SplitDir
if ($LASTEXITCODE -ne 0) {
  throw "RNNT split failed with exit code $LASTEXITCODE"
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
$rewriteWsl = ConvertTo-WslPath (
  Join-Path $PSScriptRoot 'rewrite_rnnt_reduce_any.py')
$download = if ($DownloadMindSporeLite) { '1' } else { '0' }

$bash = @'
set -euo pipefail
DOWNLOAD="__DOWNLOAD__"
MSL_INPUT="__MSL__"
CACHE="$HOME/.cache/hoskey/mslite260-device"
ARCHIVE="$CACHE/mindspore-lite-2.6.0-linux-x64.tar.gz"
DEFAULT_MSL="$CACHE/mindspore-lite-2.6.0-linux-x64"
URL="https://ms-release.obs.cn-north-4.myhuaweicloud.com/2.6.0/MindSpore/lite/release/linux/x86_64/mindspore-lite-2.6.0-linux-x64.tar.gz"
SHA="dc70d965c440ecab9692fd2dd7362e967d79e92ccb0ff632ac021d25d2af9c9e"
MSL="${MSL_INPUT:-$DEFAULT_MSL}"
CONV="$MSL/tools/converter/converter/converter_lite"
BENCH="$MSL/tools/benchmark/benchmark"
if [ ! -x "$CONV" ] || [ ! -x "$BENCH" ]; then
  if [ "$DOWNLOAD" != "1" ]; then
    echo "MindSpore Lite converter/benchmark not found under $MSL" >&2
    echo "Pass -DownloadMindSporeLite or -MindSporeLiteDir." >&2
    exit 2
  fi
  mkdir -p "$CACHE"
  if [ ! -f "$ARCHIVE" ]; then
    curl -fsSL --retry 3 -o "$ARCHIVE" "$URL"
  fi
  echo "$SHA  $ARCHIVE" | sha256sum -c -
  rm -rf "$DEFAULT_MSL"
  tar -xzf "$ARCHIVE" -C "$CACHE"
  MSL="$DEFAULT_MSL"
  CONV="$MSL/tools/converter/converter/converter_lite"
  BENCH="$MSL/tools/benchmark/benchmark"
fi

export LD_LIBRARY_PATH="$MSL/tools/converter/lib:$MSL/runtime/lib:$MSL/runtime/third_party/dnnl:$MSL/runtime/third_party/glog:$MSL/runtime/third_party/libjpeg-turbo/lib:$MSL/tools/converter/third_party/protobuf/lib:$MSL/tools/converter/third_party/securec/lib:${LD_LIBRARY_PATH:-}"
IN="__SPLIT__"
OUT="__OUTPUT__"
REWRITE="__REWRITE__"
mkdir -p "$OUT"

# Load TensorFlow's generated schema once; five separate interpreter startups
# add minutes on WSL.  These rewrites are shape-guarded in the Python module.
TF_CPP_MIN_LOG_LEVEL=3 python3 - "$REWRITE" "$IN" <<'PY'
import importlib.util
import sys
from pathlib import Path

spec = importlib.util.spec_from_file_location("rnnt_rewrite", sys.argv[1])
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
root = Path(sys.argv[2])
for name, reduce_count, split_count in [
    ("post0", 3, 2), ("post1", 3, 2),
    ("post2", 3, 2), ("post3", 3, 2),
    ("sampling", 2, 0),
]:
    result = module.rewrite_rnnt(
        root / f"rnnt_{name}.tflite",
        root / f"rnnt_{name}_ohos.tflite",
        reduce_count,
        split_count,
    )
    print(f"rewrite {name}: reduce_any={result[0]} split_v={result[1]}")
PY

# The 3.5 KB position graph hangs in the 2.6 NNACL SIN/COS path.  It is not a
# learned component: HOSKEY computes the exact standard 100-wide sinusoidal
# embedding in C++.  The ten learned graphs below are converted and executed.
for name in joint post0 post1 post2 post3 pre0 pre1 pre2 pre3 sampling; do
  input="$IN/rnnt_$name.tflite"
  case "$name" in post*|sampling) input="$IN/rnnt_${name}_ohos.tflite";; esac
  echo "convert $name"
  convert_log="/tmp/hoskey_rnnt_convert_$name.log"
  if ! "$CONV" --fmk=TFLITE --modelFile="$input" \
      --outputFile="$OUT/rnnt_$name" --saveType=MINDIR_LITE \
      --optimize=general >"$convert_log" 2>&1; then
    tail -60 "$convert_log" >&2
    exit 20
  fi
  test -f "$OUT/rnnt_$name.ms"
done

DATA=/tmp/hoskey_rnnt_inputs
mkdir -p "$DATA"
for spec in f100:400 f750:3000 f300:1200 f3000:12000 f1500:6000 f900:3600 f260:1040; do
  name=${spec%:*}
  bytes=${spec#*:}
  dd if=/dev/zero of="$DATA/$name.bin" bs="$bytes" count=1 status=none
done

for name in joint post0 post1 post2 post3 pre0 pre1 pre2 pre3 sampling; do
  case "$name" in
    joint) inputs="$DATA/f100.bin,$DATA/f750.bin";;
    post*) inputs="$DATA/f100.bin,$DATA/f300.bin,$DATA/f3000.bin,$DATA/f1500.bin,$DATA/f900.bin";;
    pre*) inputs="$DATA/f100.bin,$DATA/f100.bin";;
    sampling) inputs="$DATA/f260.bin";;
  esac
  log="/tmp/hoskey_rnnt_benchmark_$name.log"
  timeout -k 2s 30s "$BENCH" --modelFile="$OUT/rnnt_$name.ms" \
    --inDataFile="$inputs" --loopCount=1 --warmUpLoopCount=0 \
    --numThreads=2 >"$log" 2>&1
  grep -q "Run Benchmark .* Success" "$log"
  grep -E "Model = .*AvgRunTime" "$log" | tail -1
done
'@

$bash = $bash.Replace('__DOWNLOAD__', $download)
$bash = $bash.Replace('__MSL__', $MindSporeLiteDir.Replace('\', '/'))
$bash = $bash.Replace('__SPLIT__', $splitWsl)
$bash = $bash.Replace('__OUTPUT__', $outWsl)
$bash = $bash.Replace('__REWRITE__', $rewriteWsl)
$bashPath = Join-Path $env:TEMP 'hoskey_convert_rnnt_models.sh'
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText(
  $bashPath, $bash.Replace("`r`n", "`n"), $utf8NoBom)
wsl bash (ConvertTo-WslPath $bashPath)
if ($LASTEXITCODE -ne 0) {
  throw "RNNT conversion or benchmark failed with exit code $LASTEXITCODE"
}

$expected = @(
  'rnnt_joint.ms',
  'rnnt_post0.ms', 'rnnt_post1.ms', 'rnnt_post2.ms', 'rnnt_post3.ms',
  'rnnt_pre0.ms', 'rnnt_pre1.ms', 'rnnt_pre2.ms', 'rnnt_pre3.ms',
  'rnnt_sampling.ms'
)
$models = Get-ChildItem -LiteralPath $OutputDir -Filter 'rnnt_*.ms' |
  Where-Object { $_.Name -in $expected } |
  Sort-Object Name
if ($models.Count -ne $expected.Count) {
  throw "expected $($expected.Count) runnable RNNT models, found $($models.Count)"
}
$models | Select-Object Name, Length, LastWriteTime
