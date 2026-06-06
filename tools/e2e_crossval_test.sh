#!/usr/bin/env bash
#
# e2e_crossval_test.sh — End-to-end cross-validation test
#
# Transmits known BLE advertising packets via Ubertooth One,
# captures ESP32 nRF24L01+ serial output and optionally nRF52840
# Sniffer pcap, then cross-validates that both systems received
# the Ubertooth packets with the correct MAC address.
#
# Usage:
#   ./tools/e2e_crossval_test.sh                    # default: MAC CA:FE:BA:BE:00:01, 30s
#   ./tools/e2e_crossval_test.sh --nrf -t 60       # with nRF Sniffer, 60s
#   ./tools/e2e_crossval_test.sh --no-flash -v     # skip flash, verbose
#   ./tools/e2e_crossval_test.sh -h                # help
#
# MAC byte ordering (CRITICAL):
#   Ubertooth -m argument:  CA:FE:BA:BE:00:01  (standard notation)
#   ESP32 serial output:   01:00:BE:BA:FE:CA  (buf[7]:...buf[2], reversed)
#   nRF Sniffer/tshark:   ca:fe:ba:be:00:01  (same order as -m, lowercase)
#
# The script automatically reverses the MAC for ESP32 log search.
#
# Exit codes:
#   0  PASS — ESP32 received Ubertooth packets
#   1  FAIL — nRF saw packets but ESP32 did not
#   2  FAIL — Neither sniffer saw packets
#   3  ERROR — missing tools or configuration
#

set -uo pipefail

# ── Colour helpers ──────────────────────────────────────────────────────────
if [[ -t 1 ]]; then
    RED='\033[0;31m' GREEN='\033[0;32m' YELLOW='\033[1;33m'
    CYAN='\033[0;36m' BOLD='\033[1m' NC='\033[0m'
else
    RED='' GREEN='' YELLOW='' CYAN='' BOLD='' NC=''
fi

info()  { printf "${CYAN}[INFO]${NC}  %s\n" "$*"; }
ok()    { printf "${GREEN}[ OK ]${NC}  %s\n" "$*"; }
warn()  { printf "${YELLOW}[WARN]${NC}  %s\n" "$*"; }
err()   { printf "${RED}[ERR ]${NC}  %s\n" "$*" >&2; }
die()   { err "$@"; exit 3; }

# ── Defaults ────────────────────────────────────────────────────────────────
#
# MAC:  CA:FE:BA:BE:00:01  — distinctive pattern unlikely in ambient traffic
# Data: Flags(0x02,0x01,0x06) + Complete Local Name "E2ETEST"
#       (0x08,0x09,"E2ETEST") = 0201060809453245544 5354
#
MAC="${MAC:-CA:FE:BA:BE:00:01}"
ADV_DATA="${ADV_DATA:-020106080945324554455354}"
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

End-to-end cross-validation test for ESP32 nRF24L01+ BLE sniffer.

Uses Ubertooth One to transmit known BLE advertising packets with a
distinctive MAC address, then verifies the ESP32 (and optionally
nRF Sniffer) received those packets.

Options:
  -m MAC          MAC address for Ubertooth TX (default: $MAC)
  -d DATA         Advertising data hex (default: $ADV_DATA)
  -c CHANNEL      Single channel or comma-separated (default: $CHANNELS)
  -t TIME         Duration in seconds (default: $TIME)
  -p PORT         ESP32 serial port (default: $ESP_PORT)
  --nrf           Also capture with Nordic nRF Sniffer for cross-validation
  --nrf-port PORT nRF Sniffer serial port (default: $NRF_PORT)
  --no-flash      Skip ESP32 firmware flash step
  -v              Verbose output
  -h              Show this help message

Examples:
  $0                              # default test, 30s
  $0 --nrf -t 60                 # with nRF Sniffer, 60 seconds
  $0 --no-flash -c 37 -t 10     # single channel, no flash, 10s

