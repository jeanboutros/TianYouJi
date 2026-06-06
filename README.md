# ESP32 nRF24L01+ BLE Sniffer

Passive BLE advertising packet sniffer using an ESP32 and nRF24L01+ module. Captures and dewhitens BLE advertising packets on channels 37, 38, and 39, decoding PDU type and MAC address in real time. Validated against an Ubertooth One as independent ground truth.

## Hardware Requirements

- **ESP32 dev board** — ESP32-DevKitC or equivalent
- **nRF24L01+ module** — the **+** variant is required (250 kbps support, RPD register)
- **Ubertooth One** (optional) — for TX validation; see [testing guide](docs/learning/ubertooth-ble-testing.md)

### Wiring

| ESP32 GPIO | nRF24L01+ Pin | Function                            |
|------------|---------------|-------------------------------------|
| GPIO 19    | MISO          | SPI MISO (nRF24L01+ → ESP32)       |
| GPIO 23    | MOSI          | SPI MOSI (ESP32 → nRF24L01+)      |
| GPIO 18    | SCK           | SPI Clock                           |
| GPIO 17    | CSN           | SPI Chip Select (active low)       |
| GPIO 5     | CE            | Chip Enable (RX/TX mode control)   |
| 3.3 V      | VCC           | Power supply (3.3 V, **NOT 5 V**)  |
| GND        | GND           | Ground                              |

> **Warning:** The nRF24L01+ is a 3.3 V device. Connecting 5 V will damage it.

---

## ESP-IDF Environment Setup

This project uses ESP-IDF v6.0.1. Activate the environment before any build or flash operation:

```bash
# Activate ESP-IDF (do this FIRST in every terminal session)
source ~/.espressif/tools/activate_idf_v6.0.1.sh
```

For VS Code, ensure `.vscode/settings.json` includes:

```jsonc
{
  "idf.currentSetup": "/home/huyang/.espressif/v6.0.1/esp-idf-v6.0",
  "idf.port": "/dev/ttyUSB0",
  "idf.flashType": "UART",
  "C_Cpp.default.compileCommands": "${workspaceFolder}/build/compile_commands.json",
  "C_Cpp.default.configurationProvider": "espressif.esp-idf-extension"
}
```

See [ESP-IDF Getting Started Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/index.html) for full installation instructions.

---

## Build, Flash, and Monitor

```bash
# Build the project
source ~/.espressif/tools/activate_idf_v6.0.1.sh
idf.py build

# Flash to ESP32 (replace /dev/ttyUSB0 with your serial port)
idf.py -p /dev/ttyUSB0 flash

# Monitor serial output (Ctrl+] to exit)
idf.py -p /dev/ttyUSB0 monitor

# Build + flash + monitor in one command
idf.py -p /dev/ttyUSB0 flash monitor
```

---

## Serial Port Permissions

If you encounter "Permission denied" or "not readable" errors on `/dev/ttyUSB0`:

```bash
# Option 1: Add user to dialout group (permanent — requires logout/login)
sudo usermod -a -G dialout $USER

# Option 2: Temporary fix (resets on reboot)
sudo chmod 666 /dev/ttyUSB0

# Find your serial device:
ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null
# Or watch kernel messages when plugging in:
sudo dmesg -w
```

---

## Testing with Ubertooth One

The Ubertooth One transmits known BLE advertising packets that the ESP32 sniffer should decode correctly. See the full guide: [docs/learning/ubertooth-ble-testing.md](docs/learning/ubertooth-ble-testing.md)

Quick validation steps:

```bash
# Step 1: Flash the ESP32 sniffer and start monitoring
idf.py -p /dev/ttyUSB0 flash monitor

# Step 2: In a separate terminal, transmit BLE ADV packets with Ubertooth
python3 tools/ubertooth_btle_tx.py --channel 37 --mac CA:FE:BA:BE:00:01 --interval 100

# Step 3: Look for the MAC in the ESP32 serial output
# Expected: [ch37] ADV_IND  CA:FE:BA:BE:00:01  len=...
```

Ubertooth TX script options:

| Flag               | Description                                  |
|--------------------|----------------------------------------------|
| `--channel 37\|38\|39` | BLE advertising channel                      |
| `--mac XX:XX:XX:XX:XX:XX` | Custom MAC address in transmitted packets |
| `--interval 100`   | Advertising interval in milliseconds         |
| `--count 0`        | Infinite advertising (default)              |

---

## Tools & Scripts

Utility scripts for BLE cross-validation testing and reference verification.

### `tools/e2e_crossval_test.sh` — End-to-end BLE cross-validation test

Transmits known BLE ADV_IND packets from Ubertooth One and verifies reception by both the ESP32 nRF24L01+ sniffer and an nRF52840 Sniffer Dongle. Ideal for CI-style validation.

