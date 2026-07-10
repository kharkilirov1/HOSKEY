#!/usr/bin/env python3
"""
frida_extract_frequencies.py

Orchestrator for extracting word frequency/weight scores from Yandex Keyboard
using Frida instrumentation on a rooted Android device.

PREREQUISITES:
  1. Android device with root access connected via USB
  2. Frida server running on the device:
       adb push frida-server-XX.X.X-android-arm64 /data/local/tmp/frida-server
       adb shell "chmod 755 /data/local/tmp/frida-server"
       adb shell "su -c /data/local/tmp/frida-server &"
  3. Yandex Keyboard installed and active (open any text field to activate it)
  4. Python packages: pip install frida frida-tools

USAGE:
  python frida_extract_frequencies.py                    # Full extraction from words_final.csv
  python frida_extract_frequencies.py --passive           # Passive mode: capture while typing
  python frida_extract_frequencies.py --input words.csv   # Custom input file
  python frida_extract_frequencies.py --batch-size 50     # Batch size for active mode
  python frida_extract_frequencies.py --resume             # Resume from last checkpoint
  python frida_extract_frequencies.py --timeout 1.0        # Per-word timeout in seconds

PACKAGE NAMES (auto-detected):
  - ru.yandex.androidkeyboard   (standard)
  - com.yandex.keyboard          (alternative)

OUTPUT:
  words_with_weights.tsv   - word<TAB>weight pairs
  checkpoint.json          - progress checkpoint for resume
"""

import frida
import sys
import os
import csv
import json
import time
import signal
import atexit
import argparse
import subprocess
import threading
from pathlib import Path
from collections import OrderedDict

# ============================================================
# Configuration
# ============================================================
SCRIPT_DIR = Path(__file__).parent.resolve()
JS_SCRIPT_PATH = SCRIPT_DIR / "frida_extract_frequencies.js"

DEFAULT_INPUT = Path(r"C:\Users\Kharki\clawd\v600_analysis\words_final.csv")
DEFAULT_OUTPUT = SCRIPT_DIR / "words_with_weights.tsv"
CHECKPOINT_FILE = SCRIPT_DIR / "freq_checkpoint.json"

PACKAGE_NAMES = [
    "ru.yandex.androidkeyboard",
    "com.yandex.keyboard",
]

DEFAULT_BATCH_SIZE = 20
DEFAULT_TIMEOUT = 0.5  # seconds per word
SAVE_INTERVAL = 100    # save every N words


# ============================================================
# Result collector
# ============================================================
class FrequencyCollector:
    """Thread-safe collector for word frequency results."""

    def __init__(self, output_path: Path, checkpoint_path: Path):
        self.output_path = output_path
        self.checkpoint_path = checkpoint_path
        self.results: OrderedDict[str, int] = OrderedDict()
        self.lock = threading.Lock()
        self.pending_word = None
        self.word_event = threading.Event()
        self.ready_event = threading.Event()
        self.error_count = 0
        self.processed_count = 0
        self.hook_info = {}

        # Raw protobuf data for analysis
        self.raw_responses: list[dict] = []

    def add_result(self, word: str, weight: int):
        with self.lock:
            self.results[word] = weight
            self.processed_count += 1
            if self.pending_word == word:
                self.word_event.set()

    def get_weight(self, word: str) -> int | None:
        with self.lock:
            return self.results.get(word)

    def save(self):
        """Save current results to TSV and checkpoint."""
        with self.lock:
            results_copy = dict(self.results)
            count = self.processed_count

        if not results_copy:
            return

        # Save TSV
        with open(self.output_path, 'w', encoding='utf-8', newline='') as f:
            f.write("word\tweight\n")
            for word, weight in sorted(results_copy.items()):
                f.write(f"{word}\t{weight}\n")

        # Save checkpoint
        checkpoint = {
            "processed_count": count,
            "last_word": list(results_copy.keys())[-1] if results_copy else "",
            "total_results": len(results_copy),
            "nonzero_weights": sum(1 for w in results_copy.values() if w > 0),
            "timestamp": time.strftime("%Y-%m-%d %H:%M:%S"),
        }
        with open(self.checkpoint_path, 'w', encoding='utf-8') as f:
            json.dump(checkpoint, f, indent=2, ensure_ascii=False)

        nonzero = checkpoint["nonzero_weights"]
        print(f"[SAVE] {len(results_copy)} words saved, {nonzero} with weight > 0")

    def load_checkpoint(self) -> set[str]:
        """Load checkpoint and existing results. Returns set of already processed words."""
        processed = set()

        if self.output_path.exists():
            try:
                with open(self.output_path, 'r', encoding='utf-8') as f:
                    reader = csv.reader(f, delimiter='\t')
                    header = next(reader, None)
                    for row in reader:
                        if len(row) >= 2:
                            word, weight = row[0], int(row[1])
                            self.results[word] = weight
                            processed.add(word)
                print(f"[RESUME] Loaded {len(processed)} existing results")
            except Exception as e:
                print(f"[WARN] Could not load existing results: {e}")

        if self.checkpoint_path.exists():
            try:
                with open(self.checkpoint_path, 'r') as f:
                    cp = json.load(f)
                print(f"[RESUME] Checkpoint: {cp.get('processed_count', 0)} processed, "
                      f"last saved at {cp.get('timestamp', '?')}")
            except Exception:
                pass

        return processed


