'use strict';

// Replaced by tools/capture_ranker_features.py before the script is loaded.
const CONFIG = __CONFIG_JSON__;
const PROFILE = CONFIG.profile || 'normal';
const FEATURE_COUNTS = {
  normal: 44,
  swipe: 47,
  'swipe-backoff': 42,
  'swipe-blocker': 31
};
const FEATURE_COUNT = FEATURE_COUNTS[PROFILE];
if (FEATURE_COUNT === undefined) throw new Error('unsupported profile: ' + PROFILE);
const COMBO_TOLERANCE = 1.0e-3;

const ykeyboard = Process.findModuleByName('libjni_ykeyboard3.so');
if (ykeyboard === null) {
  throw new Error('libjni_ykeyboard3.so is not loaded');
}

let lastComposing = '';
let lastSuggestion = '';
let lastRequestBytes = null;
let javaFacilitator = null;
let emittedVectors = 0;
const followedThreads = new Set();
const attachedTargets = [];

function status(message) {
  send({ type: 'status', message: message });
}

function insideModule(address) {
  return address.compare(ykeyboard.base) >= 0 &&
      address.compare(ykeyboard.base.add(ykeyboard.size)) < 0;
}

function relativeOffset(address) {
  return '0x' + address.sub(ykeyboard.base).toString(16);
}

function readableRange(address, byteCount) {
  if (address.isNull()) return false;
  const range = Process.findRangeByAddress(address);
  if (range === null || range.protection.indexOf('r') < 0) return false;
  return address.add(byteCount).compare(range.base.add(range.size)) <= 0;
}

function booleanLike(value) {
  return Math.abs(value) <= COMBO_TOLERANCE ||
      Math.abs(value - 1.0) <= COMBO_TOLERANCE;
}

function integerLength(value) {
  return Number.isFinite(value) && value >= 0.0 && value <= 32.0 &&
      Math.abs(value - Math.round(value)) <= COMBO_TOLERANCE;
}

function validateSwipeVector(values, statsOffset) {
  for (const index of [3, 4, 8]) {
    const maximum = index === 3 ? 16 : (index === 4 ? 32 : 512);
    if (values[index] < 0 || values[index] > maximum ||
        Math.abs(values[index] - Math.round(values[index])) > COMBO_TOLERANCE) {
      return false;
    }
  }
  if (values[4] < 2 || values[8] < 2 || values[0] < 0 || values[0] > 50000 ||
      values[1] < 0 || values[1] > 1000 || values[2] < 0 || values[2] > 200 ||
      values[9] <= 0 || values[9] > 10000) return false;
  for (const group of [[13, 14, 15, 16, 17], [18, 19, 20, 21, 22],
                       [30, 31, 27, 32, 33]]) {
    const minimum = values[group[0]];
    const maximum = values[group[1]];
    const total = values[group[2]];
    const average = values[group[3]];
    const stddev = values[group[4]];
    if (Math.min(minimum, maximum, total, average, stddev) < 0 ||
        minimum > average + COMBO_TOLERANCE ||
        average > maximum + COMBO_TOLERANCE ||
        total + COMBO_TOLERANCE < maximum) return false;
  }
  if (values[27] <= 0 || values[27] > 10000 ||
      values[34] <= 0 || values[34] > 5000) return false;
  for (const index of [23, 24, 25, 26]) {
    if (Math.abs(values[index]) > 2) return false;
  }
  const minWeight = values[statsOffset];
  const minDistance = values[statsOffset + 1];
  const meanWeight = values[statsOffset + 2];
  const minPathPenalty = values[statsOffset + 3];
  return minWeight <= values[1] + COMBO_TOLERANCE &&
      minDistance <= values[0] + COMBO_TOLERANCE &&
      meanWeight + COMBO_TOLERANCE >= minWeight &&
      minPathPenalty <= values[21] + COMBO_TOLERANCE;
}

