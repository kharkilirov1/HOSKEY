#!/bin/bash
# Full pipeline: start emulator → install Yandex KB → run Frida → extract frequencies
set -e

ANDROID_HOME="$HOME/android-sdk"
ADB="$ANDROID_HOME/platform-tools/adb"
EMU="$ANDROID_HOME/emulator/emulator"
AVD_NAME="yandex_test"
APK="/mnt/c/Users/Kharki/clawd/yandex_new.apk"
FRIDA_SERVER="/mnt/c/Users/Kharki/Desktop/yandex_extracted/frida-server-17.6.2"
TOOLS="/mnt/c/Users/Kharki/DevEcoStudioProjects/HOSKEY/tools"
OUTPUT="$TOOLS/words_with_weights.tsv"

echo "=== Step 1: Create AVD ==="
"$ANDROID_HOME/cmdline-tools/latest/bin/avdmanager" create avd \
    -n "$AVD_NAME" \
    -k "system-images;android-30;google_apis;x86_64" \
    --force -d "pixel" 2>/dev/null || true
echo "AVD created"

echo "=== Step 2: Start emulator (headless) ==="
"$EMU" -avd "$AVD_NAME" -no-window -no-audio -no-boot-anim -gpu swiftshader_indirect &
EMU_PID=$!
echo "Emulator PID: $EMU_PID"

echo "Waiting for device..."
"$ADB" wait-for-device
"$ADB" shell 'while [[ -z $(getprop sys.boot_completed) ]]; do sleep 2; done'
echo "Device booted"

echo "=== Step 3: Install Yandex Keyboard ==="
"$ADB" install -r "$APK" 2>&1 | tail -3
echo "APK installed"

echo "=== Step 4: Enable Yandex Keyboard ==="
"$ADB" shell ime enable ru.yandex.androidkeyboard/.YandexKeyboard 2>/dev/null || \
"$ADB" shell ime enable com.yandex.keyboard/.YandexKeyboard 2>/dev/null || true
"$ADB" shell ime set ru.yandex.androidkeyboard/.YandexKeyboard 2>/dev/null || \
"$ADB" shell ime set com.yandex.keyboard/.YandexKeyboard 2>/dev/null || true
echo "Keyboard enabled"

echo "=== Step 5: Push frida-server ==="
"$ADB" root
sleep 2
"$ADB" push "$FRIDA_SERVER" /data/local/tmp/frida-server
"$ADB" shell chmod 755 /data/local/tmp/frida-server
"$ADB" shell "/data/local/tmp/frida-server -D &" 2>/dev/null &
sleep 3
echo "Frida server started"

echo "=== Step 6: Open text field (trigger keyboard) ==="
"$ADB" shell am start -a android.intent.action.VIEW -d "content://contacts" 2>/dev/null || \
"$ADB" shell am start -n com.android.settings/.Settings 2>/dev/null
sleep 3
# Tap on search/text field to bring up keyboard
"$ADB" shell input tap 540 200 2>/dev/null || true
sleep 2

echo "=== Step 7: Run Frida extraction ==="
cd "$TOOLS"
python3 frida_extract_frequencies.py --output "$OUTPUT" 2>&1

echo "=== Step 8: Rebuild container ==="
python3 build_hosdict.py

echo "=== Done ==="
echo "Weights: $(wc -l < "$OUTPUT") words"
echo "Container: $(ls -lh /mnt/c/Users/Kharki/DevEcoStudioProjects/HOSKEY/entry/src/main/resources/rawfile/dict/ru.hoskey.hosdict)"

# Cleanup
kill $EMU_PID 2>/dev/null || true