# ============================================================
# Frida message handler
# ============================================================
def create_message_handler(collector: FrequencyCollector):
    """Create a Frida message handler closure."""

    def on_message(message, data):
        if message['type'] == 'send':
            payload = message['payload']
            msg_type = payload.get('type', '')

            if msg_type == 'result':
                word = payload.get('word', '')
                weight = payload.get('weight', 0)
                source = payload.get('source', '?')
                scores = payload.get('scores', [])
                suggestions = payload.get('suggestions', [])

                # Use the max score from all detected score fields
                if scores:
                    weight = max(scores)

                collector.add_result(word, weight)

                extra = ""
                if suggestions:
                    extra = f" suggestions={suggestions[:3]}"
                if scores:
                    extra += f" raw_scores={scores[:5]}"
                print(f"  [{source}] {word} -> {weight}{extra}")

            elif msg_type == 'passive_result':
                words = payload.get('words', [])
                scores = payload.get('scores', [])
                hex_data = payload.get('hex', '')

                # Store raw response for offline analysis
                collector.raw_responses.append({
                    'words': words,
                    'scores': scores,
                    'hex_preview': hex_data[:200]
                })

                for w in words:
                    if w not in collector.results:
                        # Try to pair with a score
                        best_score = 0
                        for s in scores:
                            v = s.get('v', 0) if isinstance(s, dict) else s
                            if v > best_score:
                                best_score = v
                        collector.add_result(w, best_score)
                        print(f"  [passive] {w} -> {best_score}")

            elif msg_type == 'native_weight':
                weight = payload.get('weight', 0)
                node = payload.get('node', '')
                # Native weight from trie - can't directly map to word here
                # but useful for correlation with active lookups
                pass

            elif msg_type == 'ready':
                collector.hook_info = payload
                collector.ready_event.set()
                print(f"[+] Agent ready: hookMode={payload.get('hookMode')}, "
                      f"facilitator={payload.get('hasFacilitator')}, "
                      f"nativeWeight={payload.get('hasNativeWeight')}, "
                      f"nativeSuggest={payload.get('hasNativeSuggest')}")

            elif msg_type == 'status':
                collector.hook_info = payload
                print(f"[STATUS] {json.dumps(payload)}")

            elif msg_type == 'batch_done':
                print(f"[BATCH] Completed {payload.get('count', 0)} words")

            elif msg_type == 'pong':
                pass

            elif msg_type == 'log':
                print(payload.get('message', ''))

            elif msg_type == 'error':
                print(f"[ERROR] {payload.get('message', '')}")
                collector.error_count += 1

        elif message['type'] == 'error':
            desc = message.get('description', str(message))
            print(f"[FRIDA ERROR] {desc}")
            stack = message.get('stack', '')
            if stack:
                print(f"  Stack: {stack[:200]}")
            collector.error_count += 1

    return on_message


# ============================================================
# Device & process helpers
# ============================================================
def get_keyboard_pid() -> int | None:
    """Find Yandex Keyboard PID via adb."""
    for pkg in PACKAGE_NAMES:
        try:
            result = subprocess.run(
                ['adb', 'shell', f'ps -A | grep {pkg}'],
                capture_output=True, text=True, timeout=10
            )
            for line in result.stdout.strip().split('\n'):
                if pkg in line and 'AppMetrica' not in line and 'passport' not in line:
                    parts = line.split()
                    if len(parts) >= 2:
                        pid = int(parts[1])
                        print(f"[+] Found {pkg} PID: {pid}")
                        return pid
        except Exception:
            continue
    return None


def find_keyboard_process(device) -> str | None:
    """Find keyboard process name on device."""
    try:
        processes = device.enumerate_processes()
        for proc in processes:
            for pkg in PACKAGE_NAMES:
                if pkg in proc.name:
                    print(f"[+] Found process: {proc.name} (PID {proc.pid})")
                    return proc.name
    except Exception as e:
        print(f"[-] enumerate_processes failed: {e}")
    return None