function validateVector(values) {
  if (values.length !== FEATURE_COUNT) return false;
  for (let i = 0; i < values.length; ++i) {
    if (!Number.isFinite(values[i])) return false;
  }
  if (PROFILE === 'swipe-backoff') return validateSwipeVector(values, 38);
  if (PROFILE === 'swipe') {
    return booleanLike(values[38]) && validateSwipeVector(values, 43);
  }
  if (PROFILE === 'swipe-blocker') {
    return values[0] >= 0 && Math.abs(values[0] - Math.round(values[0])) <=
        COMBO_TOLERANCE && values[1] >= 0 && values[2] >= 0 && values[2] <= 1.001;
  }
  if (!integerLength(values[0]) || !integerLength(values[7])) return false;
  for (const index of [6, 13, 14, 15, 26, 43]) {
    if (!booleanLike(values[index])) return false;
  }
  if (Math.abs(values[28] - (values[16] + values[8])) > COMBO_TOLERANCE) {
    return false;
  }
  if (Math.abs(values[29] - (values[16] + values[11])) > COMBO_TOLERANCE) {
    return false;
  }
  if (Math.abs(values[30] - (values[1] - values[8])) > COMBO_TOLERANCE) {
    return false;
  }
  if (Math.abs(values[31] - (values[2] - values[9])) > COMBO_TOLERANCE) {
    return false;
  }
  return Math.abs(values[32] - (values[0] - values[7])) <= COMBO_TOLERANCE;
}

let nativeHeapModule = null;
let nativeHeapScan = null;
let nativeHeapCallback = null;
let profileHeapScanStarted = false;

function installNativeHeapScanner() {
  if (PROFILE !== 'swipe' && PROFILE !== 'swipe-backoff') return false;
  const statsOffset = PROFILE === 'swipe' ? 43 : 38;
  const layoutCheck = PROFILE === 'swipe' ?
      'if (!(x[38] == 0.0f || x[38] == 1.0f)) return 0;' : '';
  const source = `
    #include <stdint.h>
    #include <stddef.h>
    typedef void (*emit_t)(const float*);
    static int finite_range(float value, float low, float high) {
      return value == value && value >= low && value <= high;
    }
    static int valid(const float* x) {
      for (size_t i = 0; i < ${FEATURE_COUNT}; ++i) {
        if (!(x[i] == x[i])) return 0;
      }
      if (!finite_range(x[0], 0.0f, 50000.0f) ||
          !finite_range(x[1], 0.0f, 1000.0f) ||
          !finite_range(x[2], 0.0f, 200.0f)) return 0;
      if (x[3] != (float)(int)x[3] || x[3] < 0.0f || x[3] > 16.0f ||
          x[4] != (float)(int)x[4] || x[4] < 2.0f || x[4] > 32.0f ||
          x[8] != (float)(int)x[8] || x[8] < 2.0f || x[8] > 512.0f ||
          !finite_range(x[9], 0.0001f, 10000.0f)) return 0;
      const int groups[3][5] = {
        {13, 14, 15, 16, 17}, {18, 19, 20, 21, 22}, {30, 31, 27, 32, 33}
      };
      for (int i = 0; i < 3; ++i) {
        float minimum = x[groups[i][0]], maximum = x[groups[i][1]];
        float total = x[groups[i][2]], average = x[groups[i][3]];
        float stddev = x[groups[i][4]];
        if (!finite_range(minimum, 0.0f, 100000.0f) ||
            !finite_range(maximum, 0.0f, 100000.0f) ||
            !finite_range(total, 0.0f, 1000000.0f) ||
            !finite_range(average, 0.0f, 100000.0f) ||
            !finite_range(stddev, 0.0f, 100000.0f) ||
            minimum > average + 0.001f || average > maximum + 0.001f ||
            total + 0.001f < maximum) return 0;
      }
      for (int i = 23; i <= 26; ++i) {
        if (!finite_range(x[i], -2.0f, 2.0f)) return 0;
      }
      if (!finite_range(x[27], 0.0001f, 10000.0f) ||
          !finite_range(x[34], 0.0001f, 5000.0f)) return 0;
      ${layoutCheck}
      if (x[${statsOffset}] > x[1] + 0.001f ||
          x[${statsOffset + 1}] > x[0] + 0.001f ||
          x[${statsOffset + 2}] + 0.001f < x[${statsOffset}] ||
          x[${statsOffset + 3}] > x[21] + 0.001f) return 0;
      return 1;
    }
    void scan(const uint8_t* base, size_t size, emit_t emit) {
      uintptr_t first = ((uintptr_t)base + 3u) & ~(uintptr_t)3u;
      uintptr_t end = (uintptr_t)base + size;
      const size_t bytes = ${FEATURE_COUNT}u * sizeof(float);
      for (uintptr_t p = first; p + bytes <= end; p += sizeof(float)) {
        const float* values = (const float*)p;
        if (valid(values)) emit(values);
      }
    }
  `;
  try {
    nativeHeapModule = new CModule(source);
    nativeHeapScan = new NativeFunction(
        nativeHeapModule.scan, 'void', ['pointer', 'ulong', 'pointer']);
    nativeHeapCallback = new NativeCallback(function(address) {
      if (emittedVectors >= CONFIG.maxVectors) return;
      const values = readVector(address);
      if (values === null) return;
      ++emittedVectors;
      send({
        type: 'feature-vector', offset: 'heap-' + PROFILE, arg: -1,
        indirection: -1, vectorAddress: address.toString(),
        prefix: lastComposing, suggestion: lastSuggestion,
        sequence: emittedVectors, features: values
      });
    }, 'void', ['pointer']);
    return true;
  } catch (error) {
    status('native heap scanner unavailable: ' + error);
    return false;
  }
}

