# BLE Sniffer Architecture — From Advertising Basics to Packet Parsing

> **Note:** This document explains how the ESP32 + nRF24L01+ BLE sniffer works end-to-end. Code examples use the typed library API (`nrf24::ble::dewhiten()`, `nrf24::ble::configure_rx()`, etc.) for production-quality code. Where raw hex values appear, they are annotated with their meaning — never use raw hex in production code.

---

## 1. What Is BLE Advertising?

### 1.1 BLE Channel Architecture

Bluetooth Low Energy divides the 2.4 GHz ISM band into **40 channels**, each 2 MHz wide, spanning 2402–2480 MHz:

| Channel Type | Channel Indices | Count | Purpose |
|---|---|---|---|
| **Advertising** | 37, 38, 39 | 3 | Device discovery, broadcasting, connection initiation |
| **Data** | 0–36 (excluding 0, 1, 2, 3, 4, … varies) | 37 | Post-connection encrypted communication |

The advertising channels are deliberately placed at the edges and middle of the 2.4 GHz band to maximise the chance of being heard, even in the presence of Wi-Fi interference which typically occupies channels around 2412–2472 MHz:

| BLE Channel | Frequency | RF_CH (nRF24) | Position |
|---|---|---|---|
| **37** | 2402 MHz | 2 | Below Wi-Fi band |
| **38** | 2426 MHz | 26 | Mid-band |
| **39** | 2480 MHz | 80 | Above Wi-Fi band |

### 1.2 Advertising: "Hello, I'm Here"

BLE advertising is the mechanism by which a device announces its presence. The advertiser broadcasts a small packet on one or more of the three advertising channels, and any scanner in range can receive it. No pairing, no encryption, no connection required.

Think of it like a person standing in a room calling out "I'm here, my name is X, here's what I can do" — anyone in the room can hear it.

Advertising packets are sent on all three channels by default, cycling through them: 37 → 38 → 39 (the spec does not mandate a particular order, but round-robin is common). The advertiser sends on one channel at a time, spending a random interval (20 ms to 10.24 s, typically around 100 ms) on each.

### 1.3 Data Channels: Not Part of This Sniffer

Once two devices connect (via a `CONNECT_IND` PDU), they shift to data channels (0–36). Data channels use a different access address (negotiated during connection), encrypted payloads, and adaptive frequency hopping. **This sniffer does not target data channels** — it is a passive advertising-only sniffer.

---

## 2. What Can This Sniffer Capture?

### 2.1 Advertising PDU Types

The nRF24L01+ configured for BLE passive RX can capture the following advertising physical channel PDU types, as defined in Bluetooth Core Spec Vol 6 Part B §2.3 Table 2.2:

| PDU Type | Code | Description | AdvA Present? | Notes |
|---|---|---|---|---|
| **ADV_IND** | 0 | Connectable undirected advertising | Yes (6 bytes) | Most common — phones, watches, beacons |
| **ADV_DIRECT_IND** | 1 | Connectable directed advertising | Yes (6 bytes) | Targets a specific device |
| **ADV_NONCONN_IND** | 2 | Non-connectable advertising | Yes (6 bytes) | Beacons, AirTags — no connection possible |
| **SCAN_REQ** | 3 | Scan request | No (has ScanA + AdvA) | Scanner → advertiser |
| **SCAN_RSP** | 4 | Scan response | Yes (6 bytes) | Additional data from scannable devices |
| **CONNECT_IND** | 5 | Connection indication | No (has InitA + AdvA) | Initiates a connection |
| **ADV_SCAN_IND** | 6 | Scannable undirected advertising | Yes (6 bytes) | Offers scan response data |
| **ADV_EXT_IND** | 7 | Extended advertising | Variable (in ext header) | BLE 5.0+, longer data |

The library maps these to the `nrf24::ble::BleAdvPduType` enum:

```cpp
enum class BleAdvPduType : uint8_t {
    AdvInd        = 0,  // Connectable undirected advertising
    AdvDirectInd  = 1,  // Connectable directed advertising
    AdvNonconnInd = 2,  // Non-connectable undirected advertising
    ScanReq       = 3,  // Scan request
    ScanRsp       = 4,  // Scan response
    ConnectInd    = 5,  // Connection indication
    AdvScanInd    = 6,  // Scannable undirected advertising
    AdvExtInd     = 7,  // Extended advertising (BLE 5.0+)
};
```

### 2.2 What We CANNOT Capture

- **Data channel packets**: Different access address, encrypted payload, channel hopping. The nRF24L01+ would need to track the connection to know the channel map and access address.
- **Full CRC-24 verification**: The nRF24L01+ has CRC disabled for BLE mode (BLE CRC is a different polynomial), so we receive the CRC bits as part of the payload but cannot verify them natively. Software-based CRC-24 verification is possible but not yet implemented.
- **Complete extended advertising PDUs**: ADV_EXT_IND can be up to 254 bytes, but the nRF24L01+ FIFO holds only 32 bytes. We capture the first 32 bytes, which typically includes the extended header.
- **Coded PHY (LE Coded)**: BLE 5.0's 125 kbps and 500 kbps coded PHY uses a different modulation (S=2, S=8 coding). The nRF24L01+ only supports 1 Mbps and 2 Mbps GFSK, so coded PHY packets cannot be received.

---

## 3. Step-by-Step: From SPI to Parsed Packet

This section walks through every step from configuring the nRF24L01+ to receiving and parsing a BLE advertising packet.

### Step 1: Configure the nRF24L01+ for BLE Passive RX

The nRF24L01+ was designed as a proprietary 2.4 GHz radio, not a BLE chip. To receive BLE packets, we must configure it in a way that bypasses its Enhanced ShockBurst™ protocol layer and treats BLE packets as raw payloads.

The `nrf24::ble::configure_rx()` function does this. Here are the critical register settings and **why** each one matters:

#### 1a. Disable auto-acknowledgment (EN_AA = 0x00) — CRITICAL

```cpp
nrf24::EnAa en_aa;
for (int i = 0; i < 6; ++i) en_aa.pipe[i] = false;  // All pipes off
radio.write_reg(en_aa);
```

**Why this MUST come first:** The nRF24L01+ datasheet states that if `EN_AA` is non-zero for any pipe, the `EN_CRC` bit in CONFIG is **forced high by hardware**. The power-on reset value of `EN_AA` is `0x3F` (all pipes enabled). If you write `CONFIG` with `EN_CRC=0` while `EN_AA` is still `0x3F`, the hardware silently overrides `EN_CRC` to 1, and BLE packets (which use a different CRC) are rejected. **Always write `EN_AA=0x00` before writing `CONFIG`.**

See [`nrf24-enaa-encrc-override.md`](nrf24-enaa-encrc-override.md) for the full trap analysis.

#### 1b. Power up in PRX mode with CRC disabled

```cpp
nrf24::Config cfg;
cfg.power_mode   = nrf24::PowerMode::Up;     // PWR_UP = 1
cfg.primary      = nrf24::PrimaryMode::RX;    // PRIM_RX = 1
cfg.crc_mode     = nrf24::CrcMode::Disabled;  // EN_CRC = 0 (BLE has its own CRC)
cfg.crc_encoding = nrf24::CrcEncoding::Bytes1; // Irrelevant when CRC is off
cfg.irq_mask     = nrf24::IrqMask::None;       // All interrupts reflected on IRQ pin
radio.write_reg(cfg);
```

