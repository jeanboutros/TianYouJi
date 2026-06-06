# 009 — Ubertooth BLE TX Fix: Enabling Packet Injection for Dewhitening Validation

**Date:** 2026-06-06  
**Author:** Wireless Expert  
**Status:** RESOLVED

## Problem

We needed to transmit known BLE advertising packets on specific channels (37, 38, 39) using the Ubertooth One to validate dewhitening on our ESP32 nRF24L01+ sniffer. The `ubertooth-tx` tool was not available.

## Root Cause Analysis

### Hypothesis 1: `ubertooth-tx` not in PATH
**ELIMINATED.** `ubertooth-tx` does not exist anywhere on the system. It was never part of the installed Debian package.

### Hypothesis 2: Debian package doesn't include `ubertooth-tx`
**CONFIRMED.** The Debian package `ubertooth 2018.12.R1-5.3` includes these binaries:
- `ubertooth-afh`, `ubertooth-btle`, `ubertooth-debug`, `ubertooth-dfu`
- `ubertooth-ducky`, `ubertooth-dump`, `ubertooth-ego`, `ubertooth-follow`
- `ubertooth-rx`, `ubertooth-scan`, `ubertooth-specan`, `ubertooth-util`

**`ubertooth-tx` is NOT in the package**, even though the source code exists in the upstream repo.

### Hypothesis 3: `ubertooth-tx` is a useful BLE TX tool
**ELIMINATED.** Reading the upstream source (`ubertooth-tx.c` at tag `2020-12-R1`), the tool explicitly prints:

```
WARNING: This tool currently does nothing!
```

It is a stub for classic Bluetooth symbol retransmission (NEEDS LAP+UAP, sends `cmd_tx_syms`), not for BLE packet injection.

### Hypothesis 4: Firmware lacks BLE TX capability
**ELIMINATED.** The firmware (2020-12-R1) has `TX_ENABLE` compiled in by default (controlled by `DISABLE_TX` make variable, unset by default). It supports two BLE TX modes:

1. **`MODE_BT_SLAVE_LE`** — Advertising slave mode via `UBERTOOTH_BTLE_SLAVE` (cmd=54) + `UBERTOOTH_LE_SET_ADV_DATA` (cmd=71). The firmware constructs proper ADV_IND PDUs with CRC and whitening.

2. **`MODE_TX_GENERIC`** — Raw packet transmission via `UBERTOOTH_TX_GENERIC_PACKET` (cmd=68). Host pre-whitens data and appends CRC. More flexible but requires host-side whitening implementation.

### Hypothesis 5: Host library doesn't expose BLE TX commands
**PARTIALLY TRUE.** The Debian `libubertooth1` (2018.12.R1) has:
- ✅ `cmd_le_set_adv_data` — Sets advertising data
- ✅ `cmd_btle_slave` — Starts BLE slave/advertising mode
- ❌ `cmd_tx_generic_packet` — Does NOT exist (added in later API version but never wrapped in `ubertooth_control.h`)

The existing `cmd_le_set_adv_data` + `cmd_btle_slave` are sufficient for our dewhitening validation.

## Solution

### 1. Built ubertooth host tools from source (2020-12-R1)

The Debian package was two major versions behind. Building from source gives us matching tools for our firmware.

**Prerequisites installed via Homebrew** (since no sudo was available for apt):
```bash
brew install libusb libbtbb pkg-config
```

**Build steps:**
```bash
git clone --branch 2020-12-R1 --depth 1 \
    https://github.com/greatscottgadgets/ubertooth.git /tmp/ubertooth-build/ubertooth

mkdir -p /tmp/ubertooth-build/host/build
cd /tmp/ubertooth-build/host/build

cmake /tmp/ubertooth-build/ubertooth/host \
    -DCMAKE_INSTALL_PREFIX=$HOME/.local \
    -DCMAKE_PREFIX_PATH=/home/linuxbrew/.linuxbrew \
    -DENABLE_PYTHON=OFF

make -j$(nproc)
make install
```

**Result:** All tools installed to `~/.local/bin/` including `ubertooth-tx` (stub), `ubertooth-btle`, `ubertooth-util`, etc.

**Known issue:** `make install` fails at the udev rule copy to `/etc/udev/rules.d/` (needs sudo), but all binaries and libs install fine to `~/.local/`.

### 2. Wrote custom Python BLE TX script

Since `ubertooth-tx` is a useless stub, created `tools/ubertooth_btle_tx.py` which uses pyusb to send USB commands directly via the `UBERTOOTH_BTLE_SLAVE` + `UBERTOOTH_LE_SET_ADV_DATA` firmware API.

**Virtual environment for pyusb:**
```bash
python3 -m venv /tmp/opencode/ubertooth-build/venv
source /tmp/opencode/ubertooth-build/venv/bin/activate
pip install pyusb
```

**Script usage:**
```bash
# Activate venv first
source /tmp/opencode/ubertooth-build/venv/bin/activate

# Transmit on channel 37
python3 tools/ubertooth_btle_tx.py \
    -m AA:BB:CC:DD:EE:FF \
    -d 0201050FFF4C001234 \
    -c 37 -v -t 10

# Cycle through all three advertising channels
python3 tools/ubertooth_btle_tx.py \
    -m DE:AD:BE:EF:00:01 \
    -d 0201060B0954455354 \
    -C 37,38,39 -i 5 -v
```

## How the Firmware BLE TX Works

### Packet Construction (bt_slave_le() in bluetooth_rxtx.c)

The firmware constructs ADV_IND packets as follows:

