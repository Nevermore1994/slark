#!/bin/bash
# Unified script for mobile emulator integration tests
#
# Output: detailed logs -> test/output/<YYYYMMDD_HHMMSS>/
#         terminal      -> per-test-case PASS/FAIL/SKIP names + summary
#
# Usage: ./scripts/run_integration_test.sh [<ios|android|all>] [args...]
#   all (default): run iOS + Android integration tests sequentially
#   ios:          first optional arg is device name (default iPhone 17 Pro),
#                 remaining args are passed to gtest
#   android:      remaining args are passed to gradle assemble tasks
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
PLATFORM="${1:-all}"
shift || true

GREEN='\033[0;32m'; RED='\033[0;31m'; YELLOW='\033[1;33m'; CYAN='\033[0;36m'; NC='\033[0m'

TIMESTAMP="$(date +%Y%m%d_%H%M%S)"
OUTPUT_DIR="${ROOT_DIR}/test/output/${TIMESTAMP}"
mkdir -p "${OUTPUT_DIR}"

echo -e "${CYAN}============================================${NC}"
echo -e "${CYAN} Slark Mobile Integration Tests${NC}"
echo -e "${CYAN}============================================${NC}"
echo " Platform : ${PLATFORM}"
echo " Output   : ${OUTPUT_DIR}"
echo -e "${CYAN}============================================${NC}"

PASS_COUNT=0; FAIL_COUNT=0; SKIP_COUNT=0
ALL_FAILED=()

# Print one test-case result and update counters
report_case() {
    local platform="$1" status="$2" name="$3"
    case "${status}" in
        PASS) PASS_COUNT=$((PASS_COUNT + 1)); echo -e "  ${GREEN}✓${NC} [${platform}] ${name}" ;;
        FAIL) FAIL_COUNT=$((FAIL_COUNT + 1)); ALL_FAILED+=("${name}"); echo -e "  ${RED}✗${NC} [${platform}] ${name}" ;;
        SKIP) SKIP_COUNT=$((SKIP_COUNT + 1)); echo -e "  ${YELLOW}-${NC} [${platform}] ${name} (skipped)" ;;
    esac
}

# Parse gtest text log (FAILED lines need the timing marker to exclude the
# duplicated list in the final summary section; test name is field 4)
collect_gtest() {
    local platform="$1" log_file="$2" parsed
    parsed="$(awk '/^\[       OK \]/ && / ms\)/{print "PASS\t" $4; next}
                  /^\[  SKIPPED \]/ && / ms\)/{print "SKIP\t" $4; next}
                  /^\[  FAILED  \]/ && / ms\)/{print "FAIL\t" $4}' "${log_file}")"
    while IFS=$'\t' read -r status name; do
        report_case "${platform}" "${status}" "${name}"
    done <<< "${parsed}"
}

# Parse am instrument log (-r raw mode: test= lines + STATUS_CODE lines)
collect_instrumentation() {
    local platform="$1" log_file="$2" parsed
    parsed="$(awk '/INSTRUMENTATION_STATUS: test=/{split($0, a, "="); test=a[2]; next}
                  /INSTRUMENTATION_STATUS_CODE: 0/{print "PASS\t" test; next}
                  /INSTRUMENTATION_STATUS_CODE: -1/{print "FAIL\t" test; next}
                  /INSTRUMENTATION_STATUS_CODE: -2/{print "FAIL\t" test}' "${log_file}")"
    while IFS=$'\t' read -r status name; do
        report_case "${platform}" "${status}" "${name}"
    done <<< "${parsed}"
}

