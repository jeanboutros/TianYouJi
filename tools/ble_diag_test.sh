#!/usr/bin/env bash
#
# ble_diag_test.sh — Full BLE diagnostic test for ESP32 nRF24L01+ sniffer
#
# Orchestrates a complete end-to-end test:
#   1. Flashes the ESP32 with the latest firmware (optional, --no-flash to skip)
#   2. Captures ESP32 serial output to a timestamped log file
#   3. Transmits known BLE advertising packets via Ubertooth One
#   4. Optionally captures with the Nordic nRF Sniffer for cross-validation
#   5. Waits for the test duration, then stops all processes
#   6. Analyzes the ESP32 log for received packets and signal data
#   7. Prints a summary report with next-step suggestions
#
# Usage:
#   ./tools/ble_diag_test.sh -m CA:FE:BA:BE:00:01 -d 0201060B0954455354 -t 30
#   ./tools/ble_diag_test.sh --nrf --nrf-port /dev/ttyACM0 -v
#   ./tools/ble_diag_test.sh --no-flash -t 60
#
# See: ./tools/ble_diag_test.sh -h for full option list
#

set -euo pipefail

# ── Colour helpers ──────────────────────────────────────────────────────────
if [[ -t 1 ]]; then
    RED='\033[0;31m'
    GREEN='\033[0;32m'
    YELLOW='\033[1;33m'
    CYAN='\033[0;36m'
    BOLD='\033[1m'
    NC='\033[0m'
else
    RED='' GREEN='' YELLOW='' CYAN='' BOLD='' NC=''
fi

info()  { printf "${CYAN}[INFO]${NC}  %s\n" "$*"; }
ok()    { printf "${GREEN}[ OK ]${NC}  %s\n" "$*"; }
warn()  { printf "${YELLOW}[WARN]${NC}  %s\n" "$*"; }
err()   { printf "${RED}[ERR ]${NC}  %s\n" "$*" >&2; }
die()   { err "$@"; exit 1; }

# ── Defaults ────────────────────────────────────────────────────────────────
MAC="${MAC:-CA:FE:BA:BE:00:01}"
ADV_DATA="${ADV_DATA:-0201060B0954455354}"     # Flags + Complete Local Name "TEST"
CHANNELS="${CHANNELS:-37,38,39}"
TIME="${TIME:-30}"
ESP_PORT="${ESP_PORT:-/dev/ttyUSB0}"
NRF_PORT="${NRF_PORT:-/dev/ttyACM0}"
USE_NRF=0
NO_FLASH=0
VERBOSE=0

# ── Parse arguments ──────────────────────────────────────────────────────────
usage() {
    cat <<EOF
Usage: $0 [OPTIONS]

BLE diagnostic test for the ESP32 nRF24L01+ sniffer + Ubertooth One TX.

Options:
  -m MAC          MAC address for Ubertooth TX (default: CA:FE:BA:BE:00:01)
  -d DATA         Advertising data hex (default: 0201060B0954455354)
  -c CHANNEL      Single channel or comma-separated (default: 37,38,39)
  -t TIME         Duration in seconds (default: 30)
  -p PORT         ESP32 serial port (default: /dev/ttyUSB0)
  --nrf           Also capture with Nordic nRF Sniffer
  --nrf-port PORT nRF Sniffer serial port (default: /dev/ttyACM0)
  --no-flash      Skip ESP32 firmware flash step
  -v              Verbose output
  -h              Show this help message

Examples:
  $0 -m CA:FE:BA:BE:00:01 -t 30
  $0 --nrf --nrf-port /dev/ttyACM0 -v
  $0 --no-flash -c 37 -t 60
EOF
    exit 0
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        -m)  MAC="$2";       shift 2 ;;
        -d)  ADV_DATA="$2";  shift 2 ;;
        -c)  CHANNELS="$2";  shift 2 ;;
        -t)  TIME="$2";      shift 2 ;;
        -p)  ESP_PORT="$2";  shift 2 ;;
        --nrf)      USE_NRF=1;  shift ;;
        --nrf-port) NRF_PORT="$2"; shift 2 ;;
        --no-flash) NO_FLASH=1;   shift ;;
        -v)  VERBOSE=1;      shift ;;
        -h)  usage ;;
        *)   die "Unknown option: $1. Use -h for help." ;;
    esac