function scanProfileFeatureVectors() {
  if (profileHeapScanStarted) return;
  profileHeapScanStarted = true;
  if (!installNativeHeapScanner()) return;
  const ranges = Process.enumerateRanges({protection: 'rw-', coalesce: true})
      .filter((range) => range.size >= FEATURE_COUNT * 4 &&
          range.size < 128 * 1024 * 1024 &&
          !(range.file && range.file.path.indexOf('/dev/kgsl') === 0));
  let index = 0;
  function next() {
    if (emittedVectors >= CONFIG.maxVectors || index >= ranges.length) {
      status('heap ' + PROFILE + ' scan completed: ranges=' + index +
          ', vectors=' + emittedVectors);
      return;
    }
    const range = ranges[index++];
    try {
      nativeHeapScan(range.base, range.size, nativeHeapCallback);
    } catch (error) {
      status('heap ' + PROFILE + ' range failed: ' + error);
    }
    setImmediate(next);
  }
  next();
}

function readVector(address) {
  const bytes = FEATURE_COUNT * 4;
  if (!readableRange(address, bytes)) return null;
  const values = [];
  try {
    for (let i = 0; i < FEATURE_COUNT; ++i) {
      values.push(address.add(i * 4).readFloat());
    }
  } catch (_) {
    return null;
  }
  return validateVector(values) ? values : null;
}

function emitVector(target, argIndex, address, indirection, values) {
  if (emittedVectors >= CONFIG.maxVectors) return;
  emittedVectors += 1;
  send({
    type: 'feature-vector',
    offset: relativeOffset(target),
    arg: argIndex,
    indirection: indirection,
    vectorAddress: address.toString(),
    prefix: lastComposing,
    suggestion: lastSuggestion,
    sequence: emittedVectors,
    features: values
  });
}

function scanArgument(target, argIndex, value) {
  const seen = new Set();

  function tryAddress(address, indirection) {
    const key = address.toString();
    if (seen.has(key)) return;
    seen.add(key);
    const vector = readVector(address);
    if (vector !== null) emitVector(target, argIndex, address, indirection, vector);
  }

  tryAddress(value, 0);
  if (!readableRange(value, 96)) return;
  for (let offset = 0; offset < 96; offset += Process.pointerSize) {
    try {
      const child = value.add(offset).readPointer();
      if (!child.isNull()) tryAddress(child, 1);
    } catch (_) {
      // The range check is deliberately conservative; ignore racing mappings.
    }
  }
}

function attachCaptureTarget(offsetText) {
  const target = ykeyboard.base.add(ptr(offsetText));
  if (!insideModule(target)) {
    status('skip target outside module: ' + offsetText);
    return;
  }
  const listener = Interceptor.attach(target, {
    onEnter(args) {
      if (emittedVectors >= CONFIG.maxVectors) return;
      for (let i = 0; i < 8; ++i) scanArgument(target, i, args[i]);
    }
  });
  attachedTargets.push(listener);
  status('capture target attached: ' + offsetText);
}

function installJavaContextHooks() {
  Java.perform(function () {
    try {
      const wrapper = Java.use('android.view.inputmethod.InputConnectionWrapper');
      const composing = wrapper.setComposingText.overload(
          'java.lang.CharSequence', 'int');
      composing.implementation = function (text, cursor) {
        lastComposing = text === null ? '' : String(text);
        return composing.call(this, text, cursor);
      };
    } catch (error) {
      status('setComposingText hook unavailable: ' + error);
    }

    try {
      const suggestView = Java.use(
          'ru.yandex.androidkeyboard.suggest.ui.suggestion.SuggestTextView');
      const setText = suggestView.setText.overload('java.lang.CharSequence');
      setText.implementation = function (text) {
        const value = text === null ? '' : String(text);
        if (value.length > 0) {
          lastSuggestion = value;
          send({ type: 'suggestion-text', text: value, prefix: lastComposing });
        }
        return setText.call(this, text);
      };
    } catch (error) {
      status('SuggestTextView hook unavailable: ' + error);
    }
  });
}