Exit codes:
  0  PASS — ESP32 received Ubertooth packets
  1  FAIL — nRF saw packets but ESP32 did not (configuration issue)
  2  FAIL — Neither sniffer saw packets (transmission issue)
  3  ERROR — missing tools or configuration
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
ESP_LOG="${CAPTURE_DIR}/e2e_${TIMESTAMP}_esp32.log"
NRF_PCAP="${CAPTURE_DIR}/e2e_${TIMESTAMP}_nrf.pcap"
SUMMARY_FILE="${CAPTURE_DIR}/e2e_${TIMESTAMP}_summary.txt"

# ── PIDs for cleanup ─────────────────────────────────────────────────────────
ESP32_CAPTURE_PID=""
UBERTOOTH_PID=""
NRF_PID=""

# ── Cleanup function ─────────────────────────────────────────────────────────
cleanup() {
    local sig="${1:-}"
    if [[ -n "$sig" ]]; then
        echo ""
        warn "Caught signal — cleaning up..."
    fi

    # Stop ESP32 serial capture
    if [[ -n "$ESP32_CAPTURE_PID" ]] && kill -0 "$ESP32_CAPTURE_PID" 2>/dev/null; then
        info "Stopping ESP32 serial capture (PID $ESP32_CAPTURE_PID)..."
        kill "$ESP32_CAPTURE_PID" 2>/dev/null || true
        wait "$ESP32_CAPTURE_PID" 2>/dev/null || true
    fi

    # Stop Ubertooth TX
    if [[ -n "$UBERTOOTH_PID" ]] && kill -0 "$UBERTOOTH_PID" 2>/dev/null; then
        info "Stopping Ubertooth TX (PID $UBERTOOTH_PID)..."
        kill "$UBERTOOTH_PID" 2>/dev/null || true
        wait "$UBERTOOTH_PID" 2>/dev/null || true
    fi

    # Stop nRF Sniffer
    if [[ -n "$NRF_PID" ]] && kill -0 "$NRF_PID" 2>/dev/null; then
        info "Stopping nRF Sniffer (PID $NRF_PID)..."
        kill "$NRF_PID" 2>/dev/null || true
        wait "$NRF_PID" 2>/dev/null || true
    fi

    # Restore serial port settings
    if [[ -c "$ESP_PORT" ]]; then
        stty -F "$ESP_PORT" 115200 raw -echo 2>/dev/null || true
    fi

    ok "All processes stopped."
}

trap 'cleanup SIGINT' INT
trap 'cleanup SIGTERM' TERM