done

# ── Validate numeric arguments ──────────────────────────────────────────────
[[ "$TIME" =~ ^[0-9]+$ ]] || die "Duration (-t) must be a positive integer, got: $TIME"
[[ "$TIME" -gt 0 ]]       || die "Duration (-t) must be > 0, got: $TIME"

# ── Paths ───────────────────────────────────────────────────────────────────
PROJECT_DIR="/home/huyang/projects/esp32"
CAPTURE_DIR="${PROJECT_DIR}/captures"
TOOLS_DIR="${PROJECT_DIR}/tools"
UBERTOOTH_TX="${TOOLS_DIR}/ubertooth_btle_tx.py"
NRF_SNIFFER="/home/huyang/.nrfutil/bin/nrfutil-ble-sniffer"
IDF_ACTIVATE="$HOME/.espressif/tools/activate_idf_v6.0.1.sh"

# ── Timestamp for file names ────────────────────────────────────────────────
TIMESTAMP="$(date +%Y%m%d_%H%M%S)"
ESP_LOG="${CAPTURE_DIR}/diag_${TIMESTAMP}_esp32.log"
NRF_PCAP="${CAPTURE_DIR}/diag_${TIMESTAMP}_nrf.pcap"
SUMMARY_FILE="${CAPTURE_DIR}/diag_${TIMESTAMP}_summary.txt"

# ── PIDs for cleanup ─────────────────────────────────────────────────────────
ESP32_CAPTURE_PID=""
UBERTOOTH_PID=""
NRF_PID=""

# ── Cleanup function ─────────────────────────────────────────────────────────
cleanup() {
    local sig="${1:-}"
    if [[ -n "$sig" ]]; then
        echo ""
        warn "Caught signal — cleaning up…"
    fi

    # Stop ESP32 serial capture
    if [[ -n "$ESP32_CAPTURE_PID" ]] && kill -0 "$ESP32_CAPTURE_PID" 2>/dev/null; then
        info "Stopping ESP32 serial capture (PID $ESP32_CAPTURE_PID)…"
        kill "$ESP32_CAPTURE_PID" 2>/dev/null || true
        wait "$ESP32_CAPTURE_PID" 2>/dev/null || true
    fi

    # Stop Ubertooth TX
    if [[ -n "$UBERTOOTH_PID" ]] && kill -0 "$UBERTOOTH_PID" 2>/dev/null; then
        info "Stopping Ubertooth TX (PID $UBERTOOTH_PID)…"
        kill "$UBERTOOTH_PID" 2>/dev/null || true
        wait "$UBERTOOTH_PID" 2>/dev/null || true
    fi

    # Stop nRF Sniffer
    if [[ -n "$NRF_PID" ]] && kill -0 "$NRF_PID" 2>/dev/null; then
        info "Stopping nRF Sniffer (PID $NRF_PID)…"
        kill "$NRF_PID" 2>/dev/null || true
        wait "$NRF_PID" 2>/dev/null || true
    fi

    # Restore serial port settings (in case cat left it in raw mode)
    if [[ -c "$ESP_PORT" ]]; then
        stty -F "$ESP_PORT" 115200 raw -echo 2>/dev/null || true
    fi

    ok "All processes stopped."
}

# Trap SIGINT and SIGTERM for graceful shutdown
trap 'cleanup SIGINT' INT
trap 'cleanup SIGTERM' TERM

# ── Pre-flight checks ───────────────────────────────────────────────────────
info "BLE Diagnostic Test — ${TIMESTAMP}"
info "────────────────────────────────────────────────────────────────"

if [[ "$VERBOSE" -eq 1 ]]; then
    info "Configuration:"
    info "  MAC:           $MAC"
    info "  Adv Data:      $ADV_DATA"
    info "  Channels:      $CHANNELS"
    info "  Duration:      ${TIME}s"
    info "  ESP32 Port:    $ESP_PORT"
    info "  nRF Sniffer:   $( (( USE_NRF )) && echo "YES ($NRF_PORT)" || echo "NO" )"
    info "  Flash ESP32:   $( (( NO_FLASH )) && echo "NO (--no-flash)" || echo "YES" )"
fi

# Check capture directory
if [[ ! -d "$CAPTURE_DIR" ]]; then
    info "Creating capture directory: $CAPTURE_DIR"
    mkdir -p "$CAPTURE_DIR"
fi

# Check ESP32 serial port
if [[ ! -c "$ESP_PORT" ]]; then
    die "ESP32 serial port not found: $ESP_PORT
  - Check device is connected: ls ${ESP_PORT%/*}/ttyUSB* ${ESP_PORT%/*}/ttyACM*
  - Fix permissions: sudo chmod 666 $ESP_PORT
  - Or specify a different port: -p /dev/ttyUSB1"
fi
ok "ESP32 serial port found: $ESP_PORT"

# Check Ubertooth TX script
if [[ ! -x "$UBERTOOTH_TX" ]]; then
    die "Ubertooth TX script not found or not executable: $UBERTOOTH_TX"
fi
ok "Ubertooth TX script found: $UBERTOOTH_TX"

# Check Ubertooth device (USB VID:PID 1d50:6002)
if ! lsusb 2>/dev/null | grep -q "1d50:6002"; then
    die "Ubertooth One not found on USB (VID:PID 1d50:6002)
  - Check device is plugged in: lsusb | grep 1d50
  - Check udev rules for permissions: /etc/udev/rules.d/"
fi
ok "Ubertooth One found on USB"

# Check nRF Sniffer if requested
if [[ "$USE_NRF" -eq 1 ]]; then
    if [[ ! -x "$NRF_SNIFFER" ]]; then
        die "nRF Sniffer tool not found or not executable: $NRF_SNIFFER"
    fi
    if [[ ! -c "$NRF_PORT" ]]; then
        die "nRF Sniffer serial port not found: $NRF_PORT
  - Check device is connected: ls ${NRF_PORT%/*}/ttyACM*
  - Or specify a different port: --nrf-port /dev/ttyACM1"
    fi
    ok "nRF Sniffer port found: $NRF_PORT"
fi

# Check ESP-IDF activation script
if [[ "$NO_FLASH" -eq 0 ]]; then
    if [[ ! -f "$IDF_ACTIVATE" ]]; then
        die "ESP-IDF activation script not found: $IDF_ACTIVATE"
    fi
    ok "ESP-IDF activation script found"
fi

info "────────────────────────────────────────────────────────────────"
info "Pre-flight checks passed."

# ── Step 1: Flash ESP32 ─────────────────────────────────────────────────────
if [[ "$NO_FLASH" -eq 0 ]]; then
    info "Step 1: Flashing ESP32 firmware…"
    # Activate ESP-IDF environment and flash
    # shellcheck disable=SC1090
    if ! ( source "$IDF_ACTIVATE" && idf.py -p "$ESP_PORT" flash ); then
        die "Failed to flash ESP32. Check serial port, firmware, and ESP-IDF setup."
    fi
    ok "ESP32 flashed successfully."
else
    info "Step 1: Skipping flash (--no-flash)."
fi

# ── Step 2: Start ESP32 serial capture ──────────────────────────────────────
info "Step 2: Starting ESP32 serial capture → $ESP_LOG"

# Configure serial port for raw read at 115200 baud
stty -F "$ESP_PORT" 115200 raw -echo 2>/dev/null || true

# Read from serial port in background, appending to log file.
# Using cat instead of idf.py monitor because the latter requires an interactive TTY.
cat "$ESP_PORT" > "$ESP_LOG" 2>/dev/null &
ESP32_CAPTURE_PID=$!

# Small delay to ensure cat has opened the port
sleep 0.5

if [[ -n "$ESP32_CAPTURE_PID" ]] && kill -0 "$ESP32_CAPTURE_PID" 2>/dev/null; then
    ok "ESP32 serial capture started (PID $ESP32_CAPTURE_PID)."
else
    die "Failed to start ESP32 serial capture. Check port: $ESP_PORT"
fi

# ── Step 3: Wait for ESP32 to boot ──────────────────────────────────────────
info "Step 3: Waiting 3 seconds for ESP32 to boot and start scanning…"
sleep 3
ok "ESP32 should be ready and scanning."

# ── Step 4: Start Ubertooth TX ──────────────────────────────────────────────
info "Step 4: Starting Ubertooth BLE TX…"
info "  MAC=$MAC  DATA=$ADV_DATA  CH=$CHANNELS  TIME=${TIME}s"

UBERTOOTH_VERBOSE=""
if [[ "$VERBOSE" -eq 1 ]]; then
    UBERTOOTH_VERBOSE="-v"
fi

python3 "$UBERTOOTH_TX" \
    -m "$MAC" \
    -d "$ADV_DATA" \
    -C "$CHANNELS" \
    -i 3 \
    -t "$TIME" \
    ${UBERTOOTH_VERBOSE:+$UBERTOOTH_VERBOSE} \
    &
UBERTOOTH_PID=$!

sleep 0.5
if [[ -n "$UBERTOOTH_PID" ]] && kill -0 "$UBERTOOTH_PID" 2>/dev/null; then
    ok "Ubertooth TX started (PID $UBERTOOTH_PID)."
else
    warn "Ubertooth TX process may have exited early. Continuing…"
fi

# ── Step 5: Start nRF Sniffer (if requested) ─────────────────────────────────
if [[ "$USE_NRF" -eq 1 ]]; then
    info "Step 5: Starting nRF Sniffer capture → $NRF_PCAP"

    # nrfutil-ble-sniffer expects timeout in milliseconds
    NRF_TIMEOUT_MS=$(( TIME * 1000 ))

    "$NRF_SNIFFER" sniff \
        --port "$NRF_PORT" \
        --timeout "$NRF_TIMEOUT_MS" \
        --output-pcap-file "$NRF_PCAP" \
        &
    NRF_PID=$!

    sleep 0.5
    if [[ -n "$NRF_PID" ]] && kill -0 "$NRF_PID" 2>/dev/null; then
        ok "nRF Sniffer started (PID $NRF_PID)."
    else
        warn "nRF Sniffer process may have exited early. Continuing…"
    fi
else
    info "Step 5: nRF Sniffer not requested (use --nrf to enable)."
fi

# ── Step 6: Wait for test duration ──────────────────────────────────────────
info "Step 6: Running test for ${TIME} seconds…"
info "  (Press Ctrl+C to stop early)"

# Progress counter
ELAPSED=0
while [[ "$ELAPSED" -lt "$TIME" ]]; do
    REMAINING=$(( TIME - ELAPSED ))
    if [[ "$VERBOSE" -eq 1 ]] && (( ELAPSED > 0 && ELAPSED % 5 == 0 )); then
        info "  ${ELAPSED}s / ${TIME}s elapsed (${REMAINING}s remaining)"
    fi
    sleep 1
    ELAPSED=$(( ELAPSED + 1 ))
done
ok "Test duration reached."

# ── Step 7: Stop all processes ──────────────────────────────────────────────
info "Step 7: Stopping all processes…"
cleanup ""

# ── Step 8: Post-test analysis ──────────────────────────────────────────────
info "Step 8: Analyzing captured data…"

# The ESP32 firmware prints the advertiser MAC from the received BLE PDU in
# reversed byte order (BLE transmits LSB first).  The Ubertooth TX script also
# reverses the -m argument for on-air transmission, so a double reversal means
# the ESP32 prints the MAC in the SAME order as the -m argument.
#
# Example: -m CA:FE:BA:BE:00:01
#   Ubertooth TX sends on-air: 01:00:BE:BA:FE:CA  (reversed per BLE spec)
#   ESP32 receives and prints: buf[7]:buf[6]:buf[5]:buf[4]:buf[3]:buf[2]
#     = 01:00:BE:BA:FE:CA
#
# Wait — the firmware reads 6 bytes starting at buf[2] and prints them
# reversed (buf[7]:buf[6]:...buf[2]), which is the standard "presentation"
# order of the on-air AdvA.  The on-air AdvA is the -m argument in reversed
# byte order.  So the ESP32 prints the reversed version of -m.
#
# Example: -m CA:FE:BA:BE:00:01
#   ESP32 prints: 01:00:BE:BA:FE:CA

# Build the MAC pattern the ESP32 will print (reversed byte order from -m)
IFS=':' read -ra MAC_BYTES <<< "$MAC"
MAC_REVERSED=""
for (( i=5; i>=0; i-- )); do
    if [[ -n "$MAC_REVERSED" ]]; then
        MAC_REVERSED="${MAC_REVERSED}:"
    fi
    MAC_REVERSED="${MAC_REVERSED}${MAC_BYTES[$i]}"
done

info "  Searching for MAC pattern in ESP32 log: $MAC_REVERSED"
info "  (On-air byte order reverses MAC from TX argument)"

# Count metrics from ESP32 log
LINES_TOTAL=0
MAC_HITS=0
RPD_SIGNAL_HIGH=0
RPD_SIGNAL_LOW=0
RAW_LINES=0
ADV_IND_COUNT=0
ADV_NONCONN_IND_COUNT=0
ADV_DIRECT_IND_COUNT=0
ADV_SCAN_IND_COUNT=0
SCAN_RSP_COUNT=0
CONNECT_IND_COUNT=0
UNKNOWN_PDU_COUNT=0

if [[ -f "$ESP_LOG" ]]; then
    LINES_TOTAL=$(wc -l < "$ESP_LOG" | tr -d ' ')

    # Search for MAC (case-insensitive hex matching)
    MAC_HITS=$(grep -ci "${MAC_REVERSED}" "$ESP_LOG" 2>/dev/null || true)

    # RPD signal counts — match both "[diag] RPD:" and "[diag] RPD before switch:"
    RPD_SIGNAL_HIGH=$(grep -c '\[diag\] RPD.*signal=1' "$ESP_LOG" 2>/dev/null || true)
    RPD_SIGNAL_LOW=$(grep -c '\[diag\] RPD.*signal=0' "$ESP_LOG" 2>/dev/null || true)

    # Raw packet lines — "[diag] raw: XX XX XX …"
    RAW_LINES=$(grep -c '\[diag\] raw:' "$ESP_LOG" 2>/dev/null || true)

    # PDU type counts — match the %-17s formatted output from the firmware.
    # These patterns are anchored with space-after to avoid substring false
    # positives (e.g. "ADV_IND " won't match "ADV_NONCONN_IND ").
    ADV_IND_COUNT=$(grep -c 'ADV_IND ' "$ESP_LOG" 2>/dev/null || true)
    ADV_NONCONN_IND_COUNT=$(grep -c 'ADV_NONCONN_IND ' "$ESP_LOG" 2>/dev/null || true)
    ADV_DIRECT_IND_COUNT=$(grep -c 'ADV_DIRECT_IND ' "$ESP_LOG" 2>/dev/null || true)
    ADV_SCAN_IND_COUNT=$(grep -c 'ADV_SCAN_IND ' "$ESP_LOG" 2>/dev/null || true)
    SCAN_RSP_COUNT=$(grep -c 'SCAN_RSP ' "$ESP_LOG" 2>/dev/null || true)
    CONNECT_IND_COUNT=$(grep -c 'CONNECT_IND ' "$ESP_LOG" 2>/dev/null || true)
    UNKNOWN_PDU_COUNT=$(grep -c 'UNKNOWN ' "$ESP_LOG" 2>/dev/null || true)
else
    warn "ESP32 log file not found: $ESP_LOG"
fi

TOTAL_PDU_DECODED=$(( ADV_IND_COUNT + ADV_NONCONN_IND_COUNT + ADV_DIRECT_IND_COUNT
                    + ADV_SCAN_IND_COUNT + SCAN_RSP_COUNT + CONNECT_IND_COUNT
                    + UNKNOWN_PDU_COUNT ))

# ── Build summary report ────────────────────────────────────────────────────
{
    echo "=================================================================="
    echo "  BLE Diagnostic Test Summary"
    echo "  Timestamp: ${TIMESTAMP}"
    echo "=================================================================="
    echo ""
    echo "-- Configuration ------------------------------------------------"
    echo "  MAC Address:     $MAC (TX)  ->  $MAC_REVERSED (as printed by ESP32)"
    echo "  Adv Data:        $ADV_DATA"
    echo "  Channels:        $CHANNELS"
    echo "  Duration:        ${TIME}s"
    echo "  ESP32 Port:      $ESP_PORT"
    if [[ "$USE_NRF" -eq 1 ]]; then
        echo "  nRF Sniffer:     YES ($NRF_PORT)"
    else
        echo "  nRF Sniffer:     NO"
    fi
    echo ""
    echo "-- Captured Files -----------------------------------------------"
    echo "  ESP32 Log:       $ESP_LOG"
    if [[ "$USE_NRF" -eq 1 ]]; then
        echo "  nRF PCAP:        $NRF_PCAP"
    fi
    echo "  Summary:         $SUMMARY_FILE"
    echo ""
    echo "-- ESP32 Log Analysis -------------------------------------------"
    echo "  Total log lines:       ${LINES_TOTAL}"
    echo "  MAC matches:           ${MAC_HITS}  (pattern: $MAC_REVERSED)"
    echo "  Raw packet lines:      ${RAW_LINES}"
    echo "  PDU types decoded:     ${TOTAL_PDU_DECODED}"
    echo "    ADV_IND:             ${ADV_IND_COUNT}"
    echo "    ADV_NONCONN_IND:     ${ADV_NONCONN_IND_COUNT}"
    echo "    ADV_DIRECT_IND:      ${ADV_DIRECT_IND_COUNT}"
    echo "    ADV_SCAN_IND:        ${ADV_SCAN_IND_COUNT}"
    echo "    SCAN_RSP:            ${SCAN_RSP_COUNT}"
    echo "    CONNECT_IND:         ${CONNECT_IND_COUNT}"
    echo "    UNKNOWN:             ${UNKNOWN_PDU_COUNT}"
    echo ""
    echo "-- RPD Signal Detection ----------------------------------------"
    echo "  signal=1 (> -64 dBm): ${RPD_SIGNAL_HIGH}"
    echo "  signal=0 (< -64 dBm): ${RPD_SIGNAL_LOW}"
    echo ""
    echo "-- Result -------------------------------------------------------"
    if [[ "$MAC_HITS" -gt 0 ]]; then
        echo "  [PASS] MAC FOUND in ESP32 log -- Ubertooth packets were received!"
    else
        echo "  [FAIL] MAC NOT FOUND -- no Ubertooth packets detected by ESP32."
        echo "    Possible causes:"
        echo "      - nRF24L01+ not connected or misconfigured"
        echo "      - Ubertooth TX on wrong channel or too far away"
        echo "      - ESP32 firmware not running or serial not connected"
    fi
    echo ""
    if [[ "$RAW_LINES" -gt 0 ]] && [[ "$MAC_HITS" -eq 0 ]]; then
        echo "  Note: Raw packets were received but no MAC match."
        echo "        Check dewhitening or channel alignment."
    fi
    echo ""
    echo "-- Next Steps ---------------------------------------------------"
    echo "  1. Review full ESP32 log:  less $ESP_LOG"
    if [[ "$USE_NRF" -eq 1 ]] && [[ -f "$NRF_PCAP" ]]; then
        echo "  2. Analyze nRF PCAP with tshark:"
        echo "       tshark -r $NRF_PCAP -Y btle -T fields -e btle.advertising_address"
        echo "       tshark -r $NRF_PCAP -Y 'btle.advertising_address == ${MAC_REVERSED//:}' -c 20"
    else
        echo "  2. Re-run with --nrf for cross-validation with Nordic sniffer"
    fi
    echo "  3. Check RPD signal levels vs. raw packet correlation"
    echo "  4. Verify dewhitening: compare [diag] raw: vs. [chN] decoded output"
    echo "=================================================================="
} | tee "$SUMMARY_FILE"

echo ""
ok "Summary saved to: $SUMMARY_FILE"
ok "Diagnostic test complete."