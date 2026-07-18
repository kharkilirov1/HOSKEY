#!/usr/bin/env python3
"""Discover and capture Yandex Keyboard MatrixNet feature vectors.

The live Frida dependency is imported lazily so `verify` and unit tests work on
hosts whose default Python differs from the Frida installation interpreter.
"""

from __future__ import annotations

import argparse
import json
import math
import sys
import time
from collections import Counter
from pathlib import Path
from typing import Any, Iterable


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SCRIPT = ROOT / "tools" / "frida_ranker_features.js"
DEFAULT_OUTPUT = ROOT / "tools" / "ranker_feature_reference.json"
FEATURE_COUNTS = {
    "normal": 44,
    "swipe": 47,
    "swipe-backoff": 42,
    "swipe-blocker": 31,
}
COMBO_TOLERANCE = 1.0e-3


def _near_integer(value: float, tolerance: float = 1.0e-3) -> bool:
    return abs(value - round(value)) <= tolerance


def _boolean_like(value: float, tolerance: float = 1.0e-3) -> bool:
    return abs(value) <= tolerance or abs(value - 1.0) <= tolerance


def _validate_swipe_vector(vector: list[float], stats_offset: int) -> tuple[bool, str]:
    """Validate common geometric invariants of both swipe rankers."""

    for index in (3, 4, 8):
        value = vector[index]
        maximum = 16.0 if index == 3 else (32.0 if index == 4 else 512.0)
        if value < 0.0 or value > maximum or not _near_integer(value):
            return False, f"feature {index} is not a plausible integer"
    if vector[4] < 2.0:
        return False, "feature 4 is not a plausible swipe word length"
    if vector[8] < 2.0:
        return False, "feature 8 is not a plausible point count"
    if not (0.0 <= vector[0] <= 50000.0 and
            0.0 <= vector[1] <= 1000.0 and
            0.0 <= vector[2] <= 200.0):
        return False, "distance/weight/lm_weight is outside the swipe domain"
    if not (0.0 < vector[9] <= 10000.0):
        return False, "feature 9 is not a plausible path length"

    # min / max / total / average / stddev groups recovered from the feature
    # catalog.  These relations are independent of keyboard size and timing.
    for minimum, maximum, total, average, stddev in (
        (13, 14, 15, 16, 17),
        (18, 19, 20, 21, 22),
        (30, 31, 27, 32, 33),
    ):
        if min(vector[minimum], vector[maximum], vector[total],
               vector[average], vector[stddev]) < 0.0:
            return False, f"negative aggregate near feature {minimum}"
        if vector[minimum] > vector[average] + COMBO_TOLERANCE or \
                vector[average] > vector[maximum] + COMBO_TOLERANCE:
            return False, f"min/avg/max mismatch near feature {minimum}"
        if vector[total] + COMBO_TOLERANCE < vector[maximum]:
            return False, f"total/max mismatch near feature {minimum}"
    if not (0.0 < vector[27] <= 10000.0 and
            0.0 < vector[34] <= 5000.0):
        return False, "gesture timing is outside the swipe domain"
    if any(abs(vector[index]) > 2.0 for index in (23, 24, 25, 26)):
        return False, "normalized endpoint delta is outside the swipe domain"

    min_weight, min_distance, mean_weight, min_path_penalty = \
        vector[stats_offset:stats_offset + 4]
    if min_weight > vector[1] + COMBO_TOLERANCE:
        return False, "min(weight) is greater than candidate weight"
    if min_distance > vector[0] + COMBO_TOLERANCE:
        return False, "min(distance) is greater than candidate distance"
    if min_path_penalty > vector[21] + COMBO_TOLERANCE:
        return False, "min(avg_path_penalty) is greater than candidate value"
    if mean_weight + COMBO_TOLERANCE < min_weight:
        return False, "mean(weight) is below min(weight)"
    return True, "ok"