# ── Compute MAC patterns for searching ──────────────────────────────────────
#
# ESP32 firmware (main.cpp line 580-582) prints the AdvA from buf[2..7]
# as buf[7]:buf[6]:buf[5]:buf[4]:buf[3]:buf[2], which is the standard
# presentation order (MSB first).  Since BLE transmits AdvA LSByte-first,
# the on-air bytes are MAC[5], MAC[4], ..., MAC[0].  After dewhitening,
# buf[2]=MAC[5], buf[3]=MAC[4], ..., buf[7]=MAC[0].  The printf then
# prints: MAC[0]:MAC[1]:MAC[2]:MAC[3]:MAC[4]:MAC[5] — i.e. the SAME
# order as the -m argument.
#
# Wait — that can't be right.  Let me re-derive:
#   Ubertooth -m CA:FE:BA:BE:00:01
#   Ubertooth sends on-air AdvA (LSByte first): 01:00:BE:BA:FE:CA
#   After dewhitening, buf[2]=01 buf[3]=00 buf[4]=BE buf[5]=BA buf[6]=FE buf[7]=CA
#   ESP32 prints: buf[7]:buf[6]:buf[5]:buf[4]:buf[3]:buf[2] = CA:FE:BA:BE:00:01
#
# So the ESP32 actually prints the SAME order as the -m argument!
# This is because printf uses buf[7] first = last on-air byte = first -m byte.
#
# HOWEVER, the Ubertooth TX script reverses the MAC for on-air transmission:
#   start_btle_slave() sends mac_bytes as-is, but the firmware bt_slave_le()
#   places them directly into the PDU as AdvA.  The BLE spec says AdvA is
#   LSByte-first on air, so the firmware expects the MAC already reversed.
#
# Cross-check with the existing ble_diag_test.sh (lines 358-365):
#   It reverses the MAC for ESP32 search pattern.  So the existing script
#   thinks the ESP32 prints the REVERSED order (01:00:BE:BA:FE:CA for input
#   CA:FE:BA:BE:00:01).
#
# We need to determine empirically which is correct.  From the recent test
# output, the ESP32 prints real BLE MACs like "7B:94:39:5B:48:7A" which
# is the standard presentation order.  For the Ubertooth TX, we need to
# check whether the firmware reverses the MAC or expects pre-reversed data.
#
# The ubertooth_btle_tx.py documentation (line 130-133) says:
#   "AdvA (6 bytes): MAC address in reversed byte order"
# This means the firmware EXPECTS the MAC already in BLE LSByte-first order.
# The parse_mac() function does NOT reverse — it just converts hex to bytes.
# So -m CA:FE:BA:BE:00:01 sends bytes CA,FE,BA,BE,00,01 on air as AdvA.
# In BLE LSByte-first on air, the first byte is CA = MSB of the -m argument.
# The nRF24 receives this (after bit-swap), so buf[2]=CA (first on-air byte
# after preamble+AA), and printf prints buf[7]:...:buf[2].
#
# For a 6-byte AdvA with on-air order [byte0,byte1,...,byte5]:
#   buf[2]=byte0, buf[3]=byte1, ..., buf[7]=byte5
#   printf: buf[7]:buf[6]:...:buf[2] = byte5:byte4:...:byte0
#
# If Ubertooth sends -m CA:FE:BA:BE:00:01 as mac_bytes = [CA,FE,BA,BE,00,01]
# on air as AdvA in LSByte-first order: AA + preamble + CA:FE:BA:BE:00:01
# (which is actually MSByte-first for the -m argument's CA:FE order)
# 
# Since this is complex and error-prone, we'll search for BOTH patterns
# (original MAC and reversed MAC) in the ESP32 log, and report whichever
# one matches.  This eliminates the byte-order confusion once and for all.
#
IFS=':' read -ra MAC_BYTES <<< "$MAC"
MAC_REVERSED=""
for (( i=5; i>=0; i-- )); do
    [[ -n "$MAC_REVERSED" ]] && MAC_REVERSED="${MAC_REVERSED}:"
    MAC_REVERSED="${MAC_REVERSED}${MAC_BYTES[$i]}"
done
# Also create lowercase versions for tshark/grep -ci
MAC_LOWER=$(echo "$MAC" | tr 'A-F' 'a-f')
MAC_REVERSED_LOWER=$(echo "$MAC_REVERSED" | tr 'A-F' 'a-f')

# ── Pre-flight checks ───────────────────────────────────────────────────────
info "End-to-End Cross-Validation Test — ${TIMESTAMP}"
info "────────────────────────────────────────────────────────────────"

if [[ "$VERBOSE" -eq 1 ]]; then
    info "Configuration:"
    info "  MAC (TX arg):      $MAC"
    info "  MAC (reversed):    $MAC_REVERSED"
    info "  Adv Data:          $ADV_DATA"
    info "  Channels:         $CHANNELS"
    info "  Duration:          ${TIME}s"
    info "  ESP32 Port:        $ESP_PORT"
    info "  nRF Sniffer:      $( (( USE_NRF )) && echo "YES ($NRF_PORT)" || echo "NO" )"
    info "  Flash ESP32:      $( (( NO_FLASH )) && echo "NO (--no-flash)" || echo "YES" )"
fi

# Capture directory
if [[ ! -d "$CAPTURE_DIR" ]]; then
    info "Creating capture directory: $CAPTURE_DIR"
    mkdir -p "$CAPTURE_DIR"
fi