def ensure_keyboard_active():
    """Try to ensure keyboard is active by focusing a text field."""
    print("[*] Hint: open a text field on the device to activate the keyboard")
    try:
        # Open a simple activity with a text field via adb
        subprocess.run(
            ['adb', 'shell', 'am', 'start', '-a', 'android.intent.action.VIEW',
             '-d', 'https://www.google.com'],
            capture_output=True, timeout=10
        )
        time.sleep(2)
        # Tap on search bar area (approximate coordinates)
        subprocess.run(
            ['adb', 'shell', 'input', 'tap', '540', '200'],
            capture_output=True, timeout=5
        )
        time.sleep(1)
    except Exception:
        pass


# ============================================================
# Word loading
# ============================================================
def load_words(input_path: Path) -> list[str]:
    """Load words from CSV file (word,weight format with header)."""
    words = []
    with open(input_path, 'r', encoding='utf-8') as f:
        reader = csv.reader(f)
        header = next(reader, None)  # skip header
        for row in reader:
            if row:
                word = row[0].strip()
                if word:
                    words.append(word)
    print(f"[+] Loaded {len(words)} words from {input_path}")
    return words


# ============================================================
# Active extraction mode
# ============================================================
def run_active_extraction(
    script,
    collector: FrequencyCollector,
    words: list[str],
    batch_size: int,
    timeout: float,
    resume: bool
):
    """
    Active mode: send words one by one to the JS agent,
    collect frequency weights from the suggestion response.
    """
    # Filter already processed words if resuming
    if resume:
        processed = collector.load_checkpoint()
        remaining = [w for w in words if w not in processed]
        print(f"[RESUME] {len(words) - len(remaining)} already done, {len(remaining)} remaining")
        words = remaining

    if not words:
        print("[+] All words already processed!")
        return

    total = len(words)
    start_time = time.time()

    print(f"\n{'=' * 60}")
    print(f"Active extraction: {total} words, batch_size={batch_size}, timeout={timeout}s")
    print(f"{'=' * 60}\n")

    for i in range(0, total, batch_size):
        batch = words[i:i + batch_size]
        batch_num = i // batch_size + 1
        total_batches = (total + batch_size - 1) // batch_size
        elapsed = time.time() - start_time
        rate = collector.processed_count / elapsed if elapsed > 0 else 0
        eta = (total - i) / rate if rate > 0 else 0

        print(f"\n--- Batch {batch_num}/{total_batches} "
              f"({collector.processed_count}/{total} done, "
              f"{rate:.1f} words/s, ETA {eta:.0f}s) ---")

        for word in batch:
            collector.pending_word = word
            collector.word_event.clear()

            # Send lookup command to JS agent
            script.post({"type": "command", "payload": {
                "type": "lookup",
                "word": word
            }})

            # Wait for result with timeout
            got_result = collector.word_event.wait(timeout=timeout)

            if not got_result:
                # Timed out - record as weight 0
                collector.add_result(word, 0)
                # Don't print timeout for every word - too noisy
                if collector.error_count < 10:
                    print(f"  [timeout] {word} -> 0")

            collector.pending_word = None

        # Periodic save
        if (i + batch_size) % SAVE_INTERVAL < batch_size:
            collector.save()

        # Check error threshold
        if collector.error_count > 100:
            print(f"\n[!] Too many errors ({collector.error_count}), stopping")
            break

    collector.save()
    elapsed = time.time() - start_time

    nonzero = sum(1 for w in collector.results.values() if w > 0)
    print(f"\n{'=' * 60}")
    print(f"Extraction complete: {collector.processed_count} words in {elapsed:.1f}s")
    print(f"Results: {len(collector.results)} total, {nonzero} with weight > 0")
    print(f"Output: {collector.output_path}")
    print(f"{'=' * 60}")


# ============================================================
# Passive capture mode
# ============================================================
def run_passive_capture(collector: FrequencyCollector):
    """Passive mode: just capture suggestions while user types."""
    print(f"\n{'=' * 60}")
    print("PASSIVE MODE: Type on the keyboard to capture word frequencies")
    print("Press Ctrl+C to stop and save")
    print(f"{'=' * 60}\n")

    last_save = time.time()
    try:
        while True:
            time.sleep(0.5)
            if time.time() - last_save > 30:
                collector.save()
                last_save = time.time()
    except KeyboardInterrupt:
        print("\n[*] Stopping...")

    collector.save()

    # Also save raw protobuf data for offline analysis
    if collector.raw_responses:
        raw_path = SCRIPT_DIR / "raw_protobuf_responses.json"
        with open(raw_path, 'w', encoding='utf-8') as f:
            json.dump(collector.raw_responses, f, indent=2, ensure_ascii=False)
        print(f"[+] Raw responses saved to {raw_path}")