def validate_feature_vector(
    values: Iterable[float], profile: str = "normal"
) -> tuple[bool, str]:
    """Validate structural invariants for the selected Yandex feature catalog."""

    vector = list(values)
    expected = FEATURE_COUNTS.get(profile)
    if expected is None:
        return False, f"unknown feature profile: {profile}"
    if len(vector) != expected:
        return False, f"expected {expected} floats, got {len(vector)}"
    if not all(math.isfinite(value) for value in vector):
        return False, "all feature values must be finite"
    if profile == "swipe-backoff":
        return _validate_swipe_vector(vector, 38)
    if profile == "swipe":
        if not _boolean_like(vector[38]):
            return False, "feature 38 layout_default is not boolean-like"
        return _validate_swipe_vector(vector, 43)
    if profile == "swipe-blocker":
        # Blocker vectors aggregate the gesture and candidate list.  Keep the
        # live-capture check deliberately conservative: exact feature semantics
        # are tested in the C++ implementation, while this rejects random heap
        # floats without assuming a model decision threshold.
        if vector[0] < 0.0 or vector[1] < 0.0 or not _near_integer(vector[0]):
            return False, "invalid blocker point counters"
        if not (0.0 <= vector[2] <= 1.0 + COMBO_TOLERANCE):
            return False, "invalid blocker out-points ratio"
        return True, "ok"

    # Normal 44-feature spelling ranker.
    for index in (0, 7):
        value = vector[index]
        if value < 0.0 or value > 32.0 or not _near_integer(value):
            return False, f"feature {index} is not a plausible integer length"
    for index in (6, 13, 14, 15, 26, 43):
        if not _boolean_like(vector[index]):
            return False, f"feature {index} is not boolean-like"
    expected_sum = vector[16] + vector[8]
    if abs(vector[28] - expected_sum) > COMBO_TOLERANCE:
        return False, "feature 28 does not equal feature 16 + feature 8"
    expected_static_sum = vector[16] + vector[11]
    if abs(vector[29] - expected_static_sum) > COMBO_TOLERANCE:
        return False, "feature 29 does not equal feature 16 + feature 11"
    expected_sub = vector[1] - vector[8]
    if abs(vector[30] - expected_sub) > COMBO_TOLERANCE:
        return False, "feature 30 does not equal feature 1 - feature 8"
    expected_static_sub = vector[2] - vector[9]
    if abs(vector[31] - expected_static_sub) > COMBO_TOLERANCE:
        return False, "feature 31 does not equal feature 2 - feature 9"
    expected_length_sub = vector[0] - vector[7]
    if abs(vector[32] - expected_length_sub) > COMBO_TOLERANCE:
        return False, "feature 32 does not equal feature 0 - feature 7"
    return True, "ok"


def _normalize_offset(value: str | int) -> str:
    if isinstance(value, int):
        return f"0x{value:x}"
    parsed = int(value, 0)
    return f"0x{parsed:x}"


def parse_targets(raw: str | None, path: Path | None) -> list[str]:
    targets: list[str] = []
    if raw:
        targets.extend(part.strip() for part in raw.split(",") if part.strip())
    if path:
        payload = json.loads(path.read_text(encoding="utf-8"))
        if isinstance(payload, dict):
            source = payload.get("targets", [])
        else:
            source = payload
        targets.extend(str(item) for item in source)
    return sorted({_normalize_offset(item) for item in targets}, key=lambda x: int(x, 0))


def verify_capture(path: Path, profile: str | None = None) -> dict[str, Any]:
    payload = json.loads(path.read_text(encoding="utf-8"))
    records = payload.get("records", []) if isinstance(payload, dict) else payload
    selected_profile = profile or (
        str(payload.get("profile", "normal")) if isinstance(payload, dict) else "normal"
    )
    if not isinstance(records, list) or not records:
        raise ValueError("capture contains no feature-vector records")

    offsets: Counter[str] = Counter()
    prefixes: set[str] = set()
    failures: list[str] = []
    for index, record in enumerate(records):
        if not isinstance(record, dict):
            failures.append(f"record {index}: not an object")
            continue
        ok, reason = validate_feature_vector(
            record.get("features", []), selected_profile
        )
        if not ok:
            failures.append(f"record {index}: {reason}")
        offset = str(record.get("offset", ""))
        if offset:
            offsets[offset] += 1
        prefix = str(record.get("prefix", ""))
        if prefix:
            prefixes.add(prefix)

    if failures:
        raise ValueError("; ".join(failures[:20]))
    repeated = {offset: count for offset, count in offsets.items() if count >= 2}
    if not repeated:
        raise ValueError("no hook offset produced multiple valid vectors")
    return {
        "records": len(records),
        "profile": selected_profile,
        "prefixes": sorted(prefixes),
        "repeated_offsets": repeated,
    }


def _load_frida() -> Any:
    try:
        import frida  # type: ignore
    except ImportError as exc:
        raise RuntimeError(
            "Frida Python bindings are unavailable in this interpreter; use "
            r"C:\Users\Kharki\AppData\Local\Programs\Python\Python312\python.exe"
        ) from exc
    return frida


def _find_pid(device: Any, package: str) -> int:
    for process in device.enumerate_processes():
        if process.name == package or process.name.startswith(package + ":"):
            if process.name == package:
                return int(process.pid)
    try:
        applications = device.enumerate_applications(scope="full")
    except TypeError:
        applications = device.enumerate_applications()
    for application in applications:
        if application.identifier == package and int(application.pid) > 0:
            return int(application.pid)
    raise RuntimeError(f"running process not found: {package}")