# ─── iOS: gtest binary (test/iOS + ios.toolchain.cmake), simctl install + launch ───
run_ios() {
    local BUILD_DIR="${ROOT_DIR}/cmake-build-Debug/iOSTest"
    local DEVICE_NAME="${1:-iPhone 17 Pro}"
    shift || true
    local BUNDLE_ID="com.slark.PlayerMediaIntegrationTest"

    if [ ! -f "${BUILD_DIR}/CMakeCache.txt" ]; then
        cmake -B "${BUILD_DIR}" -G Xcode \
            -DCMAKE_TOOLCHAIN_FILE="${ROOT_DIR}/ios.toolchain.cmake" \
            -DPLATFORM=OS64 -DDEPLOYMENT_TARGET=16.5 -DENABLE_BITCODE=FALSE \
            -DCMAKE_BUILD_TYPE=Debug "${ROOT_DIR}/test/iOS"
    fi

    echo -e "${YELLOW}[iOS] Building simulator tests...${NC}"
    # Explicit -destination avoids "supported platforms is empty" scheme error
    if ! xcodebuild -project "${BUILD_DIR}/PlayerMediaIntegration_test.xcodeproj" \
        -scheme PlayerMediaIntegration_test -configuration Debug \
        -destination 'generic/platform=iOS Simulator' \
        build CODE_SIGNING_ALLOWED=NO >"${OUTPUT_DIR}/ios_build.log" 2>&1; then
        echo -e "${RED}[iOS] Build failed, see ${OUTPUT_DIR}/ios_build.log${NC}"
        exit 1
    fi

    # CMake-generated Info.plist lacks CFBundleIdentifier; inject it
    local APP="${BUILD_DIR}/Debug-iphonesimulator/PlayerMediaIntegration_test.app"
    /usr/libexec/PlistBuddy -c "Delete :CFBundleIdentifier" "${APP}/Info.plist" 2>/dev/null || true
    /usr/libexec/PlistBuddy -c "Add :CFBundleIdentifier string ${BUNDLE_ID}" "${APP}/Info.plist"

    local UDID
    UDID=$(xcrun simctl list devices available | grep -E "${DEVICE_NAME} \(" | grep -oE '[0-9A-F-]{36}' | head -1)
    if [ -z "${UDID}" ]; then
        echo "simulator not found: ${DEVICE_NAME}"
        exit 1
    fi
    xcrun simctl boot "${UDID}" 2>/dev/null || true
    xcrun simctl bootstatus "${UDID}" -b >/dev/null

    xcrun simctl terminate "${UDID}" "${BUNDLE_ID}" 2>/dev/null || true
    xcrun simctl install "${UDID}" "${APP}"

    echo -e "${YELLOW}[iOS] Running gtest (${DEVICE_NAME})...${NC}"
    set +e
    # Media dir injected via SIMCTL_CHILD_ (simulator can access /Users host paths)
    SIMCTL_CHILD_SLARK_TEST_MEDIA_DIR="${ROOT_DIR}/test_sample" \
        xcrun simctl launch --console "${UDID}" "${BUNDLE_ID}" --gtest_color=no "$@" \
        >"${OUTPUT_DIR}/ios_test.log" 2>&1
    set -e

    echo -e "${CYAN}[iOS] Test results:${NC}"
    collect_gtest "iOS" "${OUTPUT_DIR}/ios_test.log"
}