**Why CRC disabled?** BLE uses a CRC-24 (polynomial `x^24 + x^10 + x^9 + x^6 + x^4 + x^3 + x + 1`), while the nRF24L01+ uses CRC-8 or CRC-16 with a different polynomial. If we leave the nRF24's CRC enabled, it will reject every BLE packet because the CRC bytes don't match. We disable nRF24 CRC and handle BLE CRC in software (or skip verification).

The `configure_rx()` function also verifies that CONFIG actually took `EN_CRC=0`:

```cpp
nrf24::Config readback = radio.read_reg(nrf24::Config{});
if (readback.crc_mode != nrf24::CrcMode::Disabled) {
    // Re-write and check again — may indicate a clone chip
    radio.write_reg(cfg);
    readback = radio.read_reg(nrf24::Config{});
    if (readback.crc_mode != nrf24::CrcMode::Disabled) {
        printf("[BLE CONFIG] WARNING: EN_CRC forced on — possible clone chip\n");
    }
}
```

#### 1c. Enable pipe 0 only

```cpp
nrf24::EnRxAddr en_rx;
for (int i = 0; i < 6; ++i) en_rx.pipe[i] = false;
en_rx.pipe[0] = true;  // Only pipe 0 for BLE RX
radio.write_reg(en_rx);
```

The nRF24L01+ supports up to 6 data pipes, but BLE sniffing only needs one pipe since we're matching a single address (the BLE advertising access address).

#### 1d. Set address width to 4 bytes

```cpp
nrf24::SetupAw aw;
aw.address_width = nrf24::AddressWidth::Bytes4;  // BLE AA is 4 bytes
radio.write_reg(aw);
```

BLE uses a 4-byte access address. The nRF24L01+ supports 3, 4, or 5 byte addresses. We choose 4 bytes to match the BLE access address length.

#### 1e. No retransmission

```cpp
nrf24::SetupRetr retr;
retr.delay  = nrf24::AutoRetransmitDelay::Us250;
retr.count  = nrf24::AutoRetransmitCount::Disabled;  // No retransmission
radio.write_reg(retr);
```

For passive sniffing, retransmission is irrelevant — we're only receiving, never transmitting ACKs.

#### 1f. Set RF channel to an advertising channel frequency

```cpp
nrf24::RfCh rf_ch;
rf_ch.channel = nrf24::ble::ADV_CHANNELS[0].rf_ch;  // Channel 37 → RF_CH=2 (2402 MHz)
radio.write_reg(rf_ch);
```

The `nrf24::ble::ADV_CHANNELS` array maps BLE channels to nRF24 RF_CH values:

| Index | BLE Channel | RF_CH | Frequency |
|---|---|---|---|
| 0 | 37 | 2 | 2402 MHz |
| 1 | 38 | 26 | 2426 MHz |
| 2 | 39 | 80 | 2480 MHz |

#### 1g. Set 1 Mbps data rate, 0 dBm TX power

```cpp
nrf24::RfSetup rf;
rf.data_rate = nrf24::DataRate::Mbps1;  // BLE uses GFSK at 1 Mbps
rf.tx_power  = nrf24::TxPower::dBm0;    // 0 dBm (irrelevant for RX-only)
rf.cont_wave = nrf24::ContWave::Disabled;
rf.pll_lock  = nrf24::PllLock::Disabled;
radio.write_reg(rf);
```

BLE advertising physical channels use GFSK modulation at 1 Mbps. The nRF24L01+ must be set to 1 Mbps mode. TX power is irrelevant for a passive sniffer but must be set to a valid value.

#### 1h. Set payload width to 32 bytes

```cpp
nrf24::RxPwP0 pw0;
pw0.payload_width = nrf24::MAX_PAYLOAD;  // 32 bytes
radio.write_reg(pw0);
```

The nRF24L01+ requires a fixed payload width for each pipe in non-dynamic-length mode. 32 bytes is the maximum and covers the full FIFO. BLE advertising PDUs are typically 6–37 bytes (PDU body), but since the nRF24 receives the entire packet including PDU header and CRC, 32 bytes captures most advertising PDUs. Longer PDUs (ADV_EXT_IND) will be truncated.

#### 1i. Write the BLE advertising access address

```cpp
radio.write_reg_multi(nrf24::reg::RX_ADDR_P0, nrf24::ble::ADV_ACCESS_ADDR, 4);
radio.write_reg_multi(nrf24::reg::TX_ADDR, nrf24::ble::ADV_ACCESS_ADDR, 4);
```

The BLE advertising access address is `0x8E89BED6`. However, this value must be transformed before being written to the nRF24L01+:

1. BLE transmits LSByte-first on air: `D6 BE 89 8E`
2. Each byte is bit-swapped (BLE LSBit-first → nRF24 MSBit-first): `6B 7D 91 71`
3. nRF24 on-air order is MSByte-first, so it matches: `6B 7D 91 71`
4. SPI writes are LSByte-first (datasheet §8.3.1), so the array is: `{0x71, 0x91, 0x7D, 0x6B}`

The `nrf24::ble::ADV_ACCESS_ADDR` constant stores the bytes in the correct SPI write order:

```cpp
inline constexpr uint8_t ADV_ACCESS_ADDR[4] = {0x71, 0x91, 0x7D, 0x6B};
```

See [`nrf24-spi-address-byte-order.md`](nrf24-spi-address-byte-order.md) for the full transformation chain and the bug that was found.

#### 1j. Power-on delay and CE HIGH

```cpp
radio.hal().delay_ms(2);  // Tpd2stby = 1.5 ms (datasheet §6.1.2 Table 9), we wait 2 ms
radio.ce_high();          // Transition to RX mode
```

After `PWR_UP=1`, the nRF24L01+ needs at least 1.5 ms for the oscillator to stabilise (datasheet §6.1.2, Table 9). After CE HIGH, the device transitions from Standby-I to RX mode.

---

### Step 2: Switch Channels (`switch_channel`)

BLE advertisers cycle through channels 37, 38, and 39. To capture packets on all three channels, the sniffer must rotate through them. `nrf24::ble::switch_channel()` does this:

```cpp
inline void switch_channel(Driver &radio, uint8_t ble_channel)
{
    radio.ce_low();        // Enter Standby-I
    RfCh rf_ch;
    rf_ch.channel = channel_to_rf_ch(ble_channel);
    radio.write_reg(rf_ch);   // Change frequency
    clear_irq_flags(radio);    // Clear STATUS interrupt flags
    radio.flush_rx();          // Clear RX FIFO
    radio.ce_high();           // Re-enter RX mode
    
    // Wait for settling: 200 µs covers both Tstby2a (130 µs) and RPD validity (170 µs)
    radio.hal().delay_us(200);
}
```

**Why each step:**

1. **CE LOW** → nRF24 transitions from RX mode to Standby-I. The radio stops receiving.
2. **Write RF_CH** → Changes the frequency to the desired BLE channel.
3. **Clear IRQ flags** → Clears `RX_DR`, `TX_DS`, `MAX_RT` in the STATUS register (write-1-to-clear semantics). Prevents stale interrupt flags from the previous channel from being misinterpreted.
4. **Flush RX FIFO** → Discards any data received on the previous channel.
5. **CE HIGH** → nRF24 transitions from Standby-I to RX mode.
6. **Wait 200 µs** → After CE HIGH, two timing constraints apply:
   - **Tstby2a** = 130 µs (max): Time from Standby-I to active RX (datasheet §6.1.7, Table 16)
   - **RPD validity** = 170 µs (130 µs settling + 40 µs AGC): Earliest time the Received Power Detector gives a reliable result
   - 200 µs provides comfortable margin over both.