```bash
# Default: MAC CA:FE:BA:BE:00:01, 30 s timeout
./tools/e2e_crossval_test.sh

# With nRF Sniffer cross-validation, 60 s timeout
./tools/e2e_crossval_test.sh --nrf -t 60

# Skip flash, verbose output
./tools/e2e_crossval_test.sh --no-flash -v
```

Exit codes: 0 = PASS, 1 = ESP32 fail only, 2 = both fail, 3 = error.

### `tools/ble_diag_test.sh` — Automated BLE diagnostic test

Flashes the ESP32, captures serial output, transmits test packets from Ubertooth, and analyses results with a summary report and next-step suggestions.

```bash
# Custom MAC and ADV data, 30 s test
./tools/ble_diag_test.sh -m CA:FE:BA:BE:00:01 -d 0201060B0954455354 -t 30

# With nRF Sniffer, verbose
./tools/ble_diag_test.sh --nrf --nrf-port /dev/ttyACM0 -v
```

### `tools/ubertooth_btle_tx.py` — Ubertooth BLE advertising transmitter

Transmits configurable ADV_IND / ADV_NONCONN_IND packets on channels 37/38/39 with a specified MAC address. Used for cross-validation and diagnostic testing.

```bash
# Transmit on channel 37 with custom MAC, 100 ms interval
python3 tools/ubertooth_btle_tx.py --channel 37 --mac CA:FE:BA:BE:00:01 --interval 100
```

### `docs/pipeline/scripts/ble_whiten_reference.py` — BLE whitening Python reference

Python reference implementation for BLE data whitening/dewhitening LFSR algorithm. Used for verification against the C++ `dewhiten()` implementation.

```bash
# Run round-trip consistency tests
python3 docs/pipeline/scripts/ble_whiten_reference.py --roundtrip

# Print whitening vector for channel 37
python3 docs/pipeline/scripts/ble_whiten_reference.py --vector 37
```

---

## Project Structure

```
esp32/
├── main/main.cpp                              # Application entry point
├── components/nrf24l01plus/                   # nRF24L01+ driver library
│   ├── include/nrf24l01plus/                  # Public headers
│   │   ├── driver.h                           # Driver class
│   │   ├── hal.h                               # Hardware abstraction interface
│   │   ├── ble.h                               # BLE dewhitening (Galois LFSR)
│   │   ├── ble_config.h                        # BLE RX configuration
│   │   ├── diag.h                             # Diagnostic utilities
│   │   ├── commands.h                          # SPI command constants
│   │   ├── registers.h                         # All register headers
│   │   └── registers/                         # Typed register structs
│   │       ├── addresses.h                    # Register address constants
│   │       ├── config.h                       # CONFIG register
│   │       ├── status.h                       # STATUS register with format()
│   │       ├── rf_setup.h                    # RF_SETUP register
│   │       ├── rf_ch.h                       # RF_CH register
│   │       ├── en_rxaddr.h                   # EN_RXADDR register
│   │       ├── en_aa.h                       # EN_AA register
│   │       ├── setup_aw.h                    # SETUP_AW register
│   │       ├── setup_retr.h                  # SETUP_RETR register
│   │       ├── rx_pw.h                       # RX_PW_P0–P5 registers
│   │       ├── dynpd.h                       # DYNPD register
│   │       ├── feature.h                     # FEATURE register
│   │       ├── observe_tx.h                  # OBSERVE_TX register
│   │       ├── rpd.h                          # RPD register
│   │       └── fifo_status.h                 # FIFO_STATUS register
│   └── src/                                  # Implementation files
│       ├── driver.cpp
│       ├── ble.cpp
│       ├── ble_config.cpp
│       └── diag.cpp
├── components/nrf24_espidf/                  # ESP-IDF HAL implementation
│   ├── include/nrf24_espidf/hal_espidf.h    # ESP-IDF HAL header
│   └── src/hal_espidf.cpp                   # ESP-IDF HAL (SPI, GPIO)
├── tests/                                    # Host-side unit tests (GoogleTest)
│   ├── test_ble_dewhiten.cpp                # Dewhitening round-trip tests
│   └── test_rf_setup.cpp                    # RF_SETUP register tests
├── tools/                                    # Utility scripts
│   ├── e2e_crossval_test.sh                # End-to-end BLE cross-validation
│   ├── ble_diag_test.sh                    # Automated BLE diagnostic test
│   └── ubertooth_btle_tx.py                 # Ubertooth BLE TX tool
├── docs/learning/                            # Learning documentation
│   ├── INDEX.md                              # Learning doc index
│   ├── ble-data-whitening-nrf24.md          # Whitening/dewhitening deep dive
│   ├── ubertooth-ble-testing.md             # Ubertooth testing guide
│   ├── nrf24l01plus-register-map.md         # Register map reference
│   ├── nrf24-spi-basics.md                  # SPI wiring & driver basics
│   ├── nrf24-point-to-point.md             # Point-to-point communication
│   ├── nrf24-spectrum-scan.md              # Passive spectrum scan
│   ├── nrf24-ble-packet-crafting.md        # BLE ADV_NONCONN_IND crafting
│   ├── ble-advertising-esp32.md            # ESP32 BLE advertising
│   ├── freertos-task-pinning.md            # FreeRTOS core pinning
│   ├── esp32-app-main-basics.md            # ESP32 firmware entry point
│   ├── rf-legal-boundaries.md             # RF regulatory compliance
│   └── cpp-enum-class-and-struct.md       # Typed enum design patterns
└── docs/pipeline/                           # Multi-agent validation pipeline
```