function findGetSuggestions() {
  const match = ykeyboard.enumerateExports().find(function (entry) {
    return entry.name.indexOf('DictionaryFacilitator_getSuggestions') >= 0;
  });
  if (match === undefined) throw new Error('getSuggestions export not found');
  return match.address;
}

function beginDiscovery() {
  const tid = Process.getCurrentThreadId();
  const state = { tid: tid, summary: Object.create(null) };
  followedThreads.add(tid);
  // Omitting the thread id makes Stalker instrument the current Java-callback
  // thread immediately.  Supplying the same id here defers activation until
  // the callback returns, which misses the synchronous native call below.
  Stalker.follow({
    events: { call: true },
    onCallSummary: (summary) => {
      for (const rawTarget in summary) {
        const target = ptr(rawTarget);
        if (!insideModule(target)) continue;
        const offset = relativeOffset(target);
        state.summary[offset] = (state.summary[offset] || 0) + summary[rawTarget];
      }
    }
  });
  return state;
}

function finishDiscovery(state) {
  Stalker.unfollow(state.tid);
  Stalker.flush();
  followedThreads.delete(state.tid);
  // onCallSummary is delivered asynchronously after flush.  Emitting inline
  // here produces a plausible but empty summary before Frida drains it.
  setTimeout(function () {
    const targets = Object.keys(state.summary).map((offset) => ({
      offset: offset,
      count: state.summary[offset]
    }));
    targets.sort((a, b) => b.count - a.count ||
        parseInt(a.offset, 16) - parseInt(b.offset, 16));
    send({
      type: 'call-summary',
      calls: targets.reduce((sum, item) => sum + item.count, 0),
      targets: targets.slice(0, 600),
      prefix: lastComposing
    });
  }, 50);
}

function installNativeDiscoveryHook() {
  const getSuggestions = findGetSuggestions();
  Interceptor.attach(getSuggestions, {
    onEnter(_) { this.discovery = beginDiscovery(); },
    onLeave(_) { finishDiscovery(this.discovery); }
  });
  status('native discovery hook attached at ' + relativeOffset(getSuggestions));
}

function installJavaDiscoveryHook() {
  Java.perform(function () {
    try {
      const facilitator = Java.use(
          'ru.yandex.androidkeyboard.nativecode.Native$DictionaryFacilitator');
      javaFacilitator = facilitator;
      const getSuggestions = facilitator.getSuggestions.overload('[B');
      const artMethod = ptr(getSuggestions.handle);
      let registeredNative = null;
      for (let offset = 0; offset < 64; offset += 4) {
        try {
          const candidate = artMethod.add(offset).readPointer();
          if (insideModule(candidate)) {
            registeredNative = candidate;
            break;
          }
        } catch (_) {}
      }
      if (registeredNative === null) {
        throw new Error('registered native entry not found in ArtMethod');
      }
      Interceptor.attach(registeredNative, {
        onEnter(_) { this.discovery = beginDiscovery(); },
        onLeave(_) { finishDiscovery(this.discovery); }
      });
      status('registered native discovery hook attached at ' +
          relativeOffset(registeredNative));
      getSuggestions.implementation = function (inputBytes) {
        const copy = [];
        for (let i = 0; i < inputBytes.length; ++i) copy.push(inputBytes[i]);
        lastRequestBytes = Java.array('byte', copy);
        return getSuggestions.call(this, inputBytes);
      };
      status('Java discovery hook attached to DictionaryFacilitator.getSuggestions([B)');
    } catch (error) {
      status('Java discovery hook unavailable: ' + error);
      installNativeDiscoveryHook();
    }
  });
}

function readVarint(bytes, cursor) {
  let value = 0;
  let shift = 0;
  while (cursor.index < bytes.length && shift < 35) {
    const byte = bytes[cursor.index++] & 0xff;
    value |= (byte & 0x7f) << shift;
    if ((byte & 0x80) === 0) return value >>> 0;
    shift += 7;
  }
  return null;
}