# ============================================================
# Main
# ============================================================
def main():
    parser = argparse.ArgumentParser(
        description="Extract word frequencies from Yandex Keyboard via Frida"
    )
    parser.add_argument('--input', '-i', type=Path, default=DEFAULT_INPUT,
                        help=f"Input CSV file (default: {DEFAULT_INPUT})")
    parser.add_argument('--output', '-o', type=Path, default=DEFAULT_OUTPUT,
                        help=f"Output TSV file (default: {DEFAULT_OUTPUT})")
    parser.add_argument('--passive', action='store_true',
                        help="Passive mode: capture while user types")
    parser.add_argument('--batch-size', type=int, default=DEFAULT_BATCH_SIZE,
                        help=f"Words per batch (default: {DEFAULT_BATCH_SIZE})")
    parser.add_argument('--timeout', type=float, default=DEFAULT_TIMEOUT,
                        help=f"Per-word timeout in seconds (default: {DEFAULT_TIMEOUT})")
    parser.add_argument('--resume', action='store_true',
                        help="Resume from checkpoint")
    parser.add_argument('--spawn', action='store_true',
                        help="Spawn keyboard process instead of attaching")

    args = parser.parse_args()

    print("=" * 60)
    print("Yandex Keyboard Frequency Extractor")
    print("=" * 60)

    # Load JS agent
    if not JS_SCRIPT_PATH.exists():
        print(f"[-] JS script not found: {JS_SCRIPT_PATH}")
        return 1

    js_code = JS_SCRIPT_PATH.read_text(encoding='utf-8')
    print(f"[+] JS agent loaded ({len(js_code)} bytes)")

    # Initialize collector
    collector = FrequencyCollector(args.output, CHECKPOINT_FILE)

    # Connect to device
    try:
        device = frida.get_usb_device(timeout=10)
        print(f"[+] Device: {device.name}")
    except frida.TimedOutError:
        print("[-] No USB device found! Check connection and frida-server.")
        return 1
    except Exception as e:
        print(f"[-] Device error: {e}")
        return 1

    # Find keyboard process
    session = None
    if args.spawn:
        # Spawn mode
        pkg = PACKAGE_NAMES[0]
        try:
            pid = device.spawn([pkg])
            session = device.attach(pid)
            print(f"[+] Spawned {pkg} (PID {pid})")
            device.resume(pid)
            time.sleep(3)
        except Exception as e:
            print(f"[-] Spawn failed: {e}")
            return 1
    else:
        # Attach mode
        pid = get_keyboard_pid()
        if not pid:
            # Try via Frida process enumeration
            proc_name = find_keyboard_process(device)
            if proc_name:
                try:
                    session = device.attach(proc_name)
                    print(f"[+] Attached to {proc_name}")
                except Exception as e:
                    print(f"[-] Attach failed: {e}")
                    return 1
            else:
                print("[-] Yandex Keyboard not running!")
                print("[*] Open any text field on the device to activate the keyboard.")
                ensure_keyboard_active()
                time.sleep(3)
                pid = get_keyboard_pid()
                if not pid:
                    print("[-] Still not found. Start keyboard manually and retry.")
                    return 1

        if session is None:
            try:
                session = device.attach(pid)
                print(f"[+] Attached to PID {pid}")
            except Exception as e:
                print(f"[-] Attach failed: {e}")
                return 1

    # Create and load Frida script
    script = session.create_script(js_code)
    script.on('message', create_message_handler(collector))

    try:
        script.load()
        print("[+] Script loaded")
    except Exception as e:
        print(f"[-] Script load failed: {e}")
        return 1

    # Wait for agent readiness
    print("[*] Waiting for agent initialization...")
    if not collector.ready_event.wait(timeout=10):
        print("[WARN] Agent did not signal ready within 10s, continuing anyway")

    # Register cleanup
    def cleanup():
        collector.save()
        try:
            session.detach()
        except Exception:
            pass

    atexit.register(cleanup)
    signal.signal(signal.SIGINT, lambda s, f: (cleanup(), sys.exit(0)))
    try:
        signal.signal(signal.SIGBREAK, lambda s, f: (cleanup(), sys.exit(0)))
    except (AttributeError, ValueError):
        pass

    # Run extraction
    if args.passive:
        run_passive_capture(collector)
    else:
        # Load words
        if not args.input.exists():
            print(f"[-] Input file not found: {args.input}")
            return 1
        words = load_words(args.input)
        if not words:
            print("[-] No words loaded!")
            return 1

        run_active_extraction(
            script, collector, words,
            batch_size=args.batch_size,
            timeout=args.timeout,
            resume=args.resume
        )

    cleanup()
    return 0


if __name__ == "__main__":
    sys.exit(main())