def run_live(args: argparse.Namespace) -> int:
    if not args.script.exists():
        raise FileNotFoundError(args.script)

    frida = _load_frida()
    device = frida.get_usb_device(timeout=5)
    pid = _find_pid(device, args.package)
    session = device.attach(pid)

    config = {
        "mode": args.command,
        "targets": parse_targets(args.targets, args.targets_file),
        "maxVectors": args.max_vectors,
        "captureJava": bool(args.capture_java),
        "captureContext": bool(args.capture_context),
        "profile": args.profile,
    }
    source = args.script.read_text(encoding="utf-8").replace(
        "__CONFIG_JSON__", json.dumps(config, ensure_ascii=False)
    )

    records: list[dict[str, Any]] = []
    summaries: list[dict[str, Any]] = []
    suggestions: list[dict[str, Any]] = []
    requests: list[dict[str, Any]] = []

    def on_message(message: dict[str, Any], data: bytes | None) -> None:
        del data
        if message.get("type") == "error":
            print(message.get("stack", message), file=sys.stderr)
            return
        payload = message.get("payload", {})
        if not isinstance(payload, dict):
            return
        kind = payload.get("type")
        if kind == "feature-vector":
            ok, reason = validate_feature_vector(
                payload.get("features", []), args.profile
            )
            if ok:
                records.append(payload)
                print(
                    f"[VECTOR] offset={payload.get('offset')} "
                    f"arg={payload.get('arg')} prefix={payload.get('prefix', '')!r}"
                )
            else:
                print(f"[REJECT] {reason}")
        elif kind == "call-summary":
            summaries.append(payload)
            print(
                f"[SUMMARY] calls={payload.get('calls')} "
                f"targets={len(payload.get('targets', []))}"
            )
        elif kind == "suggestion-text":
            suggestions.append(payload)
            print(f"[SUGGESTION] {payload.get('text', '')}")
        elif kind == "request-bytes":
            requests.append(payload)
            print(f"[REQUEST] bytes={len(payload.get('bytes', []))}")
        elif kind == "status":
            print(f"[STATUS] {payload.get('message', '')}")

    script = session.create_script(source)
    script.on("message", on_message)
    script.load()
    print(f"attached pid={pid}; capture window={args.seconds}s")
    started = time.monotonic()
    try:
        if args.heap_length > 0:
            started_scan = script.exports_sync.scanheap(args.heap_length)
            if not started_scan:
                raise RuntimeError(
                    f"heap scan rejected original length {args.heap_length}"
                )
            print(f"started heap vector scan for original length {args.heap_length}")
        if args.replay_count > 0:
            replay_deadline = started + min(args.seconds, 8.0)
            while time.monotonic() < replay_deadline:
                try:
                    replayed = script.exports_sync.replay(args.replay_count)
                except Exception:
                    replayed = 0
                if replayed:
                    print(f"replayed captured request {replayed} times")
                    break
                time.sleep(0.25)
        remaining = args.seconds - (time.monotonic() - started)
        if remaining > 0:
            time.sleep(remaining)
    finally:
        try:
            script.exports_sync.stop()
        except Exception:
            pass
        session.detach()

    targets: Counter[str] = Counter()
    for summary in summaries:
        for item in summary.get("targets", []):
            if isinstance(item, dict) and "offset" in item:
                targets[str(item["offset"])] += int(item.get("count", 0))

    output = {
        "schema": "hoskey-ranker-feature-capture/v1",
        "package": args.package,
        "mode": args.command,
        "profile": args.profile,
        "targets": [offset for offset, _ in targets.most_common()],
        "summaries": summaries,
        "records": records,
        "suggestions": suggestions,
        "requests": requests,
    }
    args.output.write_text(
        json.dumps(output, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    print(
        f"wrote {args.output}: summaries={len(summaries)} "
        f"records={len(records)} targets={len(targets)}"
    )
    return 0 if summaries or records else 2


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="command", required=True)

    for command in ("discover", "capture"):
        live = sub.add_parser(command)
        live.add_argument("--package", default="ru.yandex.androidkeyboard")
        live.add_argument("--seconds", type=float, default=30.0)
        live.add_argument("--script", type=Path, default=DEFAULT_SCRIPT)
        live.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
        live.add_argument("--targets")
        live.add_argument("--targets-file", type=Path)
        live.add_argument("--max-vectors", type=int, default=200)
        live.add_argument("--capture-java", action="store_true")
        live.add_argument("--capture-context", action="store_true")
        live.add_argument("--replay-count", type=int, default=0)
        live.add_argument("--heap-length", type=int, default=0)
        live.add_argument(
            "--profile", choices=sorted(FEATURE_COUNTS), default="normal"
        )
        live.set_defaults(func=run_live)

    verify = sub.add_parser("verify")
    verify.add_argument("path", type=Path)
    verify.add_argument("--profile", choices=sorted(FEATURE_COUNTS))

    return parser


def main() -> int:
    args = build_parser().parse_args()
    if args.command == "verify":
        result = verify_capture(args.path, args.profile)
        print(json.dumps(result, ensure_ascii=False, indent=2))
        return 0
    return int(args.func(args))


if __name__ == "__main__":
    raise SystemExit(main())