# ─── Android: instrumentation tests (gradle build + adb on emulator only) ────────
# gradle connectedAndroidTest runs on ALL online devices (incl. real devices);
# we only assemble APKs via gradle, then install + am instrument on the emulator
# explicitly so tests always run on the emulator.
run_android() {
    local SDK="${ANDROID_HOME:-${HOME}/Library/Android/sdk}"
    local ADB="${SDK}/platform-tools/adb"
    local EMULATOR="${SDK}/emulator/emulator"
    local AVD_NAME="slark_test"
    local serial

    # Detect online emulator (adb may also see real devices; match by serial)
    serial="$("${ADB}" devices | awk '$1 ~ /^emulator-[0-9]+/ && $2 == "device" {print $1; exit}')"

    # Start emulator if none is online and wait for boot (keep audio enabled)
    if [ -z "${serial}" ]; then
        if ! "${EMULATOR}" -list-avds 2>/dev/null | grep -qx "${AVD_NAME}"; then
            echo "AVD not found: ${AVD_NAME}"
            exit 1
        fi
        "${EMULATOR}" -avd "${AVD_NAME}" -no-window -gpu swiftshader_indirect -no-snapshot \
            >/tmp/slark_emulator.log 2>&1 &
        "${ADB}" wait-for-device
        serial="$("${ADB}" devices | awk '$1 ~ /^emulator-[0-9]+/ && $2 == "device" {print $1; exit}')"
    fi

    local booted=0
    for _ in $(seq 1 30); do
        if [ "$("${ADB}" -s "${serial}" shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')" = "1" ]; then
            booted=1
            break
        fi
        sleep 5
    done
    if [ "${booted}" != "1" ]; then
        echo "emulator boot timeout"
        exit 1
    fi

    # 1. gradle assembles APK + test APK (build only, no test run)
    echo -e "${YELLOW}[Android] Building APKs...${NC}"
    set +e
    (cd "${ROOT_DIR}/demo/Android" \
        && ./gradlew :app:assembleDebug :app:assembleDebugAndroidTest --console=plain "$@") \
        >"${OUTPUT_DIR}/android_build.log" 2>&1
    local build_exit=$?
    set -e
    if [ "${build_exit}" -ne 0 ]; then
        echo -e "${RED}[Android] Build failed, see ${OUTPUT_DIR}/android_build.log${NC}"
        exit 1
    fi

    # 2. Install APKs on the target emulator
    local APP_APK="${ROOT_DIR}/demo/Android/app/build/outputs/apk/debug/app-debug.apk"
    local TEST_APK="${ROOT_DIR}/demo/Android/app/build/outputs/apk/androidTest/debug/app-debug-androidTest.apk"
    echo -e "${YELLOW}[Android] Installing APKs on ${serial}...${NC}"
    "${ADB}" -s "${serial}" install -r "${APP_APK}" >/dev/null 2>&1
    "${ADB}" -s "${serial}" install -r "${TEST_APK}" >/dev/null 2>&1

    # 3. am instrument runs tests on the emulator only
    echo -e "${YELLOW}[Android] Running instrumentation tests (${serial})...${NC}"
    set +e
    "${ADB}" -s "${serial}" shell am instrument -w -r \
        -e class com.slark.demo.PlayerMediaIntegrationTest \
        com.slark.demo.test/androidx.test.runner.AndroidJUnitRunner \
        >"${OUTPUT_DIR}/android_test.log" 2>&1
    local android_exit=$?
    set -e

    echo -e "${CYAN}[Android] Test results:${NC}"
    collect_instrumentation "Android" "${OUTPUT_DIR}/android_test.log"

    # am instrument exited non-zero with no parsed failures -> abnormal run
    if [ "${android_exit}" -ne 0 ] && [ "${FAIL_COUNT}" -eq 0 ]; then
        echo -e "${RED}[Android] Run failed (exit=${android_exit}), see ${OUTPUT_DIR}/android_test.log${NC}"
    fi
}

case "${PLATFORM}" in
    all)
        if [ $# -gt 0 ]; then
            echo "default mode runs both platforms, ignoring extra args"
        fi
        run_ios
        run_android
        ;;
    ios) run_ios "$@" ;;
    android) run_android "$@" ;;
    *) echo "unknown platform: ${PLATFORM} (expect ios|android|all)"; exit 1 ;;
esac

# ─── Summary ────────────────────────────────────────────────────────────────────
echo ""
echo -e "${CYAN}============================================${NC}"
echo -e "${CYAN} Mobile Integration Test Summary${NC}"
echo -e "${CYAN}============================================${NC}"
echo -e " ${GREEN}Passed: ${PASS_COUNT}${NC}"
if [ "${SKIP_COUNT}" -gt 0 ]; then
    echo -e " ${YELLOW}Skipped: ${SKIP_COUNT}${NC}"
fi
if [ ${#ALL_FAILED[@]} -gt 0 ]; then
    echo -e " ${RED}Failed: ${FAIL_COUNT}${NC}"
    echo -e "${RED}────────────────────────────────────────────${NC}"
    echo -e "${RED} FAILED TESTS:${NC}"
    for failed in "${ALL_FAILED[@]}"; do
        echo -e "  ${RED}✗${NC} ${failed}"
    done
    echo -e "${RED}────────────────────────────────────────────${NC}"
else
    echo -e " ${GREEN}All passed ✓${NC}"
fi
echo -e "${CYAN}============================================${NC}"
echo " Detailed output: ${OUTPUT_DIR}/"
echo -e "${CYAN}============================================${NC}"

if [ "${FAIL_COUNT}" -gt 0 ]; then
    exit 1
fi
exit 0
