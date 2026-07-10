#!/bin/bash
# Run in WSL: wsl.exe bash /mnt/c/Users/Kharki/DevEcoStudioProjects/HOSKEY/tools/wsl_extract.sh
set -x

export ANDROID_HOME=/home/kharki/android-sdk
ADB=$ANDROID_HOME/platform-tools/adb
TOOLS=/mnt/c/Users/Kharki/DevEcoStudioProjects/HOSKEY/tools
OUTPUT=$TOOLS/yandex_weights.tsv

# Check if emulator is running
$ADB devices
if ! $ADB devices 2>/dev/null | grep -q "emulator"; then
    echo "Starting emulator..."
    pkill -f qemu 2>/dev/null
    $ADB kill-server 2>/dev/null
    sleep 1
    $ANDROID_HOME/emulator/emulator -avd yandex_test -no-window -no-audio -no-boot-anim -gpu swiftshader_indirect -no-snapshot 2>/dev/null &
    sleep 5
    $ADB start-server
    $ADB wait-for-device
    for i in $(seq 1 90); do
        B=$($ADB shell getprop sys.boot_completed 2>/dev/null | tr -d "\r\n")
        [ "$B" = "1" ] && echo "BOOTED" && break
        sleep 2
    done
fi

# Install Yandex KB
echo "Installing APK..."
$ADB install -r -t /mnt/c/Users/Kharki/clawd/yandex_new.apk
PKG=$($ADB shell pm list packages | grep yandex | head -1 | cut -d: -f2 | tr -d "\r")
echo "Package: $PKG"

# Root + Frida
$ADB root
sleep 2
$ADB push /mnt/c/Users/Kharki/Desktop/yandex_extracted/frida-server-17.6.2 /data/local/tmp/frida-server
$ADB shell chmod 755 /data/local/tmp/frida-server
$ADB shell "nohup /data/local/tmp/frida-server -D &" 2>/dev/null
sleep 3

# Enable keyboard
$ADB shell ime list -a | grep -i yandex
$ADB shell ime enable "$PKG/.InputMethodService" 2>/dev/null
$ADB shell ime set "$PKG/.InputMethodService" 2>/dev/null

# Trigger keyboard
$ADB shell am start -a android.settings.SETTINGS 2>/dev/null
sleep 2
$ADB shell input tap 540 200 2>/dev/null
sleep 2

echo "=== READY ==="

# Run extraction
cd $TOOLS
python3 frida_extract_frequencies.py --output "$OUTPUT" 2>&1

echo "=== EXTRACTION DONE ==="
wc -l "$OUTPUT"
head -20 "$OUTPUT"
