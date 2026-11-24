#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
sdk="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}"
tools="$sdk/build-tools/36.0.0"
platform="$sdk/platforms/android-36/android.jar"
out="$PWD/build"
javac="${JAVA_HOME:+$JAVA_HOME/bin/}javac"
keytool="${JAVA_HOME:+$JAVA_HOME/bin/}keytool"
export PATH="${JAVA_HOME:+$JAVA_HOME/bin:}$PATH"

test -x "$tools/aapt2" && test -f "$platform" || {
    printf '%s\n' 'Install Android SDK platform 36 and build-tools 36.0.0, then set ANDROID_HOME.' >&2
    exit 1
}
mkdir -p "$out/classes" "$out/dex" "$out/generated"
find "$out/classes" "$out/dex" "$out/generated" -type f -delete
"$tools/aapt2" compile --dir res -o "$out/resources.zip"
"$tools/aapt2" link -I "$platform" --manifest AndroidManifest.xml --java "$out/generated" \
    -o "$out/resources.apk" "$out/resources.zip"
mapfile -d '' sources < <(find src "$out/generated" -name '*.java' -type f -print0)
"$javac" --release 8 -Xlint:-options -encoding UTF-8 -classpath "$platform" -d "$out/classes" "${sources[@]}"
jar --create --file "$out/classes.jar" -C "$out/classes" .
"$tools/d8" --release --min-api 34 --lib "$platform" --output "$out/dex" "$out/classes.jar"
cp "$out/resources.apk" "$out/unsigned.apk"
zip -q -j "$out/unsigned.apk" "$out/dex"/classes*.dex
"$tools/zipalign" -f 4 "$out/unsigned.apk" "$out/aligned.apk"

key="${ANDROID_DEBUG_KEYSTORE:-$HOME/.android/debug.keystore}"
if [[ ! -f "$key" ]]; then
    (umask 077
    mkdir -p "$(dirname "$key")"
    "$keytool" -genkeypair -keystore "$key" -storepass android -keypass android -alias androiddebugkey \
        -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=Android Debug,O=Android,C=US")
fi
"$tools/apksigner" sign --ks "$key" --ks-key-alias androiddebugkey --ks-pass pass:android \
    --v4-signing-enabled false --out "$out/plan-tasks-android-dev.apk" "$out/aligned.apk"
"$tools/apksigner" verify "$out/plan-tasks-android-dev.apk"
printf '%s\n' "$out/plan-tasks-android-dev.apk"