function extractPrefixBytes(bytes) {
  const cursor = { index: 0 };
  while (cursor.index < bytes.length) {
    const tag = readVarint(bytes, cursor);
    if (tag === null) return null;
    const field = tag >>> 3;
    const wire = tag & 7;
    if (wire === 0) {
      if (readVarint(bytes, cursor) === null) return null;
    } else if (wire === 1) {
      cursor.index += 8;
    } else if (wire === 2) {
      const length = readVarint(bytes, cursor);
      if (length === null || cursor.index + length > bytes.length) return null;
      if (field === 5) {
        const value = [];
        for (let i = 0; i < length; ++i) value.push(bytes[cursor.index + i]);
        return value;
      }
      cursor.index += length;
    } else if (wire === 5) {
      cursor.index += 4;
    } else {
      return null;
    }
  }
  return null;
}

function decodeUtf8Bytes(bytes) {
  let result = '';
  for (let i = 0; i < bytes.length;) {
    const first = bytes[i++] & 0xff;
    let codepoint = first;
    let continuation = 0;
    if ((first & 0xe0) === 0xc0) {
      codepoint = first & 0x1f;
      continuation = 1;
    } else if ((first & 0xf0) === 0xe0) {
      codepoint = first & 0x0f;
      continuation = 2;
    } else if ((first & 0xf8) === 0xf0) {
      codepoint = first & 0x07;
      continuation = 3;
    }
    if (i + continuation > bytes.length) return '';
    for (let j = 0; j < continuation; ++j) {
      codepoint = (codepoint << 6) | (bytes[i++] & 0x3f);
    }
    if (codepoint <= 0xffff) {
      result += String.fromCharCode(codepoint);
    } else {
      codepoint -= 0x10000;
      result += String.fromCharCode(
          0xd800 + (codepoint >>> 10), 0xdc00 + (codepoint & 0x3ff));
    }
  }
  return result;
}

function floatPattern(value) {
  const buffer = new ArrayBuffer(4);
  new DataView(buffer).setFloat32(0, value, true);
  return Array.from(new Uint8Array(buffer))
      .map((byte) => byte.toString(16).padStart(2, '0')).join(' ');
}

function scanExactFeatureVector(prefix, originalLength) {
  const ranges = Process.enumerateRanges({protection: 'rw-', coalesce: true})
      .filter((range) => range.size >= FEATURE_COUNT * 4 &&
          range.size < 128 * 1024 * 1024 &&
          !(range.file && range.file.path.indexOf('/dev/kgsl') === 0));
  let rangeIndex = 0;
  let originalKnown = 0;
  const seen = new Set();

  function next() {
    if (emittedVectors >= CONFIG.maxVectors) return;
    if (rangeIndex >= ranges.length) {
      rangeIndex = 0;
      ++originalKnown;
      if (originalKnown > 1) {
        status('heap exact scan completed without a validated vector for ' +
            JSON.stringify(prefix));
        return;
      }
    }
    const range = ranges[rangeIndex++];
    const pattern = floatPattern(originalLength) + ' ' + '?? '.repeat(20) +
        floatPattern(originalKnown) + ' ' + floatPattern(originalLength) +
        ' ' + '?? '.repeat(24) + floatPattern(1);
    Memory.scan(range.base, range.size, pattern, {
      onMatch(address) {
        const key = address.toString();
        if (seen.has(key)) return;
        seen.add(key);
        const values = readVector(address);
        if (values === null || Math.abs(values[14] - 1.0) > COMBO_TOLERANCE ||
            Math.abs(values[1] - values[8]) > COMBO_TOLERANCE) return;
        ++emittedVectors;
        send({
          type: 'feature-vector', offset: 'heap-exact', arg: -1,
          indirection: -1, vectorAddress: key, prefix: prefix,
          suggestion: prefix, sequence: emittedVectors, features: values
        });
        return 'stop';
      },
      onError(error) { status('heap scan error: ' + error); },
      onComplete() { setImmediate(next); }
    });
  }
  next();
}