```
Byte offset  Field           Size   Description
0            PDU Type        1      0x00 = ADV_IND
1            PDU Length      1      6 + len(adv_data)
2..7         AdvA            6      MAC address (byte-reversed from USB input)
8..8+N-1     AdvData         N      From UBERTOOTH_LE_SET_ADV_DATA command
8+N..8+N+2   CRC            3      Computed by firmware (CRC init = 0x555555 for adv)
```

### Data Whitening (le_transmit() in bluetooth_rxtx.c)

Before transmission, the firmware applies BLE data whitening:
- **Polynomial:** x^7 + x^4 + 1 (per Bluetooth Core Spec Vol 6 Part B §3.2)
- **Initial seed:** `whitening_index[btle_channel_index(channel)]` — channel-dependent
- **Byte order:** LSB first within each byte (bit 0 = first to process)
- **Process:** XOR each bit with the whitening sequence LFSR output

### Channel Mapping

| BLE Channel | Frequency | FSDIV Register | Whitening Seed Index |
|-------------|-----------|----------------|---------------------|
| 37          | 2402 MHz  | channel value  | ch_idx=37           |
| 38          | 2426 MHz  | channel value  | ch_idx=38           |
| 39          | 2480 MHz  | channel value  | ch_idx=39           |

### USB Commands Used

| Command                   | Code | Direction | Data                    |
|--------------------------|------|-----------|-------------------------|
| UBERTOOTH_STOP           | 21   | CTRL_OUT  | none                    |
| UBERTOOTH_SET_CHANNEL    | 12   | CTRL_OUT  | wValue = freq_MHz       |
| UBERTOOTH_BTLE_SLAVE     | 54   | CTRL_OUT  | 6-byte MAC address      |
| UBERTOOTH_LE_SET_ADV_DATA| 71   | CTRL_OUT  | adv_data bytes (≤255)  |

## Verification Results

- ✅ Script starts and claims Ubertooth USB device
- ✅ Sets advertising data and MAC via USB control transfers
- ✅ Starts BTLE_SLAVE mode, firmware transmits ADV_IND packets
- ✅ Transmits on channel 37 (2402 MHz): ~10 pkts/sec
- ✅ Transmits on channel 38 (2426 MHz): ~10 pkts/sec
- ✅ Transmits on channel 39 (2480 MHz): ~10 pkts/sec
- ✅ Channel cycling (37→38→39→37...) works correctly
- ✅ Clean stop on timeout, Ctrl-C, or count limit
- ✅ Firmware API version 1.07 compatible with our commands

## Files Created/Modified

| File | Description |
|------|-------------|
| `tools/ubertooth_btle_tx.py` | BLE advertising transmitter script |
| `~/.local/bin/ubertooth-*` | Built from source (2020-12-R1) |
| `~/.local/lib/libubertooth.so*` | Built from source (2020-12-R1) |
| `/tmp/opencode/ubertooth-build/` | Build directory (tmp, not persistent) |

## Usage for Dewhitening Validation

To validate ESP32 dewhitening, transmit known advertising data on a specific channel:

1. Start the TX script on a known channel with known adv data
2. Set the ESP32 nRF24L01+ sniffer to the same channel
3. The ESP32 should receive and dewhiten the packet
4. Compare the dewhitened adv data with the original
5. If they match → dewhitening is correct

Example test sequence:
```bash
# Terminal 1: Transmit test packet on ch37
source /tmp/opencode/ubertooth-build/venv/bin/activate
python3 tools/ubertooth_btle_tx.py \
    -m CA:FE:BA:BE:00:01 \
    -d 0201060B0954455354DEADBEEF \
    -c 37 -v

# Terminal 2: ESP32 sniffer on ch37 (2402 MHz)
# nRF24 config: RF_CH=2, addr={0x6B,0x7D,0x91,0x71}, data_rate=1Mbps
```

## Known Limitations

1. **Firmware always sends ADV_IND** — No ADV_NONCONN_IND, ADV_DIRECT_IND, etc.
2. **Single channel at a time** — No simultaneous multi-channel TX; must stop and switch
3. **Firmware controls whitening** — Cannot send pre-whitened data via BTLE_SLAVE mode (would need TX_GENERIC_PACKET mode)
4. **No CRC verification from host** — CRC is computed by firmware, cannot override
5. **~100ms packet interval** — Firmware sleeps 100ms between transmissions
6. **pyusb venv required** — pip install pyusb in a venv since system Python is externally managed

## References

- [Ubertooth GitHub](https://github.com/greatscottgadgets/ubertooth) — tag 2020-12-R1
- [Ubertooth firmware: bluetooth_rxtx.c](https://github.com/greatscottgadgets/ubertooth/blob/2020-12-R1/firmware/bluetooth_rxtx/bluetooth_rxtx.c) — bt_slave_le(), tx_generic(), le_transmit()
- [Ubertooth host: ubertooth_interface.h](https://github.com/greatscottgadgets/ubertooth/blob/2020-12-R1/host/libubertooth/src/ubertooth_interface.h) — USB command codes
- [Ubertooth host: ubertooth_control.h](https://github.com/greatscottgadgets/ubertooth/blob/2020-12-R1/host/libubertooth/src/ubertooth_control.h) — cmd_* function declarations
- Bluetooth Core Spec Vol 6 Part B §1.4.1 — Channel mapping
- Bluetooth Core Spec Vol 6 Part B §2.3 — Advertising PDU format
- Bluetooth Core Spec Vol 6 Part B §3.2 — Data whitening