---

## Key Design Decisions

### Galois LFSR dewhitening

Uses `seed = swapbits(channel_idx) | 0x02` with polynomial `x^7 + x^4 + 1` (encoded as `0x11`), matching the Bluetooth Core Spec and [Dmitry Grinberg's reference implementation](http://dmitry.gr/?r=05.Projects&proj=11.%20Bluetooth%20LE%20fakery). See [BLE Data Whitening on nRF24L01+](docs/learning/ble-data-whitening-nrf24.md) for the full derivation.

### Bit-swap before LFSR

The nRF24L01+ sends bytes MSB-first; BLE expects LSB-first. The `nrf24::ble::dewhiten()` function applies `swapbits()` internally before the LFSR, so callers never need to remember this step.

### BLE access address

`{0x6B, 0x7D, 0x91, 0x71}` — LSByte-first, bit-reversed per nRF24 SPI convention. This is the standard BLE advertising access address `0x8E89BED6` converted to the nRF24's wire format.

### Typed register structs

All nRF24L01+ registers use typed enums and aggregate structs with `to_byte()` / `from_byte()` / `format()` — no raw hex in application code:

```cpp
// BAD — opaque, error-prone
nrf24_write_reg(0x06, 0x26);

// GOOD — self-documenting, type-safe
nrf24::RfSetup rf;
rf.data_rate = nrf24::DataRate::Mbps1;
rf.tx_power  = nrf24::TxPower::dBm0;
radio.write_reg(nrf24::reg::RF_SETUP, rf.to_byte());
```

### Write-and-verify

The driver includes `write_and_verify()` for debug validation — writes a register and confirms the read-back matches, catching SPI wiring errors early.

---

## Troubleshooting

| Problem                           | Likely Cause                              | Fix                                                      |
|-----------------------------------|-------------------------------------------|----------------------------------------------------------|
| `STATUS=0xFF`                     | SPI not connected                         | Check wiring, power, CSN pin                             |
| `pkts=0` for extended period      | No BLE devices, or nRF24 not in RX mode   | Verify `CONFIG=0x03` (PWR_UP=1, PRIM_RX=1), check antenna |
| Garbled MAC addresses             | Dewhitening bug                           | Confirm `swapbits()` is called inside `dewhiten()`       |
| `idf.py build` fails              | ESP-IDF environment not activated         | Run `source ~/.espressif/tools/activate_idf_v6.0.1.sh`   |
| VS Code IntelliSense errors       | Missing `compile_commands.json`           | Build first, then set `C_Cpp.default.compileCommands`   |

---

## Learning Documentation

Non-trivial design topics are documented separately in `docs/learning/`:

| Topic | File |
|-------|------|
| BLE data whitening (Galois LFSR, seed derivation, bug analysis) | [ble-data-whitening-nrf24.md](docs/learning/ble-data-whitening-nrf24.md) |
| Ubertooth BLE testing (TX validation, sniffing) | [ubertooth-ble-testing.md](docs/learning/ubertooth-ble-testing.md) |
| nRF24L01+ register map (typed structs) | [nrf24l01plus-register-map.md](docs/learning/nrf24l01plus-register-map.md) |
| nRF24 SPI basics (wiring, driver) | [nrf24-spi-basics.md](docs/learning/nrf24-spi-basics.md) |
| BLE packet crafting (raw ADV_NONCONN_IND) | [nrf24-ble-packet-crafting.md](docs/learning/nrf24-ble-packet-crafting.md) |
| RF legal boundaries | [rf-legal-boundaries.md](docs/learning/rf-legal-boundaries.md) |
| C++ enum class & struct patterns | [cpp-enum-class-and-struct.md](docs/learning/cpp-enum-class-and-struct.md) |
| Changelog (bug fixes, design rationale) | [CHANGELOG.md](docs/learning/CHANGELOG.md) |

Full index: [docs/learning/INDEX.md](docs/learning/INDEX.md)

---

## License

ESP-IDF project template — Apache 2.0 / CC0-1.0