function scanAllFeatureVectors(originalLength) {
  const ranges = Process.enumerateRanges({protection: 'rw-', coalesce: true})
      .filter((range) => range.size >= FEATURE_COUNT * 4 &&
          range.size < 128 * 1024 * 1024 &&
          !(range.file && range.file.path.indexOf('/dev/kgsl') === 0));
  const pattern = floatPattern(originalLength);
  const seen = new Set();
  let rangeIndex = 0;

  function next() {
    if (emittedVectors >= CONFIG.maxVectors || rangeIndex >= ranges.length) {
      status('heap vector scan completed: originalLength=' + originalLength +
          ', vectors=' + emittedVectors);
      return;
    }
    const range = ranges[rangeIndex++];
    Memory.scan(range.base, range.size, pattern, {
      onMatch(address) {
        const key = address.toString();
        if (seen.has(key)) return;
        seen.add(key);
        const values = readVector(address);
        if (values === null ||
            Math.abs(values[0] - originalLength) > COMBO_TOLERANCE) return;
        ++emittedVectors;
        send({
          type: 'feature-vector', offset: 'heap-all', arg: -1,
          indirection: -1, vectorAddress: key, prefix: lastComposing,
          suggestion: lastSuggestion, sequence: emittedVectors,
          features: values
        });
        if (emittedVectors >= CONFIG.maxVectors) return 'stop';
      },
      onError(error) { status('heap vector scan error: ' + error); },
      onComplete() { setImmediate(next); }
    });
  }
  next();
}

function installJavaHeapCaptureHook() {
  Java.perform(function () {
    const isSwipeProfile = PROFILE === 'swipe' || PROFILE === 'swipe-backoff' ||
        PROFILE === 'swipe-blocker';
    const owner = Java.use(isSwipeProfile ?
        'ru.yandex.androidkeyboard.nativecode.Native$InputModel' :
        'ru.yandex.androidkeyboard.nativecode.Native$DictionaryFacilitator');
    const getSuggestions = isSwipeProfile ? owner.update.overload('[B') :
        owner.getSuggestions.overload('[B');
    let scanStarted = false;
    let requestEmitted = false;
    getSuggestions.implementation = function (inputBytes) {
      let pendingScan = null;
      if (isSwipeProfile && !requestEmitted) {
        requestEmitted = true;
        send({
          type: 'request-bytes',
          bytes: Array.from(inputBytes).map((byte) => byte & 0xff)
        });
      }
      if (!scanStarted) {
        const prefixBytes = isSwipeProfile ? null : extractPrefixBytes(inputBytes);
        if (isSwipeProfile) {
          scanStarted = true;
          pendingScan = {swipe: true};
        }
        if (prefixBytes !== null && prefixBytes.length > 0) {
          const prefix = decodeUtf8Bytes(prefixBytes);
          const prefixLength = Array.from(prefix).length;
          if (prefix.length === 0 || prefixLength < 3) {
            return getSuggestions.call(this, inputBytes);
          }
          if (!requestEmitted) {
            requestEmitted = true;
            send({
              type: 'request-bytes',
              bytes: Array.from(inputBytes).map((byte) => byte & 0xff)
            });
          }
          scanStarted = true;
          pendingScan = {prefix: prefix, length: prefixLength};
        }
      }
      const output = getSuggestions.call(this, inputBytes);
      if (pendingScan !== null) {
        setImmediate(function () {
          if (pendingScan.swipe === true) scanProfileFeatureVectors();
          else scanExactFeatureVector(pendingScan.prefix, pendingScan.length);
        });
      }
      return output;
    };
    status('Java heap capture hook attached to ' +
        (isSwipeProfile ? 'InputModel.update([B)' :
          'DictionaryFacilitator.getSuggestions([B)'));
  });
}

if (CONFIG.captureContext === true) installJavaContextHooks();
if (CONFIG.mode === 'discover') {
  if (CONFIG.captureJava === true) installJavaDiscoveryHook();
  else installNativeDiscoveryHook();
} else if (CONFIG.mode === 'capture') {
  if (CONFIG.captureJava === true && CONFIG.targets.length === 0) {
    installJavaHeapCaptureHook();
  } else {
    for (const offset of CONFIG.targets) attachCaptureTarget(offset);
  }
} else {
  throw new Error('unsupported mode: ' + CONFIG.mode);
}

rpc.exports = {
  scanheap(originalLength) {
    if (!Number.isInteger(originalLength) || originalLength <= 0 ||
        originalLength > 32) return 0;
    scanAllFeatureVectors(originalLength);
    return 1;
  },
  replay(count) {
    if (javaFacilitator === null || lastRequestBytes === null || count <= 0) {
      return 0;
    }
    let completed = 0;
    Java.perform(function () {
      for (let i = 0; i < count; ++i) {
        javaFacilitator.getSuggestions(lastRequestBytes);
        ++completed;
      }
    });
    return completed;
  },
  stop() {
    for (const tid of followedThreads) Stalker.unfollow(tid);
    followedThreads.clear();
    for (const listener of attachedTargets) listener.detach();
    Stalker.garbageCollect();
    return emittedVectors;
  }
};