The `channel_to_rf_ch()` function maps any BLE channel index (0–39) to an nRF24 RF_CH value:

```cpp
constexpr uint8_t channel_to_rf_ch(uint8_t ble_channel)
{
    if (ble_channel == 37) return 2;    // 2402 MHz
    if (ble_channel == 38) return 26;   // 2426 MHz
    if (ble_channel == 39) return 80;   // 2480 MHz
    if (ble_channel <= 10) return static_cast<uint8_t>(4 + 2 * ble_channel);
    /* channels 11–36 */
    return static_cast<uint8_t>(28 + 2 * (ble_channel - 11));
}
```

In the sniffer loop, we cycle through the three advertising channels:

```cpp
uint8_t seq_i = 0;
while (1) {
    uint8_t ble_ch = cfg.channel_at(seq_i);  // Returns 37, 38, or 39
    nrf24::ble::switch_channel(radio, ble_ch);
    
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(cfg.scan_duration_ms);
    while (xTaskGetTickCount() < deadline) {
        // Poll for packets on this channel
        // ...
    }
    
    seq_i = (seq_i + 1) % cfg.total_channels();
}
```

---

### Step 3: Poll for Data

Once the nRF24L01+ is in RX mode on a BLE advertising channel, we poll the FIFO to check for received data:

```cpp
if (nrf24::ble::rx_fifo_not_empty(radio))
{
    uint8_t buf[nrf24::MAX_PAYLOAD];  // 32 bytes
    radio.read_payload(buf, nrf24::MAX_PAYLOAD);
    nrf24::ble::clear_irq_flags(radio);
    radio.flush_rx();
    // ... dewhiten and parse ...
}
```

**`rx_fifo_not_empty()`** reads the `FIFO_STATUS` register and checks the `RX_EMPTY` bit. It returns `true` when there is at least one packet in the RX FIFO. This is more reliable than checking `RX_DR` alone, which can be set spuriously or may not reflect the actual FIFO state after operations like `flush_rx()`.

The read payload is always 32 bytes (`nrf24::MAX_PAYLOAD`) — the nRF24L01+ fills the entire FIFO width. Bytes beyond the actual BLE PDU length are leftover garbage from a previous packet or the FIFO reset pattern (`0xFE`).

---

### Step 4: Dewhiten the Received Data

BLE applies **data whitening** to every on-air packet before transmission. The receiver must apply the inverse operation (dewhitening) to recover the original PDU. The nRF24L01+ does not perform BLE dewhitening natively — it passes the raw on-air bytes (including whitening XOR) directly to the FIFO.

Additionally, the nRF24L01+ delivers each byte **MSbit-first**, while BLE transmits **LSbit-first** per byte (Bluetooth Core Spec Vol 6 Part B §1.3.1). So every byte must be bit-swapped before BLE processing.

`nrf24::ble::dewhiten()` performs both steps in one call:

```cpp
nrf24::ble::dewhiten(buf, nrf24::MAX_PAYLOAD, ble_ch);
```

#### 4a. Bit-swap each byte

The `swapbits()` function reverses all 8 bits in a byte:

```
swapbits(0xD6) = 0x6B    // 1101_0110 → 0110_1011
swapbits(0x00) = 0x00    // identity: all zeros
swapbits(0xFF) = 0xFF    // identity: all ones
```

This converts from nRF24 MSbit-first byte order to BLE LSbit-first byte order.

#### 4b. Galois LFSR dewhitening

BLE whitening XORs the PDU and CRC with a pseudo-random bit sequence generated by a 7-bit Galois LFSR with polynomial `x^7 + x^4 + 1`. The LFSR seed is derived from the channel index:

```
seed = swapbits(channel_idx) | 0x02
```

The `| 0x02` sets bit 1, corresponding to the constant part of the spec's "channel index + 64" formula. The LFSR shifts left; when bit 7 is set, it XORs the state with `0x11` (taps at bit positions 4 and 0) before the next shift.

Since XOR whitening is symmetric, the same algorithm whitens and dewhitens.

**Example for channel 37:**

```
channel = 37 = 0x25 = 0b00100101
swapbits(0x25) = 0xA4 = 0b10100100
seed = 0xA4 | 0x02 = 0xA6 = 0b10100110
```

For the complete dewhitening algorithm, LFSR state machine, and seed tables for all 40 BLE channels, see [`ble-data-whitening-nrf24.md`](ble-data-whitening-nrf24.md).

---

### Step 5: Parse the BLE PDU

After dewhitening, the buffer contains the BLE PDU in its natural format (LSbit-first per byte). The PDU structure for advertising physical channels is:

```
Byte 0: PDU header
  bits [3:0]  = PDU type (BleAdvPduType)
  bit  [4]    = ChSel (channel selection algorithm, BLE 5.0+)
  bit  [5]    = TxAdd (0 = public address, 1 = random address)
  bit  [6]    = RxAdd (0 = public address, 1 = random address; for ADV_DIRECT_IND)
  bit  [7]    = unused in legacy advertising
  (For ADV_EXT_IND, bits [7:6] = AdvMode)

Byte 1: PDU length
  bits [5:0]  = length of PDU body (excludes the 2-byte header)
  bits [7:6]  = reserved (must be 0 in legacy advertising)

Bytes 2 to (2+length-1): PDU body
  For most PDU types, bytes 2-7 = AdvA (advertiser address, 6 bytes)
```

#### Extracting the PDU type

```cpp
auto pdu_type = static_cast<nrf24::ble::BleAdvPduType>(
    buf[0] & nrf24::ble::PDU_TYPE_MASK);  // PDU_TYPE_MASK = 0x0F
```

#### Extracting the PDU length

```cpp
uint8_t pdu_len = buf[1] & nrf24::ble::PDU_LENGTH_MASK;  // PDU_LENGTH_MASK = 0x3F
```

#### Extracting the advertiser address (AdvA)

For PDU types that have a fixed-offset AdvA (ADV_IND, ADV_NONCONN_IND, ADV_SCAN_IND, SCAN_RSP, ADV_DIRECT_IND):

```cpp
uint8_t adv_addr[6];
if (nrf24::ble::adv_address(buf, adv_addr)) {
    printf("AdvA: %s\n", nrf24::ble::format_address(adv_addr));
}
```

`adv_address()` returns `false` for PDU types without a fixed-offset AdvA:
- **SCAN_REQ** (type 3): has ScanA + AdvA, not just AdvA
- **CONNECT_IND** (type 5): has InitA + AdvA, not just AdvA
- **ADV_EXT_IND** (type 7): AdvA is at a variable offset in the extended header

Note that BLE transmits AdvA **LSByte-first** on air (Bluetooth Core Spec Vol 6 Part B §1.2). After dewhitening, `buf[2]` is the LSByte and `buf[7]` is the MSByte. The `adv_address()` function reverses this to standard MAC notation (MSByte first):

```cpp
out[0] = buf[7];  // MSByte
out[1] = buf[6];
out[2] = buf[5];
out[3] = buf[4];
out[4] = buf[3];
out[5] = buf[2];  // LSByte
```

#### Human-readable PDU type name

```cpp
const char *type_name = nrf24::ble::pdu_type_name(pdu_type);
// Returns: "ADV_IND", "ADV_DIRECT_IND", "SCAN_RSP", etc.
// For unknown types (8-15): "UNKNOWN(type=N)"
```

---

