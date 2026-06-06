# NRF24L01+ BLE Packet Crafting

## Overview

The NRF24L01+ operates on the same 2.4 GHz ISM band as BLE. Because BLE uses GFSK modulation at 1 Mbps — the exact same modulation the NRF24 supports — you can craft raw BLE advertising packets and transmit them from the NRF24. A phone's BLE scanner will detect these packets as if they came from a real BLE device.

This is educational: understanding packet structure at the bit level.

---

## 1. Why This Works

### Physical layer compatibility

| Parameter | BLE | NRF24L01+ |
|---|---|---|
| Modulation | GFSK | GFSK |
| Data rate | 1 Mbps | 1 Mbps (configurable) |
| Frequency | 2402–2480 MHz | 2400–2525 MHz |
| Preamble | 1 byte (0xAA or 0x55) | 1 byte (auto) |
| Access Address | 4 bytes | 3–5 bytes (configurable) |

The NRF24 can be configured to transmit a raw byte sequence that matches the BLE advertising packet format.

### BLE advertising channel mapping

| BLE Channel | Frequency | NRF24 Channel |
|---|---|---|
| 37 | 2402 MHz | 2 |
| 38 | 2426 MHz | 26 |
| 39 | 2480 MHz | 80 |

---

## 2. BLE Advertising Packet Structure

```
┌──────────┬───────────────────┬────────────────────────────────────────────┬─────────┐
│ Preamble │  Access Address   │              PDU                            │  CRC    │
│ (1 byte) │  (4 bytes)        │  (2-39 bytes)                              │ (3 bytes)│
└──────────┴───────────────────┴────────────────────────────────────────────┴─────────┘
     ↑              ↑                        ↑                                    ↑
  Auto by NRF    0x8E89BED6           Header + AdvA + AdvData             Calculated
  (0xAA for      (fixed for all        (we build this)                   (BLE CRC24)
   BLE adv)       adv channels)
```

### Preamble (1 byte)
- BLE: `0xAA` for advertising (LSB of access address is 0)
- NRF24: Automatically prepends preamble based on first TX byte — we configure to match

### Access Address (4 bytes)
- BLE advertising: always `0x8E89BED6`
- We set this as the NRF24 TX address (but byte-reversed due to NRF24 byte order)

### PDU (Protocol Data Unit)

```
┌──────────────────────────────────────────────────┐
│ Header (2 bytes) │ AdvA (6 bytes) │ AdvData (0-31 bytes) │
└──────────────────────────────────────────────────┘
```

#### PDU Header (2 bytes)

| Bits | Field | Value for ADV_NONCONN_IND |
|---|---|---|
| [3:0] | PDU Type | 0x2 (ADV_NONCONN_IND) |
| [4] | RFU | 0 |
| [5] | ChSel | 0 |
| [6] | TxAdd | 1 (random address) |
| [7] | RxAdd | 0 |
| [13:8] | Length | Total bytes after header (AdvA + AdvData) |

So header byte 0 = `0x42` (PDU type 0010 + TxAdd=1 → 0100 0010)
Header byte 1 = length of payload following the header

#### AdvA — Advertiser Address (6 bytes)
- A random or public Bluetooth address
- For random static address: two MSBits must be `11` (i.e., last byte ≥ 0xC0)
- Example: `{0x11, 0x22, 0x33, 0x44, 0x55, 0xC6}` (transmitted LSB first)

#### AdvData — Advertising Data (0–31 bytes)
- LTV (Length-Type-Value) formatted
- Example for name "Hi":
  ```
  0x03,   // Length: 3 bytes following
  0x09,   // Type: Complete Local Name
  'H', 'i'
  ```

---

## 3. BLE CRC24 Calculation

BLE uses a 24-bit CRC with:
- Polynomial: `x^24 + x^10 + x^9 + x^6 + x^4 + x^3 + x + 1` = `0x00065B`
- Initial value: `0x555555` (for advertising channels)
- Calculated over the PDU (header + payload), NOT over the preamble or access address

```c
// BLE CRC24 calculation
uint32_t ble_crc24(const uint8_t *data, size_t len) {
    uint32_t crc = 0x555555;  // Init value for advertising

    for (size_t i = 0; i < len; i++) {
        uint8_t byte = data[i];
        for (int bit = 0; bit < 8; bit++) {
            if ((crc >> 23) ^ (byte & 1)) {
                crc = (crc << 1) ^ 0x00065B;
            } else {
                crc = crc << 1;
            }
            byte >>= 1;
            crc &= 0xFFFFFF;
        }
    }
    return crc;
}
```

---

## 4. BLE Bit Order: Whitening

BLE applies a **data whitening** LFSR to the entire packet (PDU + CRC) to reduce DC bias. The whitening sequence depends on the channel number.

### Whitening LFSR

- Polynomial: `x^7 + x^4 + 1`
- Algorithm: **Galois LFSR** (left-shifting, checks bit 7, XOR `0x11`)
- Seed: `swapbits(channel_number) | 2`

The seed derivation and Galois LFSR algorithm are from Dmitry Grinberg's
original "Bit-banging Bluetooth Low Energy" reference implementation,
which is the origin of all known nRF24 BLE code (omriiluz, sandeepmistry,
MarcelMG, etc. all derive from it).

`swapbits(channel)` bit-reverses the channel number so it occupies the
lower 6 bits of the left-shifting LFSR state.  Bit 1 is set (`| 2`) to
place position 1 in the 7-bit Galois register.

```c
// Galois LFSR whitening/dewhitening — symmetric (same function both ways)
// Reference: Dmitry Grinberg, "Bit-banging Bluetooth Low Energy"
//   http://dmitry.gr/index.php?r=05.Projects&proj=11.%20Bluetooth%20LE%20fakery
void ble_whiten(uint8_t *data, size_t len, uint8_t channel) {
    uint8_t lfsr = swapbits(channel) | 2;  // Galois LFSR seed

    for (size_t i = 0; i < len; i++) {
        for (uint8_t m = 1; m; m <<= 1) {
            if (lfsr & 0x80) {
                lfsr ^= 0x11;   // polynomial taps: x^4 (bit 4) and 1 (bit 0)
                data[i] ^= m;
            }
            lfsr <<= 1;
        }
    }
}
```

> **Important:** For nRF24L01+ RX, the full dewhitening pipeline is:
> 1. Bit-swap each received byte (nRF24 MSbit-first → BLE LSbit-first)
> 2. Apply the Galois LFSR dewhitening above
>
> Forgetting the bit-swap is the most common bug in nRF24 BLE code.
> See our library function `nrf24::ble::dewhiten()` which encapsulates both steps.

---

## 5. NRF24L01+ Configuration for BLE TX

To transmit BLE-compatible packets, the NRF24 must be configured to act as a "dumb" transmitter:

```c
void nrf24_ble_mode_setup(uint8_t ble_channel) {
    // Map BLE channel to NRF24 RF channel
    uint8_t nrf_channel;
    switch (ble_channel) {
        case 37: nrf_channel = 2;  break;  // 2402 MHz
        case 38: nrf_channel = 26; break;  // 2426 MHz
        case 39: nrf_channel = 80; break;  // 2480 MHz
        default: nrf_channel = 2;  break;
    }

    // 1. Power down
    nrf24_write_reg(0x00, 0x00);  // CONFIG: power down, no CRC

    // 2. Disable auto-ACK (raw packet, no ShockBurst)
    nrf24_write_reg(0x01, 0x00);  // EN_AA: all disabled

    // 3. Disable RX pipes
    nrf24_write_reg(0x02, 0x00);  // EN_RXADDR: none

    // 4. Set address width to 4 bytes (BLE access address is 4 bytes)
    nrf24_write_reg(0x03, 0x02);  // SETUP_AW: 4 bytes

    // 5. Disable retransmit
    nrf24_write_reg(0x04, 0x00);  // SETUP_RETR: disabled

    // 6. Set RF channel
    nrf24_write_reg(0x05, nrf_channel);

    // 7. Set 1 Mbps, 0 dBm, NO LNA gain
    nrf24_write_reg(0x06, 0x06);  // RF_SETUP: 1Mbps, 0dBm

    // 8. Set TX address to BLE access address (byte-reversed!)
    //    BLE access address: 0x8E89BED6
    //    NRF24 sends LSByte first: 0xD6, 0xBE, 0x89, 0x8E
    uint8_t ble_access_addr[] = {0xD6, 0xBE, 0x89, 0x8E};
    nrf24_write_multi(0x10, ble_access_addr, 4);  // TX_ADDR

    // 9. Disable NRF24 CRC (we calculate BLE CRC ourselves)
    //    CONFIG: PWR_UP=1, PRIM_RX=0, EN_CRC=0
    nrf24_write_reg(0x00, 0x02);  // Power up, TX mode, no CRC
    vTaskDelay(pdMS_TO_TICKS(2));
}
```

### Key configuration points

| Feature | Normal NRF24 | BLE mode |
|---|---|---|
| CRC | Hardware (1 or 2 byte) | **Disabled** (we add BLE CRC24 manually) |
| Auto-ACK | Enabled | **Disabled** |
| Address width | 5 bytes | **4 bytes** (BLE access address) |
| Data rate | Any | **1 Mbps** (must match BLE) |
| Retransmit | Enabled | **Disabled** |
| ShockBurst | On | **Off** (raw packet mode) |

---

## 6. Building and Transmitting a BLE Packet

```c
void nrf24_send_ble_adv(const char *name, uint8_t ble_channel) {
    // 1. Build PDU
    uint8_t pdu[39];  // Max BLE ADV PDU
    size_t pdu_len = 0;

    // PDU Header
    pdu[0] = 0x42;     // ADV_NONCONN_IND (0x02) + TxAdd=1 (random addr)
    // pdu[1] = length (filled later)
    pdu_len = 2;

    // AdvA: Random static address (6 bytes, LSB first)
    pdu[pdu_len++] = 0x11;
    pdu[pdu_len++] = 0x22;
    pdu[pdu_len++] = 0x33;
    pdu[pdu_len++] = 0x44;
    pdu[pdu_len++] = 0x55;
    pdu[pdu_len++] = 0xC6;  // MSByte with bits [7:6] = 11 (random static)

    // AdvData: Flags
    pdu[pdu_len++] = 0x02;  // Len
    pdu[pdu_len++] = 0x01;  // Type: Flags
    pdu[pdu_len++] = 0x06;  // General Discoverable + BR/EDR not supported

    // AdvData: Complete Local Name
    size_t name_len = strlen(name);
    if (name_len > 20) name_len = 20;  // Limit to fit in 31 bytes
    pdu[pdu_len++] = name_len + 1;     // Len field
    pdu[pdu_len++] = 0x09;             // Type: Complete Local Name
    memcpy(&pdu[pdu_len], name, name_len);
    pdu_len += name_len;

    // Fill in PDU length (bytes after header: AdvA + AdvData)
    pdu[1] = pdu_len - 2;

    // 2. Calculate BLE CRC24 over PDU
    uint32_t crc = ble_crc24(pdu, pdu_len);

    // 3. Append CRC (3 bytes, LSB first)
    pdu[pdu_len++] = (crc >> 0) & 0xFF;
    pdu[pdu_len++] = (crc >> 8) & 0xFF;
    pdu[pdu_len++] = (crc >> 16) & 0xFF;

    // 4. Apply BLE whitening over PDU + CRC
    ble_whiten(pdu, pdu_len, ble_channel);

    // 5. Transmit via NRF24
    nrf24_ble_mode_setup(ble_channel);
    nrf24_write_payload(pdu, pdu_len);  // W_TX_PAYLOAD

    // Pulse CE to transmit
    nrf24_ce_high();
    esp_rom_delay_us(15);
    nrf24_ce_low();

    // Wait for TX complete
    vTaskDelay(pdMS_TO_TICKS(1));
    nrf24_write_reg(0x07, 0x70);  // Clear status flags
}
```

### Transmit on all 3 advertising channels

```c
void broadcast_ble_name(const char *name) {
    nrf24_send_ble_adv(name, 37);  // 2402 MHz
    nrf24_send_ble_adv(name, 38);  // 2426 MHz
    nrf24_send_ble_adv(name, 39);  // 2480 MHz
}
```

---

## 7. Byte Order Gotchas

BLE and NRF24 both use **LSByte first** for multi-byte fields, but the **bit order within each byte differs**:

| Field | BLE spec order | NRF24 TX order | Notes |
|---|---|---|---|
| Access Address | 0x8E89BED6 | `{0xD6, 0xBE, 0x89, 0x8E}` | LSByte first on air |
| AdvA (MAC) | `AA:BB:CC:DD:EE:FF` | `{0xFF, 0xEE, 0xDD, 0xCC, 0xBB, 0xAA}` | LSByte first |
| CRC24 | Result of polynomial | `{crc[7:0], crc[15:8], crc[23:16]}` | LSByte first |
| Bits within byte | **LSBit first** | **MSBit first** | **Mismatch! Requires bit-swap** |

The nRF24L01+ SPI interface delivers data MSbit-first within each byte
(nRF24L01+ Product Specification §7).  BLE transmits LSbit-first within
each byte (Bluetooth Core Spec Vol 6 Part B §1.3.1).  This means every
byte read from the nRF24 RX FIFO **must be bit-reversed** before BLE
protocol processing (dewhitening, CRC, PDU decoding).  Conversely, every
byte destined for nRF24 TX **must be bit-reversed** after BLE processing.

Use `nrf24::ble::swapbits()` or let `dewhiten()` handle it automatically.

---

## 8. Important: Single Packet, Not Flooding

**This code sends ONE advertising packet per call.** A normal BLE device advertises once per interval (typically 100 ms–1 s). Our code mimics this:

```c
void app_main(void) {
    // Init SPI + NRF24...

    while (1) {
        broadcast_ble_name("NRF24_Hi");
        vTaskDelay(pdMS_TO_TICKS(1000));  // Once per second — normal rate
    }
}
```

**What's legal:** Sending a single advertisement at normal intervals.
**What's illegal:** Sending thousands per second to flood BLE scanners (DoS attack).

---

## 9. Limitations of NRF24 BLE

| Limitation | Reason |
|---|---|
| No scan responses | Would require RX capability with BLE timing |
| No connections | NRF24 can't implement BLE link layer timing |
| Fixed channel only | No adaptive frequency hopping |
| No encryption | NRF24 has no BLE security stack |
| Timing may be off | NRF24 TX timing differs from BLE controller |
| CRC errors possible | Whitening/CRC calculation must be exact |

This is a **proof-of-concept** — the ESP32's real BLE stack (from the previous doc) is always better for production use.

---

## 10. Verification

**Success criteria:**
1. Open nRF Connect (or similar BLE scanner) on your phone
2. A device with your custom name appears (e.g., "NRF24_Hi")
3. It shows as non-connectable
4. The address type shows "Random"
5. The RSSI is reasonable

**Debug tips:**
- If nothing appears: check CRC calculation and whitening (most common bugs)
- If name is garbled: check byte order in AdvData
- If intermittent: ensure you're transmitting on all 3 advertising channels
- Use Wireshark + nRF Sniffer to capture and decode raw packets

---

## References

- NRF24L01+ Product Specification v1.0 (Nordic Semiconductor) — local copy: `docs/datasheets/nRF24L01P_PS_v1.0.pdf` — Chapter 7: Data and Control Interface
- Online: [NRF24L01+ Product Specification v1.0](https://docs.nordicsemi.com/r/bundle/pdf_ps_nrf24l01p/page/pdf/nrf24/nrf24l01p_ps_1.0/ps_nrf24l01p.html) *(verified 2026-06-05)*
- [Bluetooth Core Specification v5.x](https://www.bluetooth.com/specifications/specs/core-specification/) — Vol 6, Part B, Section 2.3 (Advertising Channel PDU)
- [Dmitry Grinberg: "Bit-banging Bluetooth Low Energy"](http://dmitry.gr/index.php?r=05.Projects&proj=11.%20Bluetooth%20LE%20fakery) — Original research on BLE-from-NRF24
- [ESP-IDF SPI Master Driver](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/spi_master.html) *(verified 2026-06-04)*
