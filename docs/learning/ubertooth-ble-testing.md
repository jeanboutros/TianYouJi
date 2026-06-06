# Ubertooth One as a BLE Test Tool for nRF24L01+ Firmware Validation

## 1. Overview

The **Ubertooth One** is an open-source 2.4 GHz USB software-defined radio (SDR)
designed by [Great Scott Gadgets](https://greatscottgadgets.com/ubertoothone/) for
Bluetooth experimentation.  It ships with a capable BLE (Bluetooth Smart) sniffer
and can also transmit BLE advertising packets.

### Why use it with nRF24L01+?

The Ubertooth serves as **independent ground truth** for validating our ESP32
nRF24L01+ BLE sniffer firmware.  Because it implements the full BLE PHY
(including whitening, CRC-24, and access address detection) in its own CC2400
transceiver and firmware, its decoded output is independent from the nRF24
hardware.  If both devices decode the same over-the-air packet to the same PDU
bytes, our dewhitening and protocol parsing are confirmed correct.

### Firmware and API versions

| Item | Value |
|------|-------|
| Firmware version | `2020-12-R1` (latest release) |
| API version | `1.07` |
| Host lib version | `2018.12.R1` (Debian) or built from source `2020-12-R1` |

### Key capabilities used in this project

| Capability | Firmware mode | Our use |
|------------|--------------|---------|
| BLE passive sniffing | `MODE_RX_GENERIC` / `ubertooth-btle` | Capture real BLE ads as ground truth |
| BLE advertising TX | `MODE_BT_SLAVE_LE` (`UBERTOOTH_BTLE_SLAVE`) | Inject known packets for dewhitening validation |
| Spectrum analysis | `MODE_SPECAN` / `ubertooth-specan` | Verify channel activity and RF environment |

---

## 2. Ubertooth Architecture

### 2.1 Hardware overview

| Component | Part | Role |
|-----------|------|------|
| Transceiver | TI CC2400 | 2.4 GHz RF, GFSK modulation, hardware CRC |
| MCU | NXP LPC175x | Firmware, USB protocol, packet construction |
| Interface | USB 2.0 Full Speed | Vendor-specific control transfers + bulk endpoints |
| Antenna | RP-SMA dipole | 2.4 GHz, supplied with the device |

The CC2400 handles the RF physical layer (modulation, demodulation, frequency
synthesis).  The LPC175x MCU runs the firmware that constructs BLE packets,
applies whitening, computes CRC, and manages the USB interface.

### 2.2 Firmware modes relevant to BLE

| Mode constant | Value | Purpose |
|---------------|-------|---------|
| `MODE_BT_SLAVE_LE` | Advertising TX slave | Transmits ADV_IND PDUs repeatedly |
| `MODE_RX_GENERIC` | Raw capture | Passively receives and decodes BLE packets |
| `MODE_SPECAN` | Spectrum analyzer | Reports RSSI per frequency bin |

### 2.3 BLE packet construction in firmware

When operating in `MODE_BT_SLAVE_LE`, the firmware function `bt_slave_le()` in
`bluetooth_rxtx.c` constructs a complete BLE on-air packet:

```
Access Address (4 bytes) → PDU Header (2 bytes) → AdvA (6 bytes) → AdvData (N bytes) → CRC (3 bytes)
```

| Field | Size | Description |
|-------|------|-------------|
| Access Address | 4 bytes | Always `0x8E89BED6` for advertising |
| PDU Header | 2 bytes | Type `0x00` (ADV_IND), length = 6 + len(AdvData) |
| AdvA | 6 bytes | MAC address (byte-reversed from USB input) |
| AdvData | N bytes | Set via `UBERTOOTH_LE_SET_ADV_DATA` command |
| CRC | 3 bytes | CRC-24 computed by firmware (init = `0x555555` for advertising) |

The firmware then applies BLE data whitening and CRC in `le_transmit()` before
putting the packet on-air.  The host does **not** need to pre-whiten or compute
CRC — the firmware handles both automatically.

### 2.4 Firmware automatic processing in BTLE_SLAVE mode

| Task | Who does it | Can host override? |
|------|------------|-------------------|
| CRC-24 computation | Firmware | No |
| Data whitening (x^7+x^4+1 LFSR) | Firmware | No |
| Access address insertion | Firmware | No |
| PDU header construction | Firmware | No (always ADV_IND) |
| Whitening seed selection | Firmware (channel-dependent) | No |
| ~100ms packet interval | Firmware timer | No |

This means the **only inputs the host controls** are:
1. The MAC address (6 bytes, via `UBERTOOTH_BTLE_SLAVE`)
2. The advertising data payload (N bytes, via `UBERTOOTH_LE_SET_ADV_DATA`)
3. The RF channel/frequency (via `UBERTOOTH_SET_CHANNEL`)

---

## 3. USB Command Protocol (Full Analysis)

The Ubertooth communicates with the host via USB vendor-specific control
transfers.  All commands are sent on the default control endpoint (endpoint 0).

### 3.1 USB request type constants

**Python** (using pyusb):

```python
import usb.util

CTRL_OUT = usb.util.build_request_type(
    usb.util.CTRL_OUT,          # host-to-device
    usb.util.CTRL_TYPE_VENDOR,  # vendor-specific
    usb.util.CTRL_RECIPIENT_DEVICE  # device recipient
)

CTRL_IN = usb.util.build_request_type(
    usb.util.CTRL_IN,           # device-to-host
    usb.util.CTRL_TYPE_VENDOR,
    usb.util.CTRL_RECIPIENT_DEVICE
)
```

**C++** (using libusb):

```cpp
#include <libusb.h>

// Host → Device (control OUT)
const uint8_t CTRL_OUT = LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_ENDPOINT_OUT;

// Device → Host (control IN)
const uint8_t CTRL_IN = LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_ENDPOINT_IN;
```

### 3.2 Command table

| Command | Code | Direction | wValue | wIndex | Data | Purpose |
|---------|------|-----------|--------|--------|------|---------|
| `UBERTOOTH_STOP` | 21 | CTRL_OUT | 0 | 0 | none | Stop current mode |
| `UBERTOOTH_SET_CHANNEL` | 12 | CTRL_OUT | freq_MHz | 0 | none | Set RF channel (frequency in MHz) |
| `UBERTOOTH_BTLE_SLAVE` | 54 | CTRL_OUT | 0 | 0 | 6-byte MAC | Start BLE ADV_IND TX |
| `UBERTOOTH_LE_SET_ADV_DATA` | 71 | CTRL_OUT | 0 | 0 | adv_data bytes | Set advertising payload |
| `UBERTOOTH_GET_PART_NUM` | 5 | CTRL_IN | 0 | 0 | 4 bytes | Read LPC175x part number |
| `UBERTOOTH_GET_SERIAL` | 6 | CTRL_IN | 0 | 0 | 4 bytes | Read serial number |
| `UBERTOOTH_GET_API_VERSION` | 9 | CTRL_IN | 0 | 0 | 2 bytes | Read firmware API version |

### 3.3 Python examples — sending each command

```python
# Stop any ongoing Ubertooth operation
dev.ctrl_transfer(CTRL_OUT, 21, 0, 0, b'', 1000)

# Set RF channel to 2402 MHz (BLE advertising channel 37)
dev.ctrl_transfer(CTRL_OUT, 12, 2402, 0, b'', 1000)

# Start BLE advertising slave with MAC address CA:FE:BA:BE:00:01
mac = bytes.fromhex('CAFEBABE0001')
dev.ctrl_transfer(CTRL_OUT, 54, 0, 0, mac, 1000)

# Set advertising data payload (Flags + Complete Local Name "TEST")
adv_data = bytes.fromhex('0201060B0954455354')
dev.ctrl_transfer(CTRL_OUT, 71, 0, 0, adv_data, 1000)

# Read API version (2 bytes, little-endian)
api_ver = dev.ctrl_transfer(CTRL_IN, 9, 0, 0, 2, 1000)
print(f"API version: {api_ver[0] | (api_ver[1] << 8)}")

# Read part number (4 bytes, little-endian)
part = dev.ctrl_transfer(CTRL_IN, 5, 0, 0, 4, 1000)
print(f"Part number: {int.from_bytes(part, 'little')}")

# Read serial number (4 bytes, little-endian)
serial = dev.ctrl_transfer(CTRL_IN, 6, 0, 0, 4, 1000)
print(f"Serial: {int.from_bytes(serial, 'little')}")
```

### 3.4 C++ examples — sending each command (raw libusb)

```cpp
#include <libusb.h>
#include <cstdio>
#include <cstdint>

const uint8_t CTRL_OUT = LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_ENDPOINT_OUT;
const uint8_t CTRL_IN  = LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_ENDPOINT_IN;

// Ubertooth USB IDs
constexpr uint16_t UBERTOOTH_VID = 0x1d50;
constexpr uint16_t UBERTOOTH_PID = 0x6002;

// Command codes (from ubertooth_interface.h)
constexpr uint8_t UBERTOOTH_STOP           = 21;
constexpr uint8_t UBERTOOTH_SET_CHANNEL     = 12;
constexpr uint8_t UBERTOOTH_BTLE_SLAVE      = 54;
constexpr uint8_t UBERTOOTH_LE_SET_ADV_DATA = 71;
constexpr uint8_t UBERTOOTH_GET_PART_NUM    =  5;
constexpr uint8_t UBERTOOTH_GET_SERIAL      =  6;
constexpr uint8_t UBERTOOTH_GET_API_VERSION =  9;

/**
 * @brief Send UBERTOOTH_STOP to halt any ongoing operation.
 */
int ubertooth_stop(libusb_device_handle *h) {
    return libusb_control_transfer(h, CTRL_OUT,
        UBERTOOTH_STOP, 0, 0, nullptr, 0, 1000);
}

/**
 * @brief Set RF channel frequency in MHz.
 * @param freq_mhz  Frequency (e.g. 2402 for BLE adv ch37).
 */
int set_channel(libusb_device_handle *h, uint16_t freq_mhz) {
    return libusb_control_transfer(h, CTRL_OUT,
        UBERTOOTH_SET_CHANNEL, freq_mhz, 0, nullptr, 0, 1000);
}

/**
 * @brief Start BLE advertising TX with the given 6-byte MAC address.
 * @param mac  Pointer to 6-byte MAC address.
 */
int start_btle_slave(libusb_device_handle *h, const uint8_t mac[6]) {
    return libusb_control_transfer(h, CTRL_OUT,
        UBERTOOTH_BTLE_SLAVE, 0, 0,
        const_cast<uint8_t*>(mac), 6, 1000);
}

/**
 * @brief Set the advertising data payload.
 * @param data  Pointer to payload bytes.
 * @param len   Number of payload bytes (1–255).
 */
int set_adv_data(libusb_device_handle *h, const uint8_t *data, uint8_t len) {
    return libusb_control_transfer(h, CTRL_OUT,
        UBERTOOTH_LE_SET_ADV_DATA, 0, 0,
        const_cast<uint8_t*>(data), len, 1000);
}

/**
 * @brief Read the firmware API version (2 bytes, returns raw 16-bit value).
 */
int get_api_version(libusb_device_handle *h, uint16_t &out) {
    uint8_t buf[2];
    int r = libusb_control_transfer(h, CTRL_IN,
        UBERTOOTH_GET_API_VERSION, 0, 0, buf, 2, 1000);
    if (r == 2) {
        out = static_cast<uint16_t>(buf[0] | (buf[1] << 8));
        return 0;
    }
    return r;
}

/**
 * @brief Read the LPC175x part number (4 bytes).
 */
int get_part_num(libusb_device_handle *h, uint32_t &out) {
    uint8_t buf[4];
    int r = libusb_control_transfer(h, CTRL_IN,
        UBERTOOTH_GET_PART_NUM, 0, 0, buf, 4, 1000);
    if (r == 4) {
        out = static_cast<uint32_t>(
            buf[0] | (buf[1] << 8) | (buf[2] << 16) | (buf[3] << 24));
        return 0;
    }
    return r;
}
```

### 3.5 C++ examples — using libubertooth (higher-level API)

The host library `libubertooth` provides wrapper functions for the most common
commands.  These are more convenient than raw libusb calls but require linking
against `libubertooth.so`.

```cpp
#include <ubertooth/ubertooth_control.h>
#include <ubertooth/ubertooth_interface.h>

/**
 * @brief Start BLE advertising TX using the libubertooth API.
 *
 * @code
 *   ubertooth_t *ut = ubertooth_start();  // find and open device
 *   cmd_set_channel(ut, 2402);           // BLE adv ch37
 *   cmd_le_set_adv_data(ut, adv_data, adv_len);
 *   cmd_btle_slave(ut, mac_bytes);
 *   // ... firmware now transmits ADV_IND packets ...
 *   cmd_stop(ut);
 * @endcode
 */
```

---

## 4. BLE Channel Mapping

BLE defines 40 channels in the 2.4 GHz ISM band: 37 data channels (0–36) and
3 advertising channels (37–39).  The nRF24L01+ uses a different addressing
scheme: `Frequency = 2400 + RF_CH [MHz]`, where `RF_CH` ranges from 0 to 125.

### 4.1 Complete advertising channel mapping

| BLE Channel | Frequency | nRF24 `RF_CH` | Ubertooth `wValue` | LFSR Seed |
|-------------|-----------|---------------|---------------------|-----------|
| 37 | 2402 MHz | 2 | 2402 | `swapbits(37) | 2 = 0xA6` |
| 38 | 2426 MHz | 26 | 2426 | `swapbits(38) | 2 = 0x66` |
| 39 | 2480 MHz | 80 | 2480 | `swapbits(39) | 2 = 0xE6` |

### 4.2 Data channel mapping (for reference)

| BLE Channel | Frequency | nRF24 `RF_CH` |
|-------------|-----------|---------------|
| 0 | 2404 MHz | 4 |
| 1 | 2406 MHz | 6 |
| ... | ... | ... |
| 10 | 2424 MHz | 24 |
| 11 | 2428 MHz | 28 |
| ... | ... | ... |
| 36 | 2478 MHz | 78 |

For the complete mapping, use the library function
`nrf24::ble::channel_to_rf_ch()` (defined in `nrf24l01plus/ble_config.h`).

### 4.3 Python — channel-to-frequency mapping

```python
def ble_ch_to_freq(ch):
    """Convert a BLE channel index (0–39) to frequency in MHz.

    Per Bluetooth Core Spec Vol 6 Part B §1.4.1.
    """
    if ch == 37: return 2402
    elif ch == 38: return 2426
    elif ch == 39: return 2480
    elif 0 <= ch <= 36: return 2404 + ch * 2
    else: raise ValueError(f"Invalid BLE channel: {ch}")
```

### 4.4 C++ — channel-to-frequency mapping

```cpp
/**
 * @brief Convert a BLE channel index (0–39) to frequency in MHz.
 *
 * Per Bluetooth Core Spec Vol 6 Part B §1.4.1.
 * For the nRF24 RF_CH value, use nrf24::ble::channel_to_rf_ch() instead.
 *
 * @code
 *   ble_ch_to_freq(37)  // returns 2402
 *   ble_ch_to_freq(0)   // returns 2404
 * @endcode
 *
 * @param ch  BLE channel index (0–39).
 * @return    Frequency in MHz, or 0 if channel is invalid.
 */
constexpr uint16_t ble_ch_to_freq(uint8_t ch) {
    if (ch == 37) return 2402;
    if (ch == 38) return 2426;
    if (ch == 39) return 2480;
    if (ch <= 36) return static_cast<uint16_t>(2404 + ch * 2);
    return 0; // invalid channel
}
```

### 4.5 Using the library's typed vocabulary for channel mapping

Prefer using the library's own functions over computing the mapping manually:

```cpp
#include <nrf24l01plus/ble_config.h>

// The library provides:
//   nrf24::ble::channel_to_rf_ch(37)  → 2
//   nrf24::ble::ADV_CHANNELS[0].rf_ch → 2  (ch37)
//   nrf24::ble::ADV_CHANNELS[1].rf_ch → 26 (ch38)
//   nrf24::ble::ADV_CHANNELS[2].rf_ch → 80 (ch39)

// Configure the nRF24 for BLE advertising channel 37:
nrf24::Driver radio(hal);
nrf24::ble::configure_rx(radio);
nrf24::ble::switch_channel(radio, 37);  // tunes to 2402 MHz
radio.ce_high();  // start listening
```

**Python equivalent** (for host-side scripts):

```python
def ble_ch_to_nrf24_rf_ch(ch):
    """Convert BLE channel to nRF24L01+ RF_CH register value.

    Matches nrf24::ble::channel_to_rf_ch() from ble_config.h.
    """
    if ch == 37: return 2
    elif ch == 38: return 26
    elif ch == 39: return 80
    elif ch <= 10: return 4 + 2 * ch
    elif ch <= 36: return 28 + 2 * (ch - 11)
    else: raise ValueError(f"Invalid BLE channel: {ch}")
```

---

## 5. Transmitting BLE Packets with Ubertooth

### 5.1 Using our Python script (`tools/ubertooth_btle_tx.py`)

Our custom script bypasses the useless `ubertooth-tx` stub and sends USB
commands directly to the firmware's BTLE_SLAVE mode.

**Setup:**

```bash
# Install pyusb (required)
python3 -m venv /tmp/opencode/ubertooth-build/venv
source /tmp/opencode/ubertooth-build/venv/bin/activate
pip install pyusb
```

**Transmit ADV_IND on a single channel:**

```bash
python3 tools/ubertooth_btle_tx.py \
    -m CA:FE:BA:BE:00:01 \
    -d 0201060B0954455354 \
    -c 37 -v
```

This transmits:
- **MAC:** `CA:FE:BA:BE:00:01`
- **AdvData:** `0201060B0954455354` (Flags=LE General Discoverable + Complete Local Name "TEST")
- **Channel:** 37 (2402 MHz)
- **PDU type:** ADV_IND (firmware always uses this type)
- **Interval:** ~100ms (firmware-controlled)

**Cycle through all three advertising channels:**

```bash
python3 tools/ubertooth_btle_tx.py \
    -m DE:AD:BE:EF:00:01 \
    -d 020105 \
    -C 37,38,39 -i 5 -v
```

This transmits on ch37 for 5 seconds, then ch38 for 5 seconds, then ch39 for
5 seconds, cycling indefinitely.

**What the firmware does automatically:**

| Step | Firmware | Host input needed |
|------|----------|-------------------|
| PDU header (type=0x00, length) | Auto | No |
| AdvA (6-byte MAC, byte-reversed) | Auto | MAC via `UBERTOOTH_BTLE_SLAVE` |
| AdvData (payload) | Auto | Data via `UBERTOOTH_LE_SET_ADV_DATA` |
| CRC-24 computation | Auto (init=0x555555) | No |
| Data whitening | Auto (LFSR seed from channel) | No |
| Access address (0x8E89BED6) | Auto | No |

### 5.2 Using C++ with libubertooth

The libubertooth host library provides the same commands via `cmd_*` functions:

```cpp
#include <ubertooth/ubertooth_control.h>
#include <ubertooth/ubertooth_interface.h>
#include <cstdio>
#include <cstdint>

/**
 * @brief Transmit BLE ADV_IND advertising packets on channel 37.
 *
 * Uses libubertooth high-level API for simplicity.
 *
 * @code
 *   ubertooth_t *ut = ubertooth_start();
 *   if (!ut) { fprintf(stderr, "No Ubertooth found\n"); return 1; }
 *
 *   cmd_stop(ut);
 *   cmd_set_channel(ut, 2402);  // ch37 = 2402 MHz
 *
 *   uint8_t adv_data[] = {0x02, 0x01, 0x06, 0x0B, 0x09,
 *                         0x54, 0x45, 0x53, 0x54};  // "TEST"
 *   cmd_le_set_adv_data(ut, adv_data, sizeof(adv_data));
 *
 *   uint8_t mac[] = {0xCA, 0xFE, 0xBA, 0xBE, 0x00, 0x01};
 *   cmd_btle_slave(ut, mac);
 *
 *   sleep(10);  // transmit for 10 seconds
 *   cmd_stop(ut);
 * @endcode
 */
```

### 5.3 Raw USB commands (C++ with libusb — complete compilable example)

This is the lowest level: direct `libusb_control_transfer` calls without
depending on libubertooth.  This example is complete and compilable.

```cpp
/**
 * @file ubertooth_raw_tx.cpp
 * @brief Transmit BLE ADV_IND packets using raw libusb commands.
 *
 * Compile:
 *   g++ -std=c++17 -o ubertooth_raw_tx ubertooth_raw_tx.cpp -lusb-1.0
 *
 * Run:
 *   ./ubertooth_raw_tx
 */

#include <libusb.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <unistd.h>

constexpr uint16_t UBERTOOTH_VID = 0x1d50;
constexpr uint16_t UBERTOOTH_PID = 0x6002;

constexpr uint8_t CTRL_OUT = LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_ENDPOINT_OUT;

// USB command codes (ubertooth_interface.h)
constexpr uint8_t UBERTOOTH_STOP           = 21;
constexpr uint8_t UBERTOOTH_SET_CHANNEL    = 12;
constexpr uint8_t UBERTOOTH_BTLE_SLAVE     = 54;
constexpr uint8_t UBERTOOTH_LE_SET_ADV_DATA = 71;

int main() {
    // Initialize libusb
    libusb_context *ctx = nullptr;
    int r = libusb_init(&ctx);
    if (r < 0) {
        fprintf(stderr, "libusb_init failed: %s\n", libusb_strerror(r));
        return 1;
    }

    // Find Ubertooth One
    libusb_device_handle *h = libusb_open_device_with_vid_pid(ctx,
        UBERTOOTH_VID, UBERTOOTH_PID);
    if (!h) {
        fprintf(stderr, "Ubertooth One not found (VID:PID = 1d50:6002)\n");
        libusb_exit(ctx);
        return 1;
    }
    printf("Ubertooth One found\n");

    // Claim USB interface
    if (libusb_claim_interface(h, 0) < 0) {
        fprintf(stderr, "Cannot claim interface (another process using it?)\n");
        libusb_close(h);
        libusb_exit(ctx);
        return 1;
    }

    // Step 1: Stop any ongoing operation
    r = libusb_control_transfer(h, CTRL_OUT,
        UBERTOOTH_STOP, 0, 0, nullptr, 0, 1000);
    if (r < 0)
        fprintf(stderr, "Warning: UBERTOOTH_STOP failed: %s\n", libusb_strerror(r));
    usleep(200000);  // 200ms settle

    // Step 2: Set channel to 2402 MHz (BLE advertising ch37)
    r = libusb_control_transfer(h, CTRL_OUT,
        UBERTOOTH_SET_CHANNEL, 2402, 0, nullptr, 0, 1000);
    if (r < 0) {
        fprintf(stderr, "SET_CHANNEL failed: %s\n", libusb_strerror(r));
        goto cleanup;
    }
    printf("Channel set: 2402 MHz (BLE adv ch37)\n");

    // Step 3: Set advertising data
    // Flags: LE General Discoverable, BR/EDR Not Supported
    // Complete Local Name: "TEST"
    uint8_t adv_data[] = {0x02, 0x01, 0x06, 0x0B, 0x09,
                          0x54, 0x45, 0x53, 0x54};
    r = libusb_control_transfer(h, CTRL_OUT,
        UBERTOOTH_LE_SET_ADV_DATA, 0, 0,
        adv_data, sizeof(adv_data), 1000);
    if (r < 0) {
        fprintf(stderr, "LE_SET_ADV_DATA failed: %s\n", libusb_strerror(r));
        goto cleanup;
    }
    printf("Adv data set: %zu bytes\n", sizeof(adv_data));

    // Step 4: Start BLE advertising TX with MAC address
    uint8_t mac[] = {0xCA, 0xFE, 0xBA, 0xBE, 0x00, 0x01};
    r = libusb_control_transfer(h, CTRL_OUT,
        UBERTOOTH_BTLE_SLAVE, 0, 0, mac, 6, 1000);
    if (r < 0) {
        fprintf(stderr, "BTLE_SLAVE failed: %s\n", libusb_strerror(r));
        goto cleanup;
    }
    printf("BLE advertising TX started (MAC: CA:FE:BA:BE:00:01)\n");

    // Let it transmit for 10 seconds
    printf("Transmitting for 10 seconds...\n");
    sleep(10);

    // Step 5: Stop
    r = libusb_control_transfer(h, CTRL_OUT,
        UBERTOOTH_STOP, 0, 0, nullptr, 0, 1000);
    printf("Stopped.\n");

cleanup:
    libusb_release_interface(h, 0);
    libusb_close(h);
    libusb_exit(ctx);
    return 0;
}
```

---

## 6. Sniffing BLE Packets with Ubertooth

The Ubertooth can passively capture BLE packets, serving as ground truth for our
ESP32 nRF24L01+ sniffer output.

### 6.1 Basic sniffing commands

```bash
# Sniff on a specific advertising channel
ubertooth-btle -c 37           # channel 37 (2402 MHz)
ubertooth-btle -c 38           # channel 38 (2426 MHz)
ubertooth-btle -c 39           # channel 39 (2480 MHz)

# Sniff for a limited time
ubertooth-btle -c 37 -t 10    # 10 seconds, then exit

# Stop any ongoing Ubertooth operation
ubertooth-util -S
```

### 6.2 Piping to Wireshark/tshark for protocol analysis

```bash
# Live capture piped to tshark for decoding
ubertooth-btle -U -1 | tshark -i -

# Capture to PCAP file for later analysis
ubertooth-btle -c 37 -q /tmp/ch37.pcap &

# Decode a saved capture with tshark
tshark -r /tmp/ch37.pcap -T fields \
    -e btle.channel_idx \
    -e btle.pdu_type \
    -e btle.advertising_address \
    -e btle.advertising_data \
    -e btle.crc

# Verbose BLE decode
tshark -r /tmp/ch37.pcap -V -O btle
```

### 6.3 Capturing on all three advertising channels

```bash
# Sequential capture on each channel
for ch in 37 38 39; do
    echo "=== Channel $ch ==="
    ubertooth-btle -n -A${ch} -q /tmp/ch${ch}.pcap &
    BTLE_PID=$!
    sleep 20
    kill $BTLE_PID 2>/dev/null
done
```

### 6.4 Comparing Ubertooth capture with ESP32 nRF24 sniffer output

The key comparison principle: **Ubertooth outputs dewhitened PDU bytes**.  Our
ESP32 sniffer also outputs dewhitened PDU bytes (after `nrf24::ble::dewhiten()`).
If dewhitening is correct, the two outputs must match byte-for-byte.

```bash
# Terminal 1: Ubertooth captures and decodes to PCAP
ubertooth-btle -c 37 -q /tmp/ground_truth.pcap &

# Terminal 2: ESP32 nRF24 sniffer outputs raw+dewhitened hex to serial
idf.py -p /dev/ttyUSB0 monitor

# Terminal 3: Decode Ubertooth PCAP for comparison
tshark -r /tmp/ground_truth.pcap -T fields \
    -e btle.advertising_address \
    -e btle.advertising_data
```

**Important:** The `ubertooth-btle` output shows **dewhitened** data.  No
additional dewhitening step is needed for the Ubertooth side.  Compare its PDU
output directly with the ESP32's `dewhiten()` output.

---

## 7. How to Use Ubertooth to Test Our Firmware

This is the most important section.  It covers ALL techniques for using the
Ubertooth to validate the ESP32 nRF24L01+ BLE sniffer firmware.

### 7.1 Technique 1: Crafted Packet Injection + Sniffer Comparison

**Principle:** Transmit a known advertising payload with the Ubertooth.
The ESP32 nRF24L01+ sniffer receives the same on-air packet, dewhitens it,
and compares with the known original payload.

**Step-by-step procedure:**

1. Choose a known advertising data payload (e.g., Flags + Complete Local Name)
2. Choose a known MAC address
3. Transmit with Ubertooth on a specific channel (e.g., ch37)
4. ESP32 sniffer listens on the same channel
5. Compare the dewhitened output with the original payload

**Python — craft and transmit the packet:**

```python
#!/usr/bin/env python3
"""Transmit a known BLE advertising packet for dewhitening validation."""
import usb.core
import usb.util
import time

CTRL_OUT = usb.util.build_request_type(
    usb.util.CTRL_OUT, usb.util.CTRL_TYPE_VENDOR, usb.util.CTRL_RECIPIENT_DEVICE)

dev = usb.core.find(idVendor=0x1d50, idProduct=0x6002)
assert dev is not None, "Ubertooth not found"

if dev.is_kernel_driver_active(0):
    dev.detach_kernel_driver(0)
dev.set_configuration()
usb.util.claim_interface(dev, 0)

# Stop any ongoing operation
dev.ctrl_transfer(CTRL_OUT, 21, 0, 0, b'', 1000)
time.sleep(0.2)

# Set channel to 2402 MHz (BLE adv ch37)
dev.ctrl_transfer(CTRL_OUT, 12, 2402, 0, b'', 1000)

# Known advertising data: Flags (0x02 0x01 0x06) + Complete Local Name "HELLO"
known_adv_data = bytes.fromhex('020106050948454C4C4F')
dev.ctrl_transfer(CTRL_OUT, 71, 0, 0, known_adv_data, 1000)

# Known MAC address
known_mac = bytes.fromhex('DEADBEEF0001')
dev.ctrl_transfer(CTRL_OUT, 54, 0, 0, known_mac, 1000)

print(f"Transmitting on ch37 (2402 MHz)")
print(f"  MAC:     DE:AD:BE:EF:00:01")
print(f"  AdvData: {known_adv_data.hex().upper()}")
print(f"  Expected dewhitened PDU header: 00 0B")
print(f"  Expected AdvA (after dewhiten): DEADBEEF0001")
print(f"  Expected AdvData (after dewhiten): 02 01 06 05 09 48 45 4C 4C 4F")

# Transmit for 30 seconds
time.sleep(30)

dev.ctrl_transfer(CTRL_OUT, 21, 0, 0, b'', 1000)
usb.util.release_interface(dev, 0)
print("Done.")
```

**C++ — ESP32 side: receive, dewhiten, and validate:**

```cpp
#include <nrf24l01plus/driver.h>
#include <nrf24l01plus/ble_config.h>
#include <nrf24l01plus/ble.h>

/**
 * @brief Receive a BLE advertising packet and validate dewhitening.
 *
 * Demonstrates Technique 1: receive a known packet from Ubertooth,
 * dewhiten it, and compare against expected values.
 *
 * @code
 *   nrf24::Driver radio(hal);
 *   nrf24::ble::configure_rx(radio);       // BLE passive RX setup
 *   nrf24::ble::switch_channel(radio, 37);  // ch37 = 2402 MHz
 *   radio.ce_high();
 *
 *   while (true) {
 *       if (nrf24::ble::rx_available(radio)) {
 *           uint8_t buf[32];
 *           radio.read_payload(buf, 32);
 *           nrf24::ble::dewhiten(buf, 32, 37);
 *           // buf now holds dewhitened PDU (LSbit-first format)
 *           // Compare with expected values from Ubertooth TX
 *       }
 *   }
 * @endcode
 */
void validate_dewhiten(nrf24::Driver &radio) {
    // Known values from Ubertooth TX:
    // AdvData = 020106 050948454C4C4F ("HELLO")
    // PDU type = ADV_IND (0x00), length = 0x0B (6+5)
    static const uint8_t expected_pdu_header[] = {0x00, 0x0B};
    static const uint8_t expected_adva[]       = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01};
    static const uint8_t expected_advdata[]    = {0x02, 0x01, 0x06,
                                                   0x05, 0x09,
                                                   0x48, 0x45, 0x4C, 0x4C, 0x4F};

    nrf24::ble::configure_rx(radio);
    nrf24::ble::switch_channel(radio, 37);  // advertising ch37
    radio.ce_high();

    uint8_t buf[32] = {};
    bool match = false;

    for (int attempt = 0; attempt < 100; ++attempt) {
        if (!nrf24::ble::rx_available(radio)) continue;

        radio.read_payload(buf, 32);
        nrf24::ble::dewhiten(buf, 32, 37);  // bit-swap + Galois LFSR dewhiten
        nrf24::ble::clear_irq_flags(radio);

        // Check PDU header
        if (buf[0] != expected_pdu_header[0] || buf[1] != expected_pdu_header[1])
            continue;

        // Check AdvA (bytes 2..7)
        if (memcmp(&buf[2], expected_adva, 6) != 0)
            continue;

        // Check AdvData (bytes 8..8+9)
        if (memcmp(&buf[8], expected_advdata, sizeof(expected_advdata)) != 0)
            continue;

        match = true;
        break;
    }

    printf("Dewhiten validation: %s\n", match ? "PASS" : "FAIL");
}
```

**Expected pass/fail criteria:**

| Check | Expected |
|-------|----------|
| PDU type (byte 0, lower 4 bits) | `0x00` (ADV_IND) |
| PDU length (byte 1) | `0x0B` (6 + 5 = 11) |
| AdvA (bytes 2–7) | `DE AD BE EF 00 01` |
| AdvData (bytes 8–16) | `02 01 06 05 09 48 45 4C 4C 4F` |

### 7.2 Technique 2: Ubertooth Passive Capture as Ground Truth

**Principle:** Run both Ubertooth and ESP32 simultaneously capturing the same
real BLE traffic.  Use Ubertooth's decoded output as the reference standard.

**Procedure:**

1. Start Ubertooth capturing on a specific channel
2. Configure ESP32 to listen on the same channel
3. Let both capture for 30+ seconds
4. Decode Ubertooth PCAP with `tshark`
5. Compare PDU types, MAC addresses, and advertising data byte-by-byte

**Advantages:**
- Uses real BLE devices (phones, AirTags, etc.) — no special transmitter needed
- Tests against naturally varying packet contents
- Ubertooth is an independent, spec-compliant implementation

**Limitations:**
- Ubertooth dewhitens before output — you cannot see the raw whitened bytes
- Packet timing between two independent receivers may differ (one may miss packets the other catches)
- Both devices must be within range of the same BLE advertiser

**Python — read and compare from PCAP:**

```python
#!/usr/bin/env python3
"""Compare Ubertooth PCAP with ESP32 sniffer output."""
import subprocess
import sys

def extract_ubertooth_pdus(pcap_file, channel):
    """Extract decoded PDU fields from Ubertooth PCAP."""
    result = subprocess.run(
        ['tshark', '-r', pcap_file,
         '-Y', f'btle.channel_idx=={channel}',
         '-T', 'fields',
         '-e', 'btle.pdu_type',
         '-e', 'btle.advertising_address',
         '-e', 'btle.advertising_data'],
        capture_output=True, text=True
    )
    packets = []
    for line in result.stdout.strip().split('\n'):
        if not line.strip(): continue
        parts = line.split('\t')
        packets.append({
            'pdu_type': parts[0] if len(parts) > 0 else '',
            'adv_addr': parts[1] if len(parts) > 1 else '',
            'adv_data': parts[2] if len(parts) > 2 else '',
        })
    return packets

# Usage: compare_pdus.py /tmp/ch37.pcap 37
if __name__ == '__main__':
    pcap = sys.argv[1]
    ch = int(sys.argv[2])
    pkts = extract_ubertooth_pdus(pcap, ch)
    print(f"Ubertooth captured {len(pkts)} packets on ch{ch}")
    for p in pkts[:10]:
        print(f"  type={p['pdu_type']} mac={p['adv_addr']} data={p['adv_data']}")
```

### 7.3 Technique 3: Round-Trip Self-Test (No Hardware Needed)

**Principle:** Whiten a known PDU with our Python reference script, then feed
the whitened bytes through `dewhiten()` in a host-side unit test.  If the
original PDU is recovered, dewhitening is correct.  This requires no hardware.

This is what our 23 GoogleTest cases already do (see
`components/nrf24l01plus/test/` and `docs/pipeline/logs/007-dewhiten-unit-tests.md`).

**Python — generate test vectors:**

```python
#!/usr/bin/env python3
"""Reference BLE whitening function (Dmitry Grinberg's btLeWhiten)."""

def swapbits(v):
    """Bit-reverse a single byte (nrf24::ble::swapbits equivalent)."""
    r = 0
    if v & 0x80: r |= 0x01
    if v & 0x40: r |= 0x02
    if v & 0x20: r |= 0x04
    if v & 0x10: r |= 0x08
    if v & 0x08: r |= 0x10
    if v & 0x04: r |= 0x20
    if v & 0x02: r |= 0x40
    if v & 0x01: r |= 0x80
    return r

def btLeWhitenStart(chan):
    """Compute LFSR seed from channel index."""
    return swapbits(chan) | 2

def btLeWhiten(data, whiten_coeff):
    """Whiten (or dewhiten) data with the BLE Galois LFSR.

    XOR whitening is symmetric: same function whitens and dewhitens.
    """
    result = bytearray()
    for byte_val in data:
        w = 0
        for b in range(8):
            m = 1 << b
            if whiten_coeff & 0x80:
                whiten_coeff ^= 0x11
                w ^= m
            whiten_coeff = (whiten_coeff << 1) & 0xFF
        result.append(byte_val ^ w)
    return bytes(result), whiten_coeff

# Generate test vector for channel 37
plaintext = bytes([0x40, 0x0B, 0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01,
                   0x02, 0x01, 0x06, 0x05, 0x09,
                   0x48, 0x45, 0x4C, 0x4C, 0x4F])

seed = btLeWhitenStart(37)  # = swapbits(37) | 2 = 0xA6
whitened, _ = btLeWhiten(plaintext, seed)
recovered, _ = btLeWhiten(whitened, seed)  # symmetric operation

assert recovered == plaintext, "Round-trip FAILED!"
print(f"Plaintext:  {plaintext.hex()}")
print(f"Whitened:   {whitened.hex()}")
print(f"Recovered:  {recovered.hex()}")
print("Round-trip: PASS")
```

**C++ — host-side test using the library's `dewhiten()`:**

```cpp
#include <nrf24l01plus/ble.h>
#include <cstring>
#include <cstdio>

/**
 * @brief Verify round-trip dewhitening using our library function.
 *
 * This test runs on the host (no ESP32 hardware needed).
 * Compile: g++ -std=c++17 -I components/nrf24l01plus/include
 *          -o test_dewhiten test_dewhiten.cpp ble.cpp
 *
 * @code
 *   // Channel 37 round-trip test
 *   uint8_t buf[18] = {0x40, 0x0B, 0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01,
 *                      0x02, 0x01, 0x06, 0x05, 0x09,
 *                      0x48, 0x45, 0x4C, 0x4C, 0x4F};
 *   uint8_t original[18];
 *   memcpy(original, buf, 18);
 *
 *   // Whiten (dewhiten with wrong channel, then correct channel)
 *   // Actually, since XOR whitening is symmetric:
 *   nrf24::ble::dewhiten(buf, 18, 37);  // dewhiten (=whiten since symmetric)
 *   // buf is now bit-swapped + LFSR-whitened version of original
 *   // To verify round-trip, we'd need to call dewhiten again,
 *   // but dewhiten() includes the bit-swap step.
 *   // The key test is: whiten then dewhiten gives back the original.
 * @endcode
 */
```

**Note:** The `nrf24::ble::dewhiten()` function includes a bit-swap step
internally, making it a combined operation (bit-swap + LFSR dewhiten).  For a
pure host-side test, you need to account for this: the input to `dewhiten()`
is assumed to be nRF24-format (MSbit-first) data, and the output is BLE-format
(LSbit-first) data.  For a round-trip test, use a symmetric whiten/dewhiten
function that doesn't include the bit-swap (like the Python `btLeWhiten` above),
or test with actual nRF24 hardware capturing Ubertooth-transmitted packets.

### 7.4 Technique 4: CRC-24 Validation After Dewhitening

**Principle:** After dewhitening a received packet, verify the CRC-24.  If the
CRC matches, the dewhitening is definitely correct — CRC errors would propagate
through any dewhitening mistake.

The BLE CRC-24 uses polynomial `0x00065B` with initial value `0x555555` for
advertising channels (Bluetooth Core Spec Vol 6 Part B §3.1).

**Python — CRC-24 implementation:**

```python
def crc24_ble(data, init=0x555555):
    """Compute BLE CRC-24 over the given data bytes.

    Polynomial: x^24 + x^10 + x^9 + x^6 + x^4 + x^3 + x + 1
    Encoded as 0x00065B.
    Init value: 0x555555 for advertising channel PDUs.

    @param data  Bytes over which CRC is computed (PDU header + body).
    @param init  Initial CRC value (0x555555 for advertising).
    @return      24-bit CRC value.
    """
    crc = init & 0xFFFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 0x800000:
                crc = ((crc << 1) ^ 0x00065B) & 0xFFFFFF
            else:
                crc = (crc << 1) & 0xFFFFFF
    return crc

# Example: validate dewhitened advertising PDU
pdu_header = bytes([0x00, 0x0B])  # ADV_IND, len=11
adva      = bytes([0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01])
advdata   = bytes([0x02, 0x01, 0x06, 0x05, 0x09,
                   0x48, 0x45, 0x4C, 0x4C, 0x4F])
pdu = pdu_header + adva + advdata

computed_crc = crc24_ble(pdu, 0x555555)
print(f"CRC-24 = 0x{computed_crc:06X}")
# If the dewhitened packet includes the CRC at the end,
# compare computed_crc with the last 3 bytes of the dewhitened packet.
```

**C++ — CRC-24 implementation:**

```cpp
/**
 * @brief Compute BLE CRC-24 over given data.
 *
 * Polynomial: x^24 + x^10 + x^9 + x^6 + x^4 + x^3 + x + 1
 * Encoded as 0x00065B.  Init value: 0x555555 for advertising.
 * Per Bluetooth Core Spec Vol 6 Part B §3.1.
 *
 * @code
 *   uint8_t pdu[] = {0x00, 0x0B, 0xDE, 0xAD, 0xBE, 0xEF,
 *                    0x00, 0x01, 0x02, 0x01, 0x06, 0x05,
 *                    0x09, 0x48, 0x45, 0x4C, 0x4C, 0x4F};
 *   uint32_t crc = crc24_ble(pdu, sizeof(pdu), 0x555555);
 *   // Compare crc with the 3 CRC bytes at the end of the received packet
 * @endcode
 *
 * @param data  Pointer to PDU bytes (header + body, no CRC).
 * @param len   Number of PDU bytes (not including CRC).
 * @param init  Initial CRC value (0x555555 for advertising).
 * @return      24-bit CRC value.
 */
constexpr uint32_t crc24_ble(const uint8_t *data, uint8_t len,
                              uint32_t init = 0x555555) {
    uint32_t crc = init & 0xFFFFFF;
    for (uint8_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t b = 0; b < 8; b++) {
            if (crc & 0x800000) {
                crc = ((crc << 1) ^ 0x00065B) & 0xFFFFFF;
            } else {
                crc = (crc << 1) & 0xFFFFFF;
            }
        }
    }
    return crc;
}
```

**How to use CRC-24 for validation after dewhiten:**

```cpp
/**
 * @brief Validate dewhitening by checking CRC-24 of the recovered packet.
 *
 * After calling nrf24::ble::dewhiten(), the buffer contains the plaintext
 * PDU (header + body) followed by 3 CRC bytes.  If dewhitening was correct,
 * the CRC will match.
 *
 * @code
 *   uint8_t buf[32];
 *   radio.read_payload(buf, 32);
 *   nrf24::ble::dewhiten(buf, 32, 37);
 *
 *   // PDU length is in buf[1]; total PDU bytes = buf[1] + 2 (header)
 *   uint8_t pdu_len = buf[1] + 2;  // header (2) + body
 *   // CRC follows immediately after PDU
 *   if (pdu_len + 3 <= 32) {
 *       uint32_t computed = crc24_ble(buf, pdu_len, 0x555555);
 *       uint32_t received  = buf[pdu_len]
 *                          | (buf[pdu_len+1] << 8)
 *                          | (buf[pdu_len+2] << 16);
 *       bool crc_ok = (computed == received);
 *       printf("CRC-24: %s\n", crc_ok ? "PASS" : "FAIL");
 *   }
 * @endcode
 */
```

**Note on bit order:** The CRC bytes in the dewhitened output are in
LSbit-first (BLE on-air) order.  For exact comparison with the CRC-24
computation above, the CRC bytes may need to be bit-swapped, depending on
how the PDU bytes are arranged after dewhitening.  The CRC check provides a
second independent validation beyond simple byte comparison.

### 7.5 Technique 5: Multi-Channel Consistency Test

**Principle:** Transmit the same packet on channels 37, 38, and 39.  Each
channel uses a different whitening seed (`swapbits(ch) | 2`).  If the ESP32
sniffer correctly dewhitens all three, it proves that the seed computation
and LFSR are correct for all advertising channels.

**Procedure:**

1. Transmit known AdvData and MAC on ch37 (seed = `0xA6`)
2. Switch to ch38 (seed = `0x66`) and transmit the same packet
3. Switch to ch39 (seed = `0xE6`) and transmit the same packet
4. ESP32 sniffer hops channels following the same sequence
5. If all three channels decode correctly → seed computation and LFSR are correct

**Python — transmit on all three channels:**

```python
#!/usr/bin/env python3
"""Multi-channel consistency test transmitter."""
import usb.core
import usb.util
import time

CTRL_OUT = usb.util.build_request_type(
    usb.util.CTRL_OUT, usb.util.CTRL_TYPE_VENDOR, usb.util.CTRL_RECIPIENT_DEVICE)

dev = usb.core.find(idVendor=0x1d50, idProduct=0x6002)
assert dev is not None
if dev.is_kernel_driver_active(0):
    dev.detach_kernel_driver(0)
dev.set_configuration()
usb.util.claim_interface(dev, 0)

mac = bytes.fromhex('CAFEBABE0001')
adv_data = bytes.fromhex('020106050948454C4C4F')  # "HELLO"
channels = {37: 2402, 38: 2426, 39: 2480}
seeds = {37: 0xA6, 38: 0x66, 39: 0xE6}

# Set advertising data once (same for all channels)
dev.ctrl_transfer(CTRL_OUT, 21, 0, 0, b'', 1000)  # stop
dev.ctrl_transfer(CTRL_OUT, 71, 0, 0, adv_data, 1000)  # set adv data

for ch, freq in channels.items():
    dev.ctrl_transfer(CTRL_OUT, 21, 0, 0, b'', 1000)  # stop
    time.sleep(0.1)
    dev.ctrl_transfer(CTRL_OUT, 12, freq, 0, b'', 1000)  # set channel
    dev.ctrl_transfer(CTRL_OUT, 54, 0, 0, mac, 1000)  # start TX
    print(f"  ch{ch} ({freq} MHz), seed=0x{seeds[ch]:02X}")
    time.sleep(10)  # 10 seconds per channel

dev.ctrl_transfer(CTRL_OUT, 21, 0, 0, b'', 1000)  # stop
usb.util.release_interface(dev, 0)
print("Done — if ESP32 decodes all three channels, seeds are correct.")
```

**C++ — ESP32 sniffer cycling through channels:**

```cpp
#include <nrf24l01plus/driver.h>
#include <nrf24l01plus/ble_config.h>
#include <nrf24l01plus/ble.h>

/**
 * @brief Multi-channel consistency test: receive and dewhiten on all 3 adv channels.
 *
 * @code
 *   nrf24::Driver radio(hal);
 *   nrf24::ble::configure_rx(radio);
 *
 *   const uint8_t channels[] = {37, 38, 39};
 *   for (uint8_t i = 0; i < 3; i++) {
 *       nrf24::ble::switch_channel(radio, channels[i]);
 *       radio.ce_high();
 *
 *       uint8_t buf[32] = {};
 *       // Wait for packet...
 *       if (nrf24::ble::rx_available(radio)) {
 *           radio.read_payload(buf, 32);
 *           nrf24::ble::dewhiten(buf, 32, channels[i]);
 *           // Check buf[2..7] == CAFE BABE 0001 on ALL channels
 *           // This proves correct seed: ch37→0xA6, ch38→0x66, ch39→0xE6
 *       }
 *   }
 * @endcode
 */
```

**Expected results:**

| Channel | Seed | Expected AdvA | Pass criterion |
|---------|------|---------------|----------------|
| 37 | `0xA6` | `CA FE BA BE 00 01` | Byte-exact match |
| 38 | `0x66` | `CA FE BA BE 00 01` | Byte-exact match |
| 39 | `0xE6` | `CA FE BA BE 00 01` | Byte-exact match |

If any channel fails, the seed computation or LFSR implementation has a bug
specific to that channel's seed value.

### 7.6 Technique 6: Edge Case Packets

Test dewhitening with boundary-condition advertising data payloads.  Each
edge case stresses a different aspect of the dewhitening pipeline.

| Edge case | AdvData hex | Length | What it tests |
|-----------|------------|--------|---------------|
| Empty adv data (Flags only) | `020105` | 3 bytes | Minimum-length AdvData |
| Maximum-length adv data | 31 bytes of `0xBB` | 31 bytes | 32-byte nRF24 FIFO limit |
| All-zero adv data | `000000...` (31 zeros) | 31 bytes | XOR with all-zero reveals LFSR pattern |
| All-0xFF adv data | `FFFFFFFF...` (31 bytes) | 31 bytes | XOR with all-1F reveals inverse LFSR pattern |
| Random MAC (non-public) | `02XX...` in AdvA | — | Random vs static address bit (bit 1 of MSB) |

**Python — transmit edge cases:**

```python
# Empty adv data (Flags only, 3 bytes)
adv_empty = bytes.fromhex('020105')

# Maximum-length adv data (31 bytes)
adv_max = bytes([0xFF] + [0xBB] * 30)  # Manufacturer Specific, 30 bytes of 0xBB

# All-zero adv data (31 bytes)
adv_zeros = bytes(31)  # 31 zero bytes

# All-0xFF adv data (31 bytes)
adv_ones = bytes([0xFF] * 31)

edge_cases = [
    ("Empty (Flags only)", adv_empty),
    ("Maximum (31 bytes)", adv_max),
    ("All-zero", adv_zeros),
    ("All-0xFF", adv_ones),
]

mac = bytes.fromhex('CAFEBABE0001')
for name, adv_data in edge_cases:
    print(f"  {name}: {adv_data.hex()} ({len(adv_data)} bytes)")
# Use ubertooth_btle_tx.py to transmit each one
```

**C++ — validate each edge case on the ESP32:**

```cpp
/**
 * @brief Validate edge-case advertising data after dewhitening.
 *
 * @code
 *   uint8_t buf[32];
 *   radio.read_payload(buf, 32);
 *   nrf24::ble::dewhiten(buf, 32, 37);
 *
 *   // For all-zero AdvData: bytes 8..10 should be 0x02, 0x01, 0x05
 *   // (Flags field), bytes 11..18 = 0x00 * 8
 *   // The LFSR XOR mask must not corrupt zero bytes incorrectly
 *
 *   // For all-0xFF AdvData: every byte after dewhiten should be 0xFF
 *   // only in the AdvData portion, revealing the XOR mask
 * @endcode
 */
```

**What to expect after dewhitening for each edge case:**

| Edge case | Expected dewhitened PDU[0:8] | Notes |
|-----------|------------------------------|-------|
| Empty (Flags) | `00 09 CA FE BA BE 00 01 02 01 05` | Length = 9 (6+3) |
| Maximum (31 bytes) | `00 25 CA FE BA BE 00 01 FF BB...` | Length = 37 (6+31), may exceed nRF24 32-byte limit |
| All-zero | `00 25 CA FE BA BE 00 01 00 00...` | XOR mask visible in dewhitened output |
| All-0xFF | `00 25 CA FE BA BE 00 01 FF FF...` | Same as whitened bytes when input is all-1 |

**Note on maximum-length:** The nRF24L01+ FIFO is limited to 32 bytes.  A BLE
advertising PDU can be up to 39 bytes (2 header + 37 body + 0 CRC since the
nRF24 has its own CRC).  With CRC disabled and 32-byte payload width, the nRF24
captures 32 bytes of the PDU, potentially truncating the last few bytes of a
maximum-length advertising packet.  This is a hardware limitation, not a
dewhitening bug.

---

## 8. Data Whitening Deep Dive (Ubertooth Context)

### 8.1 How the Ubertooth firmware applies whitening

In the firmware's `le_transmit()` function (`bluetooth_rxtx.c`), whitening is
applied to the PDU + CRC before transmission:

1. The LFSR is initialised with `whitening_index[channel_index]`
2. Each bit of the PDU + CRC is XORed with the LFSR output
3. The LFSR shifts LSB-first (bit 0 first), matching the BLE spec
4. The polynomial is x^7 + x^4 + 1

The firmware's `whitening_index[]` array maps the BLE channel index to the
correct initial seed value.  For example, channel 37 maps to the seed for
advertising channel index 37.

### 8.2 Seed calculation in the firmware

The firmware's `whitening_index[]` table encodes the same seed that our
`nrf24::ble::swapbits(channel_idx) | 2` formula computes:

| Channel | Firmware `whitening_index[ch]` | Our formula | Match? |
|---------|-------------------------------|------------|--------|
| 37 | Channel-dependent seed | `swapbits(37) | 2 = 0xA6` | Yes |
| 38 | Channel-dependent seed | `swapbits(38) | 2 = 0x66` | Yes |
| 39 | Channel-dependent seed | `swapbits(39) | 2 = 0xE6` | Yes |

Both the firmware and our `dewhiten()` implement the same BLE spec polynomial
(x^7 + x^4 + 1) with the same Galois LFSR form and the same channel-dependent
seed.  The only difference is the bit-swap step that we add for the nRF24's
MSbit-first byte ordering.

### 8.3 Compatibility: Ubertooth TX path vs our dewhitening path

```
Ubertooth TX path:
  Plaintext PDU → CRC-24 → Whitening (LFSR, LSB-first) → On-air (LSB-first)

nRF24 RX + dewhiten path:
  On-air (LSB-first) → nRF24 delivers MSB-first → swapbits() →
  dewhiten (LFSR, MSB-first compatible) → Plaintext PDU
```

These two paths are compatible because:
- The Ubertooth firmware applies whitening in the BLE spec's LSB-first bit order
- The nRF24 receives the same bits but delivers them MSbit-first per byte
- Our `dewhiten()` first swaps each byte to restore LSB-first order, then
  applies the Galois LFSR dewhitening with the same seed and polynomial
- The result matches the original plaintext PDU

The key insight is that **both implementations follow the same BLE spec**.  The
bit-swap step exists solely to bridge the nRF24's MSbit-first byte interface
with the BLE spec's LSB-first bit transmission order.

### 8.4 Why we must bit-swap nRF24 data before dewhitening

The nRF24L01+ SPI interface delivers each byte with the MSbit first (Product
Specification §7.3: "MSB to the left").  BLE transmits each byte LSbit first
(Bluetooth Core Spec Vol 6 Part B §1.3.1).

Without the bit-swap:
- The LFSR's first output bit will XOR with the wrong data bit
- Result: completely garbled dewhitening

With the bit-swap (as in our `nrf24::ble::dewhiten()`):
- Each byte is bit-reversed to match the BLE transmission order
- The LFSR processes bits in the correct sequence
- Result: correct dewhitening

The Ubertooth firmware does not need a bit-swap because it interfaces
directly with the CC2400 transceiver, which handles BLE's LSB-first ordering
natively.

For a complete analysis of the whitening/dewhitening pipeline and the bugs
that were found and fixed, see `docs/learning/ble-data-whitening-nrf24.md`.

---

## 9. Troubleshooting

### 9.1 "Ubertooth not found"

**Symptom:** `usb.core.USBError` or `lsusb` does not show `1d50:6002`.

**Checks:**

```bash
# Verify device is plugged in
lsusb | grep 1d50

# Check user groups (must be in 'plugdev' or have udev rules)
groups $USER

# If no udev rule, create one:
sudo tee /etc/udev/rules.d/40-ubertooth.rules << 'EOF'
SUBSYSTEM=="usb", ATTR{idVendor}=="1d50", ATTR{idProduct}=="6002", MODE="0666"
EOF
sudo udevadm control --reload-rules
sudo udevadm trigger

# Replug the device after adding udev rules
```

### 9.2 "Cannot claim USB interface"

**Symptom:** `Resource busy` or `LIBUSB_ERROR_BUSY`.

**Cause:** Another process (e.g., `ubertooth-btle`, `ubertooth-specan`) is
using the device.

**Fix:**

```bash
# Stop any running Ubertooth tools
ubertooth-util -S

# Kill any lingering processes
killall ubertooth-btle ubertooth-specan 2>/dev/null

# Wait and retry
```

### 9.3 "No packets received on ESP32"

**Symptom:** ESP32 reports no RX_DR flag, sniffer sees nothing.

**Checks:**

1. **nRF24 channel mapping:** Verify `RF_CH` matches the BLE channel
   - ch37 → `RF_CH = 2` (not 37!)
   - Use `nrf24::ble::channel_to_rf_ch()` or `nrf24::ble::switch_channel()`
2. **nRF24 address:** RX address on pipe 0 must be the bit-reversed BLE
   advertising access address (`0x6B, 0x7D, 0x91, 0x71`)
   - Use `nrf24::ble::ADV_ACCESS_ADDR` from `ble_config.h`
3. **Wiring:** CE, CSN, MOSI, MISO, SCK, VCC, GND all connected
4. **CE pin:** Must be held HIGH for RX mode (use `radio.ce_high()`)
5. **Data rate:** Must be 1 Mbps (BLE requirement)
6. **Proximity:** Ubertooth and nRF24 within ~1 meter

### 9.4 "Packets received but dewhiten output is garbage"

**Symptom:** PDU header does not contain a valid BLE advertising type
(lower 4 bits of first byte not in `0x00`–`0x06`).

**Debugging steps:**

1. **Verify channel index passed to `dewhiten()`:**
   - Must match the actual BLE channel (37, 38, or 39)
   - Not the RF_CH value, and not the Ubertooth frequency
2. **Verify bit-swap is happening inside `dewhiten()`:**
   - Our `nrf24::ble::dewhiten()` includes this step internally
   - If you call `swapbits()` separately before `dewhiten()`, you'll
     double-swap and get wrong results
3. **Verify seed formula:**
   - `swapbits(channel_idx) | 2` — not `(channel_idx & 0x3F) | 0x40`
   - See `docs/learning/ble-data-whitening-nrf24.md` for the full analysis
4. **Test with known packet:** Use Technique 1 (§7.1) to inject a known packet

**Complete debugging walkthrough:**

```bash
# Step 1: Verify Ubertooth is transmitting
# (use a second Ubertooth or a phone BLE scanner app to confirm)

# Step 2: Verify ESP32 nRF24 is receiving raw bytes
# Print the raw buf[] BEFORE calling dewhiten()
# You should see non-trivial bytes that change with channel

# Step 3: After dewhiten, check PDU header
# buf[0] lower 4 bits must be 0x00-0x06 (valid BLE PDU type)
# buf[1] must be <= 37 (valid BLE advertising PDU length)

# Step 4: If header is garbage, try dewhiten with a different channel
# If ch38 works but ch37 doesn't, the seed formula has a bug
```

### 9.5 "ubertooth-tx does nothing"

**Symptom:** Running `ubertooth-tx` prints a warning and exits.

**Cause:** `ubertooth-tx` is a **stub**.  It explicitly prints:

```
WARNING: This tool currently does nothing!
```

It was intended for classic Bluetooth symbol retransmission, not BLE.  It is
not useful for our project.

**Fix:** Use our Python script instead:

```bash
python3 tools/ubertooth_btle_tx.py -m CA:FE:BA:BE:00:01 -d 020106 -c 37 -v
```

---

## 10. Pitfalls

### 10.1 ubertooth-tx is a STUB

**Do not use `ubertooth-tx`.**  It prints "WARNING: This tool currently does
nothing!" and exits.  The upstream source code at tag `2020-12-R1` confirms this.
Use our `tools/ubertooth_btle_tx.py` script or the raw USB commands in §5.3
instead.

### 10.2 Ubertooth firmware only sends ADV_IND

In `MODE_BT_SLAVE_LE`, the firmware hardcodes PDU type `0x00` (ADV_IND).  You
cannot transmit ADV_NONCONN_IND, ADV_DIRECT_IND, SCAN_REQ, or any other PDU
type through this mode.  For ADV_NONCONN_IND, you would need
`MODE_TX_GENERIC` (`UBERTOOTH_TX_GENERIC_PACKET`, cmd=68), which requires the
host to pre-whiten and compute CRC.

### 10.3 ~100ms packet interval is firmware-controlled

The firmware sleeps ~100ms between advertising transmissions.  This cannot be
changed from the host.  At this rate, you get approximately 10 packets per
second.  For sniffer testing, this is adequate, but it is far slower than a
real BLE phone advertising at 20ms intervals.

### 10.4 Firmware handles CRC and whitening automatically

In `MODE_BT_SLAVE_LE`, the firmware:
- Computes CRC-24 with init=0x555555
- Applies data whitening with the channel's LFSR seed
- Inserts the access address

You **cannot** send pre-whitened or pre-CRC'd data.  The host provides only
the MAC address and AdvData bytes as plaintext.  If you need to control
whitening or CRC (e.g., to test edge cases), use `MODE_TX_GENERIC` instead.

### 10.5 nRF24 channel mapping is non-obvious

The nRF24 `RF_CH` register value does **not** equal the BLE channel index:

| BLE ch | nRF24 RF_CH | Why? |
|--------|-------------|------|
| 37 | 2 | `2400 + 2 = 2402 MHz` |
| 38 | 26 | `2400 + 26 = 2426 MHz` |
| 39 | 80 | `2400 + 80 = 2480 MHz` |

Always use `nrf24::ble::channel_to_rf_ch()` or `nrf24::ble::ADV_CHANNELS[]`
from the library.  Never hardcode raw `RF_CH` values.

### 10.6 nRF24 access address must be bit-reversed

The BLE advertising access address is `0x8E89BED6`.  The nRF24 expects
addresses MSbit-first, but BLE transmits LSbit-first.  Each byte must be
bit-reversed:

| Original byte | Bit-reversed |
|--------------|-------------|
| `0x8E` | `0x71` |
| `0x89` | `0x91` |
| `0xBE` | `0x7D` |
| `0xD6` | `0x6B` |

Use `nrf24::ble::ADV_ACCESS_ADDR` from `ble_config.h` (which stores the
bit-reversed form: `{0x6B, 0x7D, 0x91, 0x71}`).

### 10.7 Different BLE devices use different TxPower levels

The Ubertooth transmits at approximately 0 dBm.  Real phones and BLE
peripherals vary widely:

| Device class | Typical TxPower |
|-------------|----------------|
| Ubertooth One | ~0 dBm |
| Smartphone | 0 to +10 dBm |
| BLE peripheral (key fob) | -4 to +4 dBm |
| AirTag | -8 to 0 dBm |

If the ESP32 sniffer works with the Ubertooth but misses real phone packets,
it may be a range/sensitivity issue, not a dewhitening bug.

---

## 11. References

- **Ubertooth GitHub repository** — firmware, host tools, hardware designs
  [https://github.com/greatscottgadgets/ubertooth](https://github.com/greatscottgadgets/ubertooth)
  *(verified 2026-06-06 — tag `2020-12-R1` available)*

- **Ubertooth firmware bluetooth_rxtx.c** — `bt_slave_le()`, `tx_generic()`,
  `le_transmit()` functions
  [https://github.com/greatscottgadgets/ubertooth/blob/2020-12-R1/firmware/bluetooth_rxtx/bluetooth_rxtx.c](https://github.com/greatscottgadgets/ubertooth/blob/2020-12-R1/firmware/bluetooth_rxtx/bluetooth_rxtx.c)
  *(verified 2026-06-06 — 2719 lines)*

- **Ubertooth API ubertooth_interface.h** — USB command codes
  [https://github.com/greatscottgadgets/ubertooth/blob/2020-12-R1/host/libubertooth/src/ubertooth_interface.h](https://github.com/greatscottgadgets/ubertooth/blob/2020-12-R1/host/libubertooth/src/ubertooth_interface.h)
  *(verified 2026-06-06)*

- **Ubertooth host ubertooth_control.h** — `cmd_btle_slave()`,
  `cmd_le_set_adv_data()` function declarations
  [https://github.com/greatscottgadgets/ubertooth/blob/2020-12-R1/host/libubertooth/src/ubertooth_control.h](https://github.com/greatscottgadgets/ubertooth/blob/2020-12-R1/host/libubertooth/src/ubertooth_control.h)
  *(verified 2026-06-06)*

- **PyUSB documentation** — Python USB access library
  [https://pyusb.github.io/pyusb/](https://pyusb.github.io/pyusb/)
  *(verified 2026-06-06)*

- **libusb documentation** — Cross-platform C USB library
  [https://libusb.info/](https://libusb.info/)
  *(verified 2026-06-06)*

- **Bluetooth Core Specification** — Vol 6, Part B
  [https://www.bluetooth.com/specifications/specs/core-specification/](https://www.bluetooth.com/specifications/specs/core-specification/)
  *(verified 2026-06-06 — Core Spec 6.3 available)*
  - §1.4.1 — Channel mapping (40 channels: 37 data, 3 advertising)
  - §2.3 — Advertising PDU format (header + body)
  - §3.1 — CRC-24 (polynomial, init value)
  - §3.2 — Data whitening (polynomial x^7+x^4+1, seed derivation)

- **Great Scott Gadgets Ubertooth One product page** (retired product)
  [https://greatscottgadgets.com/ubertoothone/](https://greatscottgadgets.com/ubertoothone/)
  *(verified 2026-06-06)*

- **Ubertooth documentation on Read the Docs**
  [https://ubertooth.readthedocs.io/en/latest/](https://ubertooth.readthedocs.io/en/latest/)
  *(verified 2026-06-06)*

- **Dmitry Grinberg, "Bit-banging Bluetooth Low Energy"** — original nRF24 BLE
  reference implementation (btLeWhiten, swapbits, seed formula)
  [http://dmitry.gr/?r=05.Projects&proj=11.%20Bluetooth%20LE%20fakery](http://dmitry.gr/?r=05.Projects&proj=11.%20Bluetooth%20LE%20fakery)
  *(verified 2026-06-06)*

- **Project-internal references:**
  - `docs/learning/ble-data-whitening-nrf24.md` — dewhitening bugs and correct implementation
  - `docs/pipeline/logs/006-ubertooth-dewhiten-test-plan.md` — detailed test plan
  - `docs/pipeline/logs/009-ubertooth-tx-fix.md` — enabling BLE TX with our script
  - `tools/ubertooth_btle_tx.py` — our custom BLE advertising transmitter
  - `components/nrf24l01plus/include/nrf24l01plus/ble.h` — `dewhiten()`, `swapbits()`
  - `components/nrf24l01plus/include/nrf24l01plus/ble_config.h` — channel mapping, `configure_rx()`