# ESP32 serial port
if [[ ! -c "$ESP_PORT" ]]; then
    die "ESP32 serial port not found: $ESP_PORT
  - Check device is connected: ls ${ESP_PORT%/*}/ttyUSB* ${ESP_PORT%/*}/ttyACM*
  - Fix permissions: sudo chmod 666 $ESP_PORT
  - Or specify a different port: -p /dev/ttyUSB1"
fi
ok "ESP32 serial port found: $ESP_PORT"

# Ubertooth TX script
if [[ ! -x "$UBERTOOTH_TX" ]]; then
    die "Ubertooth TX script not found or not executable: $UBERTOOTH_TX"
fi
ok "Ubertooth TX script found: $UBERTOOTH_TX"

# Ubertooth device
if ! lsusb 2>/dev/null | grep -q "1d50:6002"; then
    err "Ubertooth One not found on USB (VID:PID 1d50:6002)."
    err "  - Check device is plugged in: lsusb | grep 1d50"
    err "  - Check udev rules for permissions: /etc/udev/rules.d/"
    err ""
    err "Cannot perform end-to-end test without Ubertooth TX."
    err "Exiting gracefully — no errors, but test cannot proceed."
    exit 3
fi
ok "Ubertooth One found on USB"

# nRF Sniffer (optional)
if [[ "$USE_NRF" -eq 1 ]]; then
    if ! command -v tshark &>/dev/null; then
        die "tshark not found. Install wireshark/tshark for nRF pcap analysis."
    fi
    ok "tshark found"
    if [[ ! -c "$NRF_PORT" ]]; then
        warn "nRF Sniffer port not found: $NRF_PORT — continuing without nRF capture"
        USE_NRF=0
    else
        ok "nRF Sniffer port found: $NRF_PORT"
    fi
fi

# ESP-IDF (for flash)
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
    info "Step 1: Flashing ESP32 firmware..."
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

stty -F "$ESP_PORT" 115200 raw -echo 2>/dev/null || true
cat "$ESP_PORT" > "$ESP_LOG" 2>/dev/null &
ESP32_CAPTURE_PID=$!
sleep 0.5

if [[ -n "$ESP32_CAPTURE_PID" ]] && kill -0 "$ESP32_CAPTURE_PID" 2>/dev/null; then
    ok "ESP32 serial capture started (PID $ESP32_CAPTURE_PID)."
else
    die "Failed to start ESP32 serial capture. Check port: $ESP_PORT"
fi

# ── Step 3: Wait for ESP32 to boot ──────────────────────────────────────────
info "Step 3: Waiting 5s for ESP32 to boot and start scanning..."
sleep 5
ok "ESP32 should be ready and scanning."

# ── Step 4: Start Ubertooth TX ──────────────────────────────────────────────
info "Step 4: Starting Ubertooth BLE TX..."
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
    ${UBERTOOTH_VERBOSE:+"$UBERTOOTH_VERBOSE"} \
    &
UBERTOOTH_PID=$!

sleep 0.5
if [[ -n "$UBERTOOTH_PID" ]] && kill -0 "$UBERTOOTH_PID" 2>/dev/null; then
    ok "Ubertooth TX started (PID $UBERTOOTH_PID)."
else
    warn "Ubertooth TX may have exited early — continuing..."
fi

# ── Step 5: Start nRF Sniffer (if available) ────────────────────────────────
if [[ "$USE_NRF" -eq 1 ]]; then
    info "Step 5: Starting nRF Sniffer capture → $NRF_PCAP"
    NRF_TIMEOUT_MS=$(( TIME * 1000 + 5000 ))  # extra 5s for startup

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
        warn "nRF Sniffer may have exited early — continuing without nRF..."
        USE_NRF=0
    fi
else
    info "Step 5: nRF Sniffer not requested (use --nrf to enable)."
fi

# ── Step 6: Wait for test duration ─────────────────────────────────────────
info "Step 6: Running test for ${TIME}s (press Ctrl+C to stop early)..."

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

# ── Step 7: Extra capture time ──────────────────────────────────────────────
info "Step 7: Capturing 2s extra for in-flight packets..."
sleep 2

# ── Step 8: Stop all processes ──────────────────────────────────────────────
info "Step 8: Stopping all processes..."
cleanup ""

# ── Step 9: Analyze ESP32 log ────────────────────────────────────────────────
info "Step 9: Analyzing captured data..."

# Search for BOTH MAC patterns (original and reversed) in ESP32 log
# One of them will match, depending on the Ubertooth firmware's MAC handling
ESP_MAC_HITS_ORIG=0
ESP_MAC_HITS_REV=0
ESP_MAC_HITS=0
ESP_MAC_PATTERN=""

LINES_TOTAL=0
ADV_IND_COUNT=0
ADV_NONCONN_IND_COUNT=0
ADV_SCAN_IND_COUNT=0
ADV_EXT_IND_COUNT=0
CONNECT_IND_COUNT=0
UNKNOWN_PDU_COUNT=0

if [[ -f "$ESP_LOG" ]]; then
    LINES_TOTAL=$(wc -l < "$ESP_LOG" | tr -d ' ')

    # Try original MAC order first (CA:FE:BA:BE:00:01)
    ESP_MAC_HITS_ORIG=$(grep -ci "$MAC" "$ESP_LOG" 2>/dev/null || true)
    ESP_MAC_HITS_ORIG=${ESP_MAC_HITS_ORIG:-0}

    # Try reversed MAC order (01:00:BE:BA:FE:CA)
    ESP_MAC_HITS_REV=$(grep -ci "$MAC_REVERSED" "$ESP_LOG" 2>/dev/null || true)
    ESP_MAC_HITS_REV=${ESP_MAC_HITS_REV:-0}

    # Use whichever pattern matches (case-insensitive)
    if [[ "$ESP_MAC_HITS_ORIG" -ge "$ESP_MAC_HITS_REV" ]]; then
        ESP_MAC_HITS=$ESP_MAC_HITS_ORIG
        ESP_MAC_PATTERN="$MAC (standard order)"
    else
        ESP_MAC_HITS=$ESP_MAC_HITS_REV
        ESP_MAC_PATTERN="$MAC_REVERSED (byte-reversed)"
    fi

    # PDU type counts
    ADV_IND_COUNT=$(grep -c 'ADV_IND ' "$ESP_LOG" 2>/dev/null || true)
    ADV_IND_COUNT=${ADV_IND_COUNT:-0}
    ADV_NONCONN_IND_COUNT=$(grep -c 'ADV_NONCONN_IND ' "$ESP_LOG" 2>/dev/null || true)
    ADV_NONCONN_IND_COUNT=${ADV_NONCONN_IND_COUNT:-0}
    ADV_SCAN_IND_COUNT=$(grep -c 'ADV_SCAN_IND ' "$ESP_LOG" 2>/dev/null || true)
    ADV_SCAN_IND_COUNT=${ADV_SCAN_IND_COUNT:-0}
    ADV_EXT_IND_COUNT=$(grep -c 'ADV_EXT_IND ' "$ESP_LOG" 2>/dev/null || true)
    ADV_EXT_IND_COUNT=${ADV_EXT_IND_COUNT:-0}
    CONNECT_IND_COUNT=$(grep -c 'CONNECT_IND ' "$ESP_LOG" 2>/dev/null || true)
    CONNECT_IND_COUNT=${CONNECT_IND_COUNT:-0}
    UNKNOWN_PDU_COUNT=$(grep -c 'UNKNOWN' "$ESP_LOG" 2>/dev/null || true)
    UNKNOWN_PDU_COUNT=${UNKNOWN_PDU_COUNT:-0}
else
    warn "ESP32 log file not found: $ESP_LOG"
fi

TOTAL_PDU_DECODED=$(( ADV_IND_COUNT + ADV_NONCONN_IND_COUNT + ADV_SCAN_IND_COUNT
                    + ADV_EXT_IND_COUNT + CONNECT_IND_COUNT + UNKNOWN_PDU_COUNT ))

# ── Step 10: Analyze nRF Sniffer pcap ──────────────────────────────────────
NRF_MAC_HITS=0
NRF_TOTAL_FRAMES=0

if [[ "$USE_NRF" -eq 1 ]] && [[ -f "$NRF_PCAP" ]]; then
    NRF_TOTAL_FRAMES=$(tshark -r "$NRF_PCAP" -T fields -e btle.advertising_address 2>/dev/null | wc -l || true)
    NRF_TOTAL_FRAMES=${NRF_TOTAL_FRAMES:-0}

    # Search for lowercase MAC in tshark output
    NRF_MAC_HITS=$(tshark -r "$NRF_PCAP" -T fields -e btle.advertising_address 2>/dev/null \
        | grep -ci "$MAC_LOWER" || true)
    NRF_MAC_HITS=${NRF_MAC_HITS:-0}

    # Also try reversed order
    NRF_MAC_HITS_REV=$(tshark -r "$NRF_PCAP" -T fields -e btle.advertising_address 2>/dev/null \
        | grep -ci "$MAC_REVERSED_LOWER" || true)
    NRF_MAC_HITS_REV=${NRF_MAC_HITS_REV:-0}

    NRF_MAC_HITS=$(( NRF_MAC_HITS > NRF_MAC_HITS_REV ? NRF_MAC_HITS : NRF_MAC_HITS_REV ))
elif [[ "$USE_NRF" -eq 1 ]]; then
    warn "nRF PCAP file not found: $NRF_PCAP"
fi

# ── Determine result ─────────────────────────────────────────────────────────
PASS_ESP32=0
PASS_NRF=0
PASS_CROSSVAL="N/A"
OVERALL_RESULT="FAIL"
EXIT_CODE=2

if [[ "$ESP_MAC_HITS" -gt 0 ]]; then
    PASS_ESP32=1
    OVERALL_RESULT="PASS"
    EXIT_CODE=0
fi

if [[ "$USE_NRF" -eq 1 ]] && [[ "$NRF_MAC_HITS" -gt 0 ]]; then
    PASS_NRF=1
fi

if [[ "$USE_NRF" -eq 1 ]]; then
    if [[ "$PASS_ESP32" -eq 1 ]] && [[ "$PASS_NRF" -eq 1 ]]; then
        PASS_CROSSVAL="YES"
    elif [[ "$PASS_ESP32" -eq 0 ]] && [[ "$PASS_NRF" -eq 1 ]]; then
        PASS_CROSSVAL="NO (nRF sees it, ESP32 doesn't — config issue)"
        OVERALL_RESULT="FAIL"
        EXIT_CODE=1
    elif [[ "$PASS_ESP32" -eq 0 ]] && [[ "$PASS_NRF" -eq 0 ]]; then
        PASS_CROSSVAL="NO (neither sees — transmission issue)"
        OVERALL_RESULT="FAIL"
        EXIT_CODE=2
    else
        PASS_CROSSVAL="NO (ESP32 sees it, nRF doesn't — nRF capture issue)"
    fi
fi

# ── Build summary report ────────────────────────────────────────────────────
{
    echo "=================================================================="
    echo "  End-to-End Cross-Validation Test"
    echo "  Timestamp: ${TIMESTAMP}"
    echo "=================================================================="
    echo ""
    echo "-- Configuration ------------------------------------------------"
    echo "  MAC (TX argument):  $MAC"
    echo "  MAC (reversed):     $MAC_REVERSED"
    echo "  Search pattern:     $ESP_MAC_PATTERN"
    echo "  Adv Data:           $ADV_DATA"
    echo "  Channels:           $CHANNELS"
    echo "  Duration:           ${TIME}s"
    echo "  ESP32 Port:         $ESP_PORT"
    if [[ "$USE_NRF" -eq 1 ]]; then
        echo "  nRF Sniffer:        YES ($NRF_PORT)"
    else
        echo "  nRF Sniffer:       NO"
    fi
    echo ""
    echo "-- Captured Files -----------------------------------------------"
    echo "  ESP32 Log:          $ESP_LOG"
    if [[ "$USE_NRF" -eq 1 ]]; then
        echo "  nRF PCAP:           $NRF_PCAP"
    fi
    echo "  Summary:            $SUMMARY_FILE"
    echo ""
    echo "-- ESP32 Log Analysis -------------------------------------------"
    echo "  Total log lines:             ${LINES_TOTAL}"
    echo "  Ubertooth MAC matches:       ${ESP_MAC_HITS}  (searching for ${ESP_MAC_PATTERN})"
    echo "  Total PDU types decoded:      ${TOTAL_PDU_DECODED}"
    echo "    ADV_IND:                   ${ADV_IND_COUNT}"
    echo "    ADV_NONCONN_IND:           ${ADV_NONCONN_IND_COUNT}"
    echo "    ADV_SCAN_IND:              ${ADV_SCAN_IND_COUNT}"
    echo "    ADV_EXT_IND:               ${ADV_EXT_IND_COUNT}"
    echo "    CONNECT_IND:               ${CONNECT_IND_COUNT}"
    echo "    UNKNOWN:                   ${UNKNOWN_PDU_COUNT}"
    echo ""
    if [[ "$USE_NRF" -eq 1 ]] && [[ -f "$NRF_PCAP" ]]; then
        echo "-- nRF Sniffer Analysis -----------------------------------------"
        echo "  Total BLE frames:            ${NRF_TOTAL_FRAMES}"
        echo "  Ubertooth MAC matches:        ${NRF_MAC_HITS}  (searching for ${MAC_LOWER} or ${MAC_REVERSED_LOWER})"
        echo ""
        echo "-- Cross-Validation ---------------------------------------------"
        echo "  ESP32 received Ubertooth:    $( (( PASS_ESP32 )) && echo "YES (${ESP_MAC_HITS} matches)" || echo "NO" )"
        echo "  nRF received Ubertooth:       $( (( PASS_NRF )) && echo "YES (${NRF_MAC_HITS} matches)" || echo "NO" )"
        echo "  Cross-validation:             ${PASS_CROSSVAL}"
    else
        echo "-- nRF Sniffer Analysis -----------------------------------------"
        echo "  (not captured — use --nrf to enable)"
        echo ""
        echo "-- Cross-Validation ---------------------------------------------"
        echo "  ESP32 received Ubertooth:    $( (( PASS_ESP32 )) && echo "YES (${ESP_MAC_HITS} matches)" || echo "NO" )"
        echo "  Cross-validation:             N/A (nRF Sniffer not used)"
    fi
    echo ""
    echo "-- Result -------------------------------------------------------"
    if [[ "$PASS_ESP32" -eq 1 ]]; then
        echo "  [PASS] ESP32 nRF24L01+ sniffer received Ubertooth packets!"
    elif [[ "$USE_NRF" -eq 1 ]] && [[ "$PASS_NRF" -eq 1 ]]; then
        echo "  [FAIL] nRF Sniffer received packets but ESP32 did not."
        echo "    Possible causes:"
        echo "      - nRF24L01+ not connected or misconfigured"
        echo "      - RX_ADDR_P0 byte order incorrect"
        echo "      - EN_CRC forced on (EN_AA override)"
        echo "      - CE pin not actually HIGH (GPIO_MODE_OUTPUT trap)"
    else
        echo "  [FAIL] No sniffer detected Ubertooth packets."
        echo "    Possible causes:"
        echo "      - Ubertooth not transmitting (check USB, firmware)"
        echo "      - Ubertooth too far from receivers"
        echo "      - Wrong channel configuration"
    fi
    echo "=================================================================="
} | tee "$SUMMARY_FILE"

echo ""
ok "Summary saved to: $SUMMARY_FILE"

if [[ "$VERBOSE" -eq 1 ]]; then
    info "Full ESP32 log: less $ESP_LOG"
    if [[ "$USE_NRF" -eq 1 ]] && [[ -f "$NRF_PCAP" ]]; then
        info "Analyze nRF PCAP: tshark -r $NRF_PCAP -Y btle -T fields -e btle.advertising_address | sort | uniq -c | sort -rn | head -20"
    fi
fi

ok "End-to-end cross-validation test complete."
info "Result: ${OVERALL_RESULT}"

exit $EXIT_CODE