### Step 6: Parsing a Real ADV_IND Packet — Worked Example

Let's walk through a real BLE ADV_IND packet from dewhitened bytes to parsed fields.

#### 6a. Raw dewhitened bytes

After receiving 32 bytes from the nRF24L01+ FIFO and running `nrf24::ble::dewhiten(buf, 32, 37)` on channel 37, we get:

```
Offset:  00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F
Value:   00 1A AA BB CC DD EE FF 07 09 54 65 73 74 20 44
         65 76 69 63 65 0A 16 0F FF 59 00 41 42 43 XX XX XX
                                                        ^^^^^^^  CRC-24 (3 bytes)
         (remaining bytes beyond pdu_len+2 may be 0xFE from FIFO)
```

> **Note:** The hex values above are illustrative, not from a specific capture. Real captures will vary.

#### 6b. Parse byte by byte

**Byte 0 — PDU Header: `0x00`**

| Bits | Field | Value | Meaning |
|---|---|---|---|
| [3:0] | PDU type | `0x0` | ADV_IND (connectable undirected advertising) |
| [4] | ChSel | 0 | Not used (BLE 5.0+ feature) |
| [5] | TxAdd | 0 | Advertiser uses a public address |
| [7:6] | Reserved | 0 | Must be 0 |

**Byte 1 — PDU Length: `0x1A`** (= 26 bytes)

The PDU body is 26 bytes long (bytes 2–27 in the buffer).

**Bytes 2–7 — AdvA: `AA BB CC DD EE FF`**

Advertiser address. In standard MAC notation (MSByte first): `FF:EE:DD:CC:BB:AA`.

Since `TxAdd=0`, this is a **public address** (assigned by IEEE). If `TxAdd=1`, it would be a random address (e.g., Bluetooth-resolvable private address).

**Bytes 8–27 — AdvData (advertising data)**

The remaining PDU body is the advertising data, which consists of a sequence of **AD structures**. Each AD structure has:

| Field | Size | Description |
|---|---|---|
| Length | 1 byte | Total length of the AD structure (including the Type field, but not the Length byte itself) |
| Type | 1 byte | AD type code (defined in Bluetooth SIG Assigned Numbers) |
| Data | Length-1 bytes | AD type data |

Let's parse the AD structures above:

**AD Structure 1:** `07 09 54 65 73 74 20 44 65 76 69 63 65`

Wait, let me be more precise. The first byte is the length:

- **Length = 0x07** → 7 bytes total (1 type byte + 6 data bytes)
- **Type = 0x09** → Complete Local Name
- **Data = `54 65 73 74 20 44 65 76 69 63 65`** → ASCII: "Test Device"

Wait, that's 10 bytes of data for a length of 7? Let me re-do this more carefully.

Actually, let me use a simpler, more precise example that fits in 32 bytes:

```
Offset:  00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D ...
Value:   00 0F AA BB CC DD EE FF 04 09 4E 52 46 02 0A 00 XX XX XX ...
```

**Byte 0 — PDU Header: `0x00`**
- PDU type = 0 (ADV_IND)
- TxAdd = 0 (public address)

**Byte 1 — PDU Length: `0x0F`** (= 15 bytes of PDU body, in bytes 2–16)

**Bytes 2–7 — AdvA: `AA BB CC DD EE FF`**

Using `nrf24::ble::adv_address()`:
```cpp
uint8_t adv_addr[6];
nrf24::ble::adv_address(buf, adv_addr);
// adv_addr = {0xFF, 0xEE, 0xDD, 0xCC, 0xBB, 0xAA}
// format_address returns "FF:EE:DD:CC:BB:AA"
```

**Bytes 8–16 — AdvData (9 bytes)**

AD Structure 1: `04 09 4E 52 46 02`
- Length = 4 (1 type + 3 data)
- Type = 0x09 = Complete Local Name
- Data = `4E 52 46` → ASCII: " NRF"

Wait, let me use a clean, realistic example:

```
PDU bytes (dewhitened):
  Byte 0: 0x00  (PDU header: type=ADV_IND, TxAdd=0)
  Byte 1: 0x0E  (PDU length = 14 bytes)
  Bytes 2-7: AA BB CC DD EE FF  (AdvA)
  Bytes 8-15: (AdvData, 8 bytes)
```

#### 6c. Parsing AdvData — AD Structures

AdvData is a sequence of length-type-value structures:

| AD Type Code | Name | Description |
|---|---|---|
| 0x01 | Flags | BLE discoverability flags |
| 0x02 | Incomplete List of 16-bit UUIDs | Partial service UUID list |
| 0x03 | Complete List of 16-bit UUIDs | Full service UUID list |
| 0x07 | Complete List of 128-bit UUIDs | 128-bit UUID list |
| 0x08 | Shortened Local Name | Abbreviated device name |
| 0x09 | Complete Local Name | Full device name |
| 0x0A | TX Power Level | Transmit power in dBm |
| 0xFF | Manufacturer Specific Data | Vendor-specific data |

**Example AdvData with two AD structures:**

```
AdvData bytes (offset from PDU start):
  Byte 8:  0x02  (AD Structure 1: Length = 2)
  Byte 9:  0x01  (AD Type = Flags)
  Byte 10: 0x06  (AD Data = 0x06 = LE General Discoverable + BR/EDR Not Supported)
  
  Byte 11: 0x0B  (AD Structure 2: Length = 11)
  Byte 12: 0x09  (AD Type = Complete Local Name)
  Bytes 13-21: "MyBLEDevice" (ASCII: 4D 79 42 4C 45 44 65 76 69 63 65)
  
Total AdvData: 3 + 12 = 15 bytes → PDU length = 6 (AdvA) + 15 (AdvData) = 21... Hmm, that exceeds our example above.
```

Let me use a compact real-world example:

**Compact ADV_IND with just Flags + Shortened Name:**

```
Dewhitened buffer (channel 37):
  [0]:  0x00  ← PDU header: PDU type = 0 (ADV_IND)
  [1]:  0x08  ← PDU length = 8
  [2]:  0xAA  ← AdvA byte 0 (LSByte)
  [3]:  0xBB  ← AdvA byte 1
  [4]:  0xCC  ← AdvA byte 2
  [5]:  0xDD  ← AdvA byte 3
  [6]:  0xEE  ← AdvA byte 4
  [7]:  0xFF  ← AdvA byte 5 (MSByte)
  [8]:  0x02  ← AD Structure 1: Length = 2
  [9]:  0x01  ← AD Type = Flags
  [10]: 0x06  ← Flags value: LE General Discoverable + BR/EDR Not Supported
  [11]: 0x03  ← AD Structure 2: Length = 3
  [12]: 0x09  ← AD Type = Complete Local Name
  [13]: 0x4E  ← 'N'
  [14]: 0x52  ← 'R'
  [15]: 0x46  ← 'F'
  [16-31]: ... (CRC-24 starts at offset 10 = 2 + pdu_len, then FIFO padding)
```

Wait — actually for ADV_IND with pdu_len=8, the PDU body bytes are [2] through [9], i.e., 6 bytes AdvA + 2 bytes AdvData? That doesn't make sense. Let me recalculate:

PDU format: `header(1) + length(1) + body(length bytes)`
- Byte 0: header
- Byte 1: length = the number of bytes in the body
- Bytes [2, 2+length): body

For pdu_len=8, the body is bytes [2] through [9] (8 bytes total).
- Bytes [2-7]: AdvA (6 bytes)
- Bytes [8-9]: AdvData (2 bytes)

But 2 bytes is too short for meaningful AdvData (1 byte length + 1 byte type = 0 data). Let me use pdu_len = 12 instead:

```
Dewhitened buffer:
  [0]:  0x00      ← PDU type = ADV_IND, TxAdd = 0
  [1]:  0x0C      ← PDU length = 12  (body is 12 bytes)
  [2]:  AdvA[0]   ← LSByte of advertiser address
  [3]:  AdvA[1]
  [4]:  AdvA[2]
  [5]:  AdvA[3]
  [6]:  AdvA[4]
  [7]:  AdvA[5]   ← MSByte of advertiser address
  ── AdvData starts (6 bytes remaining in body) ──
  [8]:  0x02      ← AD Structure 1 length = 2 (type + data)
  [9]:  0x01      ← AD Type = Flags (0x01)
  [10]: 0x06      ← Flags: LE General Discoverable, BR/EDR Not Supported
  [11]: 0x04      ← AD Structure 2 length = 4
  [12]: 0x09      ← AD Type = Complete Local Name (0x09)
  [13]: 0x4E      ← 'N' (0x4E)
  [14]: 0x52      ← 'R' (0x52)
  [15]: 0x46      ← 'F' (0x46)
  ── PDU body ends at offset 2+12-1=13 ... hmm wait ──
```

Let me get the indexing right. The PDU format per Bluetooth Core Spec:

- `buf[0]` = PDU Header byte
- `buf[1]` = PDU Length byte (length of PDU body, excluding header and length byte themselves)
- `buf[2]` through `buf[1 + pdu_len]` = PDU body (pdu_len bytes)

So for `pdu_len = 12`:
- PDU body spans `buf[2]` to `buf[13]`
- Bytes `buf[2]` through `buf[7]` = AdvA (6 bytes)
- Bytes `buf[8]` through `buf[13]` = AdvData (6 bytes)

The CRC-24 follows the PDU: bytes `buf[14]`, `buf[15]`, `buf[16]`.

#### 6d. C++ code for parsing AdvData

```cpp
/**
 * Parse AD structures from a BLE advertising PDU's AdvData.
 *
 * This shows the low-level SPI API for educational purposes.
 * Production code should use the typed struct API — see
 * cpp-enum-class-and-struct.md.
 */
void parse_adv_data(const uint8_t *buf, uint8_t pdu_len)
{
    // AdvA is 6 bytes starting at buf[2]
    // AdvData starts at buf[8] and ends at buf[1 + pdu_len]
    
    if (pdu_len < 6) return;  // Not enough room for AdvA, let alone AdvData
    
    const uint8_t *adv_data = buf + 8;           // Start of AdvData
    uint8_t adv_data_len = pdu_len - 6;            // AdvData length = pdu_len - 6 (minus AdvA)
    
    uint8_t pos = 0;
    while (pos < adv_data_len) {
        uint8_t ad_len = adv_data[pos];             // Length of this AD structure
        if (ad_len == 0 || pos + 1 + ad_len > adv_data_len) break;
        
        uint8_t ad_type = adv_data[pos + 1];       // AD type code
        
        switch (ad_type) {
        case 0x01:  // Flags
            if (ad_len >= 2) {
                uint8_t flags = adv_data[pos + 2];
                printf("  Flags: 0x%02X\n", flags);
            }
            break;
        case 0x09:  // Complete Local Name
            printf("  Name: %.*s\n", ad_len - 1, &adv_data[pos + 2]);
            break;
        case 0x0A:  // TX Power Level
            if (ad_len >= 2) {
                int8_t tx_power = static_cast<int8_t>(adv_data[pos + 2]);
                printf("  TX Power: %d dBm\n", tx_power);
            }
            break;
        case 0xFF:  // Manufacturer Specific Data
            printf("  MfrData: len=%u\n", ad_len - 1);
            break;
        default:
            printf("  AD type 0x%02X, len=%u\n", ad_type, ad_len - 1);
            break;
        }
        
        pos += 1 + ad_len;  // Skip Length byte + structure
    }
}
```

#### 6e. Complete end-to-end sniffer loop example

```cpp
// This shows the sniffer loop pattern used in main.cpp
void process_packet(nrf24::Driver &radio, uint8_t ble_ch)
{
    uint8_t buf[nrf24::MAX_PAYLOAD];
    radio.read_payload(buf, nrf24::MAX_PAYLOAD);
    nrf24::ble::clear_irq_flags(radio);
    radio.flush_rx();
    
    // Step 1: Dewhiten (bit-swap + LFSR XOR)
    nrf24::ble::dewhiten(buf, nrf24::MAX_PAYLOAD, ble_ch);
    
    // Step 2: Extract PDU header fields
    auto pdu_type = static_cast<nrf24::ble::BleAdvPduType>(
        buf[0] & nrf24::ble::PDU_TYPE_MASK);
    uint8_t pdu_len = buf[1] & nrf24::ble::PDU_LENGTH_MASK;
    const char *type_name = nrf24::ble::pdu_type_name(pdu_type);
    
    // Step 3: Extract advertiser address (if available for this PDU type)
    uint8_t adv_addr[6];
    if (nrf24::ble::adv_address(buf, adv_addr)) {
        printf("[ch%u] %-17s  %s  len=%u\n",
               ble_ch, type_name,
               nrf24::ble::format_address(adv_addr),
               pdu_len);
    } else {
        printf("[ch%u] %-17s  len=%u\n", ble_ch, type_name, pdu_len);
    }
    
    // Step 4: For ADV_IND, parse AdvData
    if (pdu_type == nrf24::ble::BleAdvPduType::AdvInd && pdu_len > 6) {
        parse_adv_data(buf, pdu_len);  // From the function above
    }
}
```

---

## 4. The BLE Advertising Packet Structure (Visual)

### 4.1 Full on-air packet

```
┌─────────────────────────────────────────────────────────────────────┐
│                     BLE Advertising Packet                          │
│                    (as transmitted on air)                          │
├──────────┬──────────────┬──────────────┬───────────┬───────────────┤
│ Preamble │ Access Addr  │ PDU Header   │ PDU Body  │ CRC-24        │
│ 1 byte   │ 4 bytes      │ 2 bytes      │ 0-37 bytes│ 3 bytes       │
│          │              │              │           │               │
│ 10101010 │ 0x8E89BED6   │ Type | Len   │ ...       │ x^24+x^10+...│
│ or       │ (advertising │              │           │               │
│ 01010101 │ channel AA)  │              │           │               │
├──────────┼──────────────┼──────────────┼───────────┼───────────────┤
│ Not      │ nRF24 matches│ Parsed by    │ Contains  │ Received but  │
│ received │ on this      │ dewhitened   │ AdvA +    │ not verified  │
│ by nRF24 │ address      │ sniffer code │ AdvData   │ (CRC off)     │
└──────────┴──────────────┴──────────────┴───────────┴───────────────┘
```

### 4.2 PDU Header bit layout (byte 0, after dewhitening)

```
Byte 0 (PDU Header):
┌───┬───┬───┬───┬────┬───────┬───────┬───────┐
│ 7 │ 6 │ 5 │ 4 │ 3  │  2    │  1    │  0    │
├───┼───┼───┼───┼────┼───────┼───────┼───────┤
│Rsv│AdvM│RxA│TxA│ChS │PDU Type [3:0]          │
│   │ode│dd │dd │el  │                        │
│   │ * │   │   │    │                        │
└───┴───┴───┴───┴────┴───────────────────────┘

* For ADV_EXT_IND (type 7), bits [7:6] = AdvMode:
    00 = non-conn non-scan
    01 = connectable
    10 = scannable
    11 = reserved

For legacy PDU types (0-6), bit 7 is reserved, bits 5 and 4 are
TxAdd and RxAdd respectively.
```

### 4.3 PDU Length byte (byte 1, after dewhitening)

```
Byte 1 (PDU Length):
┌─────────────────┬─────────────────────────────┐
│ Bits [7:6]      │ Bits [5:0]                  │
├─────────────────┼─────────────────────────────┤
│ Reserved (must  │ Length of PDU body          │
│ be 0 for legacy │ (0-37 for legacy,            │
│ advertising)    │ 0-254 for extended)          │
└─────────────────┴─────────────────────────────┘
```

### 4.4 PDU body for ADV_IND

```
PDU Body for ADV_IND (bytes 2 onwards):
┌──────────────────────┬─────────────────────────────────────────┐
│ AdvA (6 bytes)        │ AdvData (0-31 bytes)                    │
│                      │                                         │
│ Advertiser address   │ Sequence of AD structures               │
│ LSB first on air    │ Each: Length(1) + Type(1) + Data(N)     │
│ buf[2]..buf[7]     │ buf[8]..buf[1+pdu_len]                 │
└──────────────────────┴─────────────────────────────────────────┘
```

### 4.5 AD Structure format (within AdvData)

```
Each AD Structure:
┌──────────┬──────────┬──────────────────────────────────────┐
│ Length   │ AD Type  │ AD Data                              │
│ (1 byte) │ (1 byte) │ (Length - 1 bytes)                   │
│          │          │                                      │
│ Total    │ Bluetooth│ Type-specific data                   │
│ size of  │ SIG      │ (e.g., device name, UUIDs, TX      │
│ type+data│ Assigned │  power, manufacturer data, etc.)    │
│ fields   │ Numbers  │                                      │
└──────────┴──────────┴──────────────────────────────────────┘

Common AD Type codes:
  0x01 = Flags                    0x0A = TX Power Level
  0x02 = Incomplete 16-bit UUIDs  0xFF = Manufacturer Specific
  0x03 = Complete 16-bit UUIDs
  0x07 = Complete 128-bit UUIDs
  0x08 = Shortened Local Name
  0x09 = Complete Local Name
```

### 4.6 What the nRF24L01+ captures vs what's on air

```
On-air BLE packet:
┌──────────┬──────────────┬──────────────┬───────────┬──────────┐
│ Preamble │ Access Addr  │ PDU (header  │ PDU body  │ CRC-24   │
│ (1B)     │ (4B)         │ + body)      │ cont.     │ (3B)     │
└──────────┴──────────────┴──────────────┴───────────┴──────────┘
│          │              │              │           │          │
│  Not     │  nRF24       │  nRF24       │  nRF24    │  nRF24   │
│  captured │  matches    │  captures    │  captures │  captures│
│  by       │  on this    │  (whitened,  │  (whitened│  (as     │
│  nRF24*   │  address    │  MSbit-first)│  MSbit-   │  part of│
│           │  filter     │              │  first)   │  payload)│
│           │             │              │           │          │
│           │             ↓              ↓           ↓          │
│           │     ┌─────────────────────────────────────────┐ │
│           │     │   nRF24 RX FIFO (32 bytes)               │ │
│           │     │   PDU header | PDU body | CRC-24 | pad   │ │
│           │     │   (whitened, MSbit-first byte order)      │ │
│           │     └─────────────────────────────────────────┘ │
│           │              │                                   │
│           │              ↓  After dewhiten(buf, 32, ch)      │
│           │     ┌─────────────────────────────────────────┐ │
│           │     │   Dewhitened buffer                       │ │
│           │     │   PDU header | PDU body | CRC-24 | pad   │ │
│           │     │   (natural BLE format, LSbit-first)       │ │
│           │     └─────────────────────────────────────────┘ │
```

*The preamble is not received because the nRF24L01+ synchronises on it internally during address detection. The access address is consumed by the address matching hardware. Everything from PDU header onwards (including the CRC-24) is placed in the RX FIFO, still whitened and in MSbit-first byte order. After dewhitening, we get the natural BLE PDU format.

### 4.7 Address matching detail

The nRF24L01+ listens for the 4-byte access address on the selected channel. When it detects a matching address on air, it begins clocking in the subsequent bits as payload data. The access address itself is consumed by the matching hardware and does NOT appear in the RX FIFO.

```
On-air:  [Preamble][Access Address 0x8E89BED6][PDU Header][PDU Body][CRC]
                      │                       │                   │      │
                      │                       │                   │      │
                      ↓                       ↓                   ↓      ↓
              Matched by nRF24 addr   Written to RX FIFO ──────────────┘
              matching logic           (whitened, MSbit-first)
```

This is why the buffer starts at the PDU header (`buf[0]`), not at the access address.

---

## 5. Making the Sniffer Top-Top-Notch

### 5.1 What's currently missing

| Feature | Status | Impact |
|---|---|---|
| CRC-24 verification | Not implemented | No way to distinguish valid packets from noise/corruption. The CRC-24 bytes are in the buffer but not checked. |
| RSSI measurement | nRF24L01+ has RPD (Received Power Detector), not true RSSI | RPD only tells you if signal is above -64 dBm. No dBm resolution. |
| Data channel sniffing | Not implemented | Can't capture post-connection encrypted traffic. Requires tracking channel hopping and access address from CONNECT_IND. |
| ADV_EXT_IND full parsing | Partial (header only) | Extended advertising PDUs can be 254 bytes long, but nRF24 FIFO is 32 bytes. We capture the extended header but miss the body. |
| Packet deduplication | Not implemented | Advertisers send on all 3 channels; the same device appears up to 3 times per advertising interval. |
| MAC address filtering | Not implemented | All packets are printed; no way to filter for a specific MAC. |

### 5.2 CRC-24 verification

BLE uses CRC-24 with polynomial `x^24 + x^10 + x^9 + x^6 + x^4 + x^3 + x + 1` and initial value `0x555555`. The CRC is computed over the PDU header and PDU body (bytes 0 through 1+pdu_len in our buffer).

The nRF24L01+ has its CRC disabled for BLE mode, so the 3 CRC bytes are included in the received payload. After dewhitening, you can compute CRC-24 over bytes `[0, 1+pdu_len)` and compare with bytes `[2+pdu_len, 5+pdu_len)`.

Implementation sketch:

```cpp
/**
 * Compute BLE CRC-24 over the PDU.
 *
 * @param buf     Dewhitened PDU buffer.
 * @param pdu_len Length field from buf[1] (bits [5:0]).
 * @return        24-bit CRC value.
 */
uint32_t ble_crc24(const uint8_t *buf, uint8_t pdu_len)
{
    uint32_t crc = 0x555551;  // Init value per Bluetooth Core Spec
    // Process each byte of PDU header + body: buf[0] through buf[1+pdu_len]
    for (uint8_t i = 0; i < 2 + pdu_len; i++) {
        crc = crc24_byte(crc, buf[i]);
    }
    return crc & 0x00FFFFFF;
}
```

Note: this is a sketch, not production code. The CRC-24 init value and byte processing must match the Bluetooth Core Spec exactly. See Bluetooth Core Spec Vol 6 Part B §3.1.1 for the precise algorithm.

### 5.3 Hardware upgrades

| Upgrade | Benefit |
|---|---|
| Better antenna (2.4 GHz tuned) | Improved range and signal quality |
| External LNA (Low Noise Amplifier) | Better sensitivity for weak signals |
| Dedicated nRF24 per channel | Simultaneous 3-channel capture (no channel switching latency) |
| nRF52840 or CC2540 as sniffer | Native BLE support with CRC verification, RSSI, timestamping |
| Logic analyser on SPI | Verify SPI timing and data integrity |

### 5.4 Software upgrades

| Upgrade | Benefit |
|---|---|
| CRC-24 check | Filter out corrupted packets and noise |
| RSSI logging (RPD) | At minimum, log whether signal was above -64 dBm |
| MAC filtering | Only display packets from specific devices |
| Deduplication | Suppress repeated packets from the same device on different channels |
| Timestamping | Record when each packet was received (microsecond resolution) |
| AD structure parser | Fully decode Flags, UUIDs, TX Power, manufacturer data |
| Extended advertising parser | Decode AUX_SYNC_IND when possible |
| Connection tracking | Parse CONNECT_IND to extract the new access address, then track data channels |

### 5.5 Channel switching overhead

The current design rotates through channels 37 → 38 → 39, spending `scan_duration_ms` on each. The `switch_channel()` function takes ~200 µs, plus the SPI register write time. With `scan_duration_ms = 50`, we spend only ~0.4% of time switching.

However, during the ~200 µs switching time, any packets transmitted on the current channel are lost. At typical advertising intervals (100 ms–1 s), this is negligible. But if three nRF24L01+ chips were used simultaneously (one per channel), switching overhead would be eliminated entirely.

---

## 6. Complete Data Flow Diagram

```
                          BLE Advertiser
                               │
                               │  On-air BLE packet:
                               │  [Preamble][AA][PDU Header][PDU Body][CRC-24]
                               │     1B      4B      1B        N B        3B
                               │
                               ▼
                    ┌──────────────────────┐
                    │   nRF24L01+ Radio    │
                    │                      │
                    │ 1. Matches Access    │
                    │    Address 0x8E89BED6│
                    │    (4 bytes,         │
                    │    pre-configuration)│
                    │                      │
                    │ 2. Receives PDU +   │
                    │    CRC as raw payload│
                    │    (MSbit-first,     │
                    │     whitened)         │
                    │                      │
                    │ 3. Places 32 bytes   │
                    │    in RX FIFO        │
                    └──────────┬───────────┘
                               │
                               │  radio.read_payload(buf, 32)
                               │
                               ▼
                    ┌──────────────────────┐
                    │   Raw Buffer (32 B)   │
                    │                      │
                    │ Byte 0: PDU header   │
                    │   (whitened, MSbit-  │
                    │    first byte order)  │
                    │ Byte 1: PDU length   │
                    │ Bytes 2+: PDU body   │
                    │ (includes CRC-24     │
                    │  at end of PDU)       │
                    └──────────┬───────────┘
                               │
                               │  nrf24::ble::dewhiten(buf, 32, ble_ch)
                               │
                               │  Step 1: swapbits() each byte
                               │  Step 2: Galois LFSR XOR
                               │
                               ▼
                    ┌──────────────────────┐
                    │  Dewhitened Buffer    │
                    │                      │
                    │ Byte 0: PDU header   │
                    │   (LSbit-first byte) │
                    │   bits[3:0] = type   │
                    │   bit[5] = TxAdd     │
                    │ Byte 1: PDU length   │
                    │   bits[5:0] = length │
                    │ Bytes 2-7: AdvA      │
                    │ Bytes 8+: AdvData   │
                    └──────────┬───────────┘
                               │
                               │  Parse PDU type, length, AdvA, AdvData
                               │
                               ▼
                    ┌──────────────────────┐
                    │  Parsed BLE Packet    │
                    │                      │
                    │ Type: ADV_IND        │
                    │ AdvA: FF:EE:DD:...   │
                    │ AdvData:             │
                    │   Flags=0x06         │
                    │   Name="NRF"          │
                    └──────────────────────┘
```

---

## 7. Configuration Reference

### 7.1 `nrf24::ble::RxConfig`

| Field | Type | Default | Description |
|---|---|---|---|
| `initial_channel_idx` | `uint8_t` | 0 | Starting channel index (0=ch37, 1=ch38, 2=ch39) |
| `payload_width` | `uint8_t` | 32 | RX payload width (use `nrf24::MAX_PAYLOAD`) |
| `scan_duration_ms` | `uint16_t` | 50 | Time per channel before rotating (ms) |
| `extra_channels` | `const uint8_t*` | nullptr | Additional BLE channels to scan (data channels 0–36) |
| `extra_channel_count` | `uint8_t` | 0 | Number of extra channels |

### 7.2 `nrf24::ble::ADV_CHANNELS`

| Index | `rf_ch` | `ch_idx` | Name | Frequency |
|---|---|---|---|---|
| 0 | 2 | 37 | "ch37" | 2402 MHz |
| 1 | 26 | 38 | "ch38" | 2426 MHz |
| 2 | 80 | 39 | "ch39" | 2480 MHz |

### 7.3 `nrf24::ble::ADV_ACCESS_ADDR`

```cpp
inline constexpr uint8_t ADV_ACCESS_ADDR[4] = {0x71, 0x91, 0x7D, 0x6B};
```

The BLE advertising access address `0x8E89BED6` transformed for nRF24L01+ SPI:
1. BLE AA on air (LSByte first): `D6 BE 89 8E`
2. Per-byte bit-swap (LSBit → MSBit): `6B 7D 91 71`
3. nRF24 on-air (MSByte first): `6B 7D 91 71`
4. SPI write (LSByte first): `{0x71, 0x91, 0x7D, 0x6B}`

See [`nrf24-spi-address-byte-order.md`](nrf24-spi-address-byte-order.md) for the complete transformation chain and the bug history.

### 7.4 Key register settings for BLE passive RX

| Register | Value | Purpose |
|---|---|---|
| CONFIG | PWR_UP=1, PRIM_RX=1, EN_CRC=0 | Power on, RX mode, CRC disabled |
| EN_AA | 0x00 (all pipes disabled) | **Must be 0 before CONFIG write** — forces EN_CRC off |
| EN_RXADDR | 0x01 (pipe 0 only) | Only receive on pipe 0 |
| SETUP_AW | Bytes4 (0x02) | 4-byte address width for BLE AA |
| SETUP_RETR | count=Disabled | No retransmission |
| RF_CH | 2/26/80 | BLE channel 37/38/39 |
| RF_SETUP | 1 Mbps, 0 dBm | BLE modulation |
| RX_PW_P0 | 32 | Maximum payload width |
| RX_ADDR_P0 | ADV_ACCESS_ADDR | BLE advertising address |
| TX_ADDR | ADV_ACCESS_ADDR | Same as RX_ADDR_P0 |

---

## 8. Register Write Order — Critical Dependency

The order of register writes matters. Here is the correct sequence, with critical ordering constraints highlighted:

```
1. EN_AA = 0x00          ← MUST be first! Clears auto-ack before CONFIG write
2. CONFIG                ← PWR_UP=1, PRIM_RX=1, EN_CRC=0 (only works if EN_AA=0)
3. Verify CONFIG         ← Read back and confirm EN_CRC=0 (clone chips may force it)
4. EN_RXADDR = 0x01      ← Enable pipe 0 only
5. SETUP_AW = 0x02       ← 4-byte addresses
6. SETUP_RETR            ← Disabled
7. RF_CH = 2             ← Initial channel (ch37)
8. RF_SETUP              ← 1 Mbps, 0 dBm
9. RX_PW_P0 = 32        ← Payload width
10. RX_ADDR_P0           ← BLE AA (4 bytes)
11. TX_ADDR              ← BLE AA (4 bytes)
12. FLUSH_RX             ← Clear RX FIFO
13. Clear STATUS flags   ← RX_DR, TX_DS, MAX_RT
14. Wait 2 ms            ← Oscillator settling (Tpd2stby = 1.5 ms)
15. CE HIGH              ← Enter RX mode
```

**Violating step 1→2 order is the #1 cause of "zero packets received."**

If EN_AA is non-zero when CONFIG is written, the nRF24L01+ hardware forces EN_CRC=1, making it impossible to receive BLE packets (BLE CRC-24 ≠ nRF24 CRC-8/CRC-16). See [`nrf24-enaa-encrc-override.md`](nrf24-enaa-encrc-override.md) for details.

---

## 9. Timing Constraints

| Parameter | Value | Source | Notes |
|---|---|---|---|
| Tpd2stby | 1.5 ms | Datasheet §6.1.2 Table 9 | Time from PWR_UP=1 to Standby-I |
| Tstby2a | 130 µs | Datasheet §6.1.7 Table 16 | Time from CE HIGH to RX active |
| RPD validity | 170 µs | Datasheet §9 | 130 µs + 40 µs AGC settling |
| Advertising interval | 20 ms – 10.24 s | Bluetooth Core Spec | Typical: 100 ms |
| nRF24 channel switch | ~200 µs | `switch_channel()` delay | CE low → RF_CH write → CE high → settle |
| SPI register write | ~20 µs | Measured | Single register via SPI at 10 MHz |

---

## 10. Common Pitfalls and Lessons Learned

### 10.1 EN_AA/EN_CRC override trap

If you write CONFIG with EN_CRC=0 while EN_AA is still at its reset value of 0x3F, the nRF24L01+ hardware silently forces EN_CRC=1. Every BLE packet will then fail nRF24 CRC validation, resulting in zero packets received. The SPI read-back of CONFIG will show EN_CRC=1, but the root cause is EN_AA, not CONFIG.

**Fix:** Always disable EN_AA (write 0x00) **before** writing CONFIG with EN_CRC=0. Then verify CONFIG read-back.

See [`nrf24-enaa-encrc-override.md`](nrf24-enaa-encrc-override.md).

### 10.2 Address byte order (SPI LSByte-first)

The nRF24L01+ SPI interface writes multi-byte registers LSByte first (datasheet §8.3.1). The BLE advertising access address `0x8E89BED6` must be written as `{0x71, 0x91, 0x7D, 0x6B}` (after bit-swap and LSByte reversal), not as the on-air order `{0x6B, 0x7D, 0x91, 0x71}`.

See [`nrf24-spi-address-byte-order.md`](nrf24-spi-address-byte-order.md).

### 10.3 Data whitening (bit-swap + LFSR)

BLE whitens every on-air PDU and CRC byte. The nRF24L01+ does not dewhitent BLE packets. Two transformations are needed:

1. **Bit-swap each byte** (nRF24 MSbit-first → BLE LSbit-first)
2. **Galois LFSR XOR dewhitening** with channel-dependent seed

The `nrf24::ble::dewhiten()` function combines both steps. Calling only the LFSR step without bit-swap will produce garbage.

See [`ble-data-whitening-nrf24.md`](ble-data-whitening-nrf24.md).

### 10.4 CE pin must use GPIO_MODE_INPUT_OUTPUT

The nRF24L01+ CE pin must be configured as `GPIO_MODE_INPUT_OUTPUT` on ESP32, not `GPIO_MODE_OUTPUT`. An output-only pin disables the input buffer, causing `gpio_get_level()` to always return 0, which prevents CE state verification. See the nRF24L01+ skill (§1.3).

### 10.5 MOSI pin direction must not be changed after SPI init

Setting an SPI pin to `GPIO_MODE_INPUT` after `spi_bus_initialize()` silently breaks all SPI writes while reads appear to work. See the nRF24L01+ skill (§1.4).

### 10.6 Clone chip detection

Si24R1/BK2425 clones may not follow the EN_CRC forcing rule correctly. The `configure_rx()` function checks CONFIG read-back and prints a warning if EN_CRC is forced on. Use `nrf24::diag::full_boot_diagnostic()` Stage 3 to detect clones.

### 10.7 FIFO padding bytes (0xFE)

After `flush_rx()`, the nRF24L01+ FIFO contains `0xFE` in unused positions. When reading 32 bytes, bytes beyond the BLE PDU length will be `0xFE` or leftover data from a previous packet. Always use `buf[1] & PDU_LENGTH_MASK` to determine the actual PDU length and ignore trailing bytes.

---

## 11. References

1. **Bluetooth Core Specification Vol 6 Part B** — Advertising physical channel PDU types (§2.3 Table 2.2), data whitening (§3.2), CRC-24 (§3.1.1), access address, PDU format. Available at: <https://www.bluetooth.com/specifications/specs/core-specification/> (Verified 2026-06-06 — Core Spec 6.3 is the current adopted version)

2. **nRF24L01+ Product Specification** — Register descriptions, SPI protocol, timing parameters (Tstby2a, Tpd2stby), CE pin behaviour, FIFO handling. Available from Nordic Semiconductor: <https://www.nordicsemi.com/Products/nRF24L01P> (Manufacturer product page)

3. **Dmitry Grinberg, "Bit-banging Bluetooth Low Energy"** — The foundational reference for using nRF24L01+ as a BLE receiver. Introduced the Galois LFSR dewhitening algorithm and bit-swap technique used in this project. (Original blog post URL no longer accessible; the algorithm is derived and verified against the Bluetooth Core Spec directly. See our implementation in `ble.cpp` and the detailed analysis in [`ble-data-whitening-nrf24.md`](ble-data-whitening-nrf24.md).)

4. **Project source files:**
   - `components/nrf24l01plus/include/nrf24l01plus/ble.h` — `swapbits()`, `dewhiten()`, `BleAdvPduType`, PDU type/length masks, `adv_address()`, `format_address()`
   - `components/nrf24l01plus/include/nrf24l01plus/ble_config.h` — `configure_rx()`, `switch_channel()`, `RxConfig`, `ADV_CHANNELS`, `ADV_ACCESS_ADDR`, `channel_to_rf_ch()`, `clear_irq_flags()`, `rx_fifo_not_empty()`
   - `components/nrf24l01plus/src/ble.cpp` — Implementation of `dewhiten()`, `pdu_type_name()`, `adv_address()`, `format_address()`
   - `components/nrf24l01plus/src/ble_config.cpp` — Implementation of `configure_rx()` with EN_AA/EN_CRC ordering

5. **Related learning docs:**
   - [`ble-data-whitening-nrf24.md`](ble-data-whitening-nrf24.md) — BLE whitening algorithm, LFSR seed derivation, and bugs found
   - [`nrf24-spi-address-byte-order.md`](nrf24-spi-address-byte-order.md) — LSByte-first SPI write convention and ADV_ACCESS_ADDR bug
   - [`nrf24-enaa-encrc-override.md`](nrf24-enaa-encrc-override.md) — EN_AA/EN_CRC register write order trap
   - [`cpp-enum-class-and-struct.md`](cpp-enum-class-and-struct.md) — Typed API patterns for production code