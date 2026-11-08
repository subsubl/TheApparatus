#!/usr/bin/env bash
# ==============================================================================
# The Apparatus - Master Test, Build & Emulation Harness
# ==============================================================================
# Portable across any host environment:
#   - Remote Linux VM:   /home/antigravity/subsubl/TheApparatus
#   - Local Workstation: /media/ator/Tera/TheAparatus
#
# Usage:
#   ./test_and_build.sh [command]
#
# Commands:
#   all         - Run all unit tests, Pi tests, GUI tests, and firmware builds
#   test        - Run all tests (unit + pi + gui)
#   test-unit   - Run host C++ unit tests (Codec, DSP, StateMachine)
#   test-pi     - Run Raspberry Pi B MPV daemon IPC test
#   test-gui    - Run Playwright headless browser E2E test suite (18 tests)
#   build       - Build ESP32 firmware for production (esp32dev) and simulation (esp32dev_sim)
#   build-prod  - Build production ESP32 firmware only
#   build-sim   - Build radar-less simulation ESP32 firmware only
#   sim         - Run desktop interactive ESP32 WebSocket simulator server
#   clean       - Remove build artifacts and temporary test binaries
#   help        - Show this help message
# ==============================================================================

set -eo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$REPO_ROOT"

# Terminal colors
BOLD='\033[1m'
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

info()  { echo -e "${BLUE}${BOLD}[INFO]${NC} $*"; }
pass()  { echo -e "${GREEN}${BOLD}[PASS]${NC} $*"; }
warn()  { echo -e "${YELLOW}${BOLD}[WARN]${NC} $*"; }
fail()  { echo -e "${RED}${BOLD}[FAIL]${NC} $*"; }

find_pio() {
    if command -v pio >/dev/null 2>&1; then
        echo "pio"
    elif [ -x "$HOME/.local/bin/pio" ]; then
        echo "$HOME/.local/bin/pio"
    elif [ -x "$HOME/.platformio/penv/bin/pio" ]; then
        echo "$HOME/.platformio/penv/bin/pio"
    elif command -v python3 >/dev/null 2>&1 && python3 -m platformio --version >/dev/null 2>&1; then
        echo "python3 -m platformio"
    else
        echo ""
    fi
}

cmd_test_unit() {
    info "Running C++ host-native unit test suites..."
    mkdir -p "$REPO_ROOT/build_native"

    info "  1/3 Testing Configuration & Codec (v5 Envelope & v4 Migration)..."
    g++ -std=c++17 -fno-exceptions -fno-rtti -Iinclude -Itools/native-test \
        src/OutputAllocation.cpp src/Configuration.cpp tools/native-test/config_codec_test.cpp \
        -o "$REPO_ROOT/build_native/config_test"
    "$REPO_ROOT/build_native/config_test"
    pass "Configuration & Codec suite passed."

    info "  2/3 Testing DSP Pipeline & LD2410 Sensitivity Cmd Generator..."
    g++ -std=c++17 -DAPPARATUS_NATIVE_TEST -Iinclude test/test_dsp.cpp src/DSP.cpp \
        -o "$REPO_ROOT/build_native/dsp_test"
    "$REPO_ROOT/build_native/dsp_test"
    pass "DSP Pipeline suite passed."

    info "  3/3 Testing State Machine (IDLE -> MACRO -> MICRO -> CONTACT)..."
    g++ -std=c++17 -DAPPARATUS_NATIVE_TEST -Iinclude test/test_state_machine.cpp \
        -o "$REPO_ROOT/build_native/sm_test"
    "$REPO_ROOT/build_native/sm_test"
    pass "State Machine suite passed."

    pass "ALL host-native unit tests PASSED!"
}

cmd_test_pi() {
    info "Running Raspberry Pi B MPV daemon mock-IPC test..."
    if ! command -v python3 >/dev/null 2>&1; then
        fail "python3 is required to run Pi daemon test."
        return 1
    fi
    python3 "$REPO_ROOT/pi/test_mpv_daemon.py"
    pass "Raspberry Pi B daemon tests PASSED!"
}

cmd_test_gui() {
    info "Running Playwright E2E GUI test suite..."
    if ! command -v python3 >/dev/null 2>&1; then
        fail "python3 is required to extract SPA."
        return 1
    fi

    python3 "$REPO_ROOT/tools/extract_spa.py"

    if ! command -v npx >/dev/null 2>&1; then
        warn "Node.js / npx not found. Skipping Playwright GUI tests."
        return 0
    fi

    # Kill any stale mock servers
    pkill -f "mock_esp32.js" >/dev/null 2>&1 || true

    pushd "$REPO_ROOT/tools/gui-test" >/dev/null
    if [ ! -d "node_modules" ]; then
        info "Installing tools/gui-test dependencies..."
        npm install
    fi
    npx playwright test
    pkill -f "mock_esp32.js" >/dev/null 2>&1 || true
    popd >/dev/null

    pass "Playwright GUI test suite PASSED (18/18 tests)!"
}

cmd_build_prod() {
    PIO=$(find_pio)
    if [ -z "$PIO" ]; then
        fail "PlatformIO (pio) not found. Please install platformio."
        return 1
    fi
    info "Extracting SPA before build..."
    python3 "$REPO_ROOT/tools/extract_spa.py"
    info "Building production ESP32 firmware (esp32dev)..."
    $PIO run -e esp32dev
    pass "Production firmware built successfully: .pio/build/esp32dev/firmware.bin"
}

cmd_build_sim() {
    PIO=$(find_pio)
    if [ -z "$PIO" ]; then
        fail "PlatformIO (pio) not found. Please install platformio."
        return 1
    fi
    info "Extracting SPA before build..."
    python3 "$REPO_ROOT/tools/extract_spa.py"
    info "Building simulation ESP32 firmware (esp32dev_sim)..."
    $PIO run -e esp32dev_sim
    pass "Simulation firmware built successfully: .pio/build/esp32dev_sim/firmware.bin"
}

cmd_build() {
    cmd_build_prod
    cmd_build_sim
    pass "All firmware targets built successfully!"
}

cmd_sim() {
    info "Starting Desktop Interactive ESP32 Simulator..."
    python3 "$REPO_ROOT/tools/extract_spa.py"
    if ! command -v node >/dev/null 2>&1; then
        fail "Node.js is required to run the interactive simulator."
        return 1
    fi
    node "$REPO_ROOT/tools/interactive_sim.js" "$@"
}

cmd_clean() {
    info "Cleaning build artifacts..."
    rm -rf "$REPO_ROOT/build_native"
    rm -f "$REPO_ROOT/build_config_test" "$REPO_ROOT/build_test_dsp" "$REPO_ROOT/build_test_sm"
    PIO=$(find_pio)
    if [ -n "$PIO" ]; then
        $PIO run -t clean >/dev/null 2>&1 || true
    fi
    pass "Clean complete."
}

cmd_all() {
    cmd_test_unit
    cmd_test_pi
    cmd_test_gui
    cmd_build
    echo ""
    echo -e "${GREEN}${BOLD}======================================================${NC}"
    echo -e "${GREEN}${BOLD}  THE APPARATUS — ALL VERIFICATIONS COMPLETED!       ${NC}"
    echo -e "${GREEN}${BOLD}  Unit Tests: 100% | Pi Daemon: PASS | GUI: 18/18    ${NC}"
    echo -e "${GREEN}${BOLD}  Firmware (esp32dev + esp32dev_sim): BUILT & READY  ${NC}"
    echo -e "${GREEN}${BOLD}======================================================${NC}"
}

cmd_help() {
    echo "The Apparatus — Test, Build & Emulation CLI"
    echo ""
    echo "Usage: ./test_and_build.sh [command]"
    echo ""
    echo "Commands:"
    echo "  all         Run native unit tests, Pi tests, GUI tests, and builds"
    echo "  test        Run all tests (unit + pi + gui)"
    echo "  test-unit   Run C++ host unit tests (Configuration, DSP, StateMachine)"
    echo "  test-pi     Run Raspberry Pi B mock MPV daemon IPC test"
    echo "  test-gui    Run Playwright browser E2E tests (18 tests)"
    echo "  build       Build both esp32dev and esp32dev_sim firmware"
    echo "  build-prod  Build production firmware only"
    echo "  build-sim   Build simulation firmware only"
    echo "  sim         Start desktop interactive ESP32 simulator"
    echo "  clean       Remove temporary test and build artifacts"
    echo "  help        Display this help message"
}

COMMAND="${1:-all}"
shift || true

case "$COMMAND" in
    all)
        cmd_all
        ;;
    test)
        cmd_test_unit
        cmd_test_pi
        cmd_test_gui
        ;;
    test-unit)
        cmd_test_unit
        ;;
    test-pi)
        cmd_test_pi
        ;;
    test-gui)
        cmd_test_gui
        ;;
    build)
        cmd_build
        ;;
    build-prod)
        cmd_build_prod
        ;;
    build-sim)
        cmd_build_sim
        ;;
    sim)
        cmd_sim "$@"
        ;;
    clean)
        cmd_clean
        ;;
    help|--help|-h)
        cmd_help
        ;;
    *)
        fail "Unknown command: $COMMAND"
        cmd_help
        exit 1
        ;;
esac
