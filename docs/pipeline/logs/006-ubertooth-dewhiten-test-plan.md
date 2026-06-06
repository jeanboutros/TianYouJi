# 006 — Ubertooth-Based Dewhitening Validation Test Plan

**Date:** 2026-06-06  
**Author:** Wireless Expert  
**Status:** DRAFT — pending implementation  
**References:**
- Bluetooth Core Spec Vol 6 Part B §1.4.1 (channel mapping)
- Bluetooth Core Spec Vol 6 Part B §3.2 (data whitening)
- nRF24L01+ Product Specification v1.0 §7.1 (ShockBurst packet format)
- Dmitry Grinberg, "Bit-Banging Bluetooth Low Energy" (whitening algorithm)

---

## 1. Purpose

Validate that the `nrf24::ble::dewhiten()` function correctly reverses BLE
data whitening so that raw nRF24L01+ RX data, after dewhitening, matches the
known plaintext packets decoded by an Ubertooth One.

This is the **gold-standard** hardware validation test: Ubertooth decodes the
BLE packets correctly (it implements the full BLE PHY including whitening,
CRC, and access address detection), so its output serves as ground truth.

---

## 2. Critical Discrepancy Detected

> **⚠️ BLOCKING ISSUE — MUST BE RESOLVED BEFORE TESTING**

The current `ble.cpp` implementation uses an algorithm that **differs from
both** the header documentation and Dmitry Grinberg's reference:

| Aspect | Header Doc (`ble.h`) | Implementation (`ble.cpp`) | Dmitry Grinberg |
|--------|---------------------|---------------------------|-----------------|
| Seed | `swapbits(channel_idx) \| 2` | `(channel_idx & 0x3F) \| 0x40` | `swapbits(chan) \| 2` |
| LFSR direction | Left-shift (Galois) | Right-shift (Fibonacci) | Left-shift (Galois) |
| Feedback check | Bit 7 (`0x80`) | Bit 0 (`0x01`) | Bit 7 (`0x80`) |
| XOR polynomial | `0x11` | `0x48` | `0x11` |
| Bit-swap inside? | Yes (documented) | **No** (missing!) | Separate step |

The current `ble.cpp` implementation is a right-shifting Fibonacci LFSR that
does NOT perform the bit-swap step. The header documents a left-shifting Galois
LFSR with internal bit-swap. Dmitry Grinberg's reference confirms the Galois
form with `swapbits(chan) | 2` seed.

**This test plan assumes the implementation will be rewritten to match
Dmitry Grinberg's btLeWhiten (Galois LFSR, swapbits seed) per the task
description.** Once rewritten, this test plan validates the new implementation.

---

## 3. Prerequisites

### 3.1 Hardware

| Item | Requirement | Verified |
|------|-------------|----------|
| Ubertooth One | Connected via USB, detected by `lsusb` | ✅ Bus 003 Device 021, ID 1d50:6002 |
| ESP32 + nRF24L01+ | Running sniffer firmware, serial output | ⬜ Must be flashed and monitoring |
| Physical proximity | Both devices within ~1 m of BLE advertiser | ⬜ Arrange on desk |
| No interference | Avoid 2.4 GHz congestion during test | ⬜ Monitor RSSI |

### 3.2 Software — Host (Verified)

| Tool | Version | Status |
|------|---------|--------|
| `ubertooth-btle` | 2018.12.R1-5.3 | ✅ Installed |
| `ubertooth-util` | 2018.12.R1-5.3 | ✅ Installed |
| `ubertooth-dfu` | 2018.12.R1-5.3 | ✅ Installed |
| Wireshark / `tshark` | 4.4.15 | ✅ Installed, BTLE dissector available |
| `libbtbb1` | 2018.12.R1-1+b2 | ✅ Installed |
| Scapy | 2.7.0 | ✅ Installed (BTLE layers available) |
| Python 3 | System | ✅ Available |

### 3.3 Software — ESP32

| Item | Requirement |
|------|-------------|
| ESP-IDF v6.0.1 | Active toolchain |
| nRF24 sniffer firmware | Built with new `dewhiten()` |
| Serial monitor | `idf.py monitor` at 115200 baud |

### 3.4 Ubertooth Firmware

| Item | Value |
|------|-------|
| Firmware version | 2020-12-R1 |
| API version | 1.07 (host lib supports 1.06 — warning but functional) |
| Status | ✅ Confirmed working — captures live BLE advertisements |

### 3.5 BLE Advertisers (Test Sources)

We use **real BLE devices** as packet sources. No custom transmitter needed.
Sources already detected in the office environment:

| Device | MAC | Type | RSSI |
|--------|-----|------|------|
| Apple device 1 | various Apple MACs | ADV_NONCONN_IND (AirTag) | -50 to -60 dBm |
| Furbo3C-S3 | 00:2d:b3:11:33:8d | ADV_IND | -80 dBm |
| Apple device 2 | various | ADV_IND | -55 dBm |
| NW-20230064 | f7:a7:e8:3c:33:63 | ADV_IND | -87 dBm |

---

## 4. Test Architecture

```
                    ┌──────────────────┐
                    │  BLE Advertiser   │
                    │  (phone, AirTag,  │
                    │   nearby device)  │
                    └────────┬─────────┘
                             │ 2.4 GHz BLE
                    ┌────────┴─────────┐
                    │                  │
            ┌───────▼──────┐  ┌──────▼───────┐
            │ Ubertooth One │  │ ESP32+nRF24  │
            │ (Ground Truth)│  │  (DUT)       │
            │               │  │              │
            │ ubertooth-btle│  │ dewhiten()   │
            │ → PCAP file   │  │ → Serial out │
            └───────┬───────┘  └──────┬───────┘
                    │                  │
            ┌───────▼──────┐  ┌──────▼───────┐
            │ tshark /      │  │ Serial       │
            │ Wireshark     │  │ monitor      │
            │ decoded PDU   │  │ raw hex dump │
            └───────┬───────┘  └──────┬───────┘
                    │                  │
                    └──────┬───────────┘
                           │
                    ┌──────▼───────┐
                    │ Comparison   │
                    │ Script       │
                    │ (Python)     │
                    └──────────────┘
```

**Key insight:** We do NOT need to transmit custom packets. The Ubertooth
passively sniffs real BLE advertisements and produces correctly-decoded output.
Our ESP32+nRF24 sniffer captures the same over-the-air packets. If `dewhiten()`
is correct, the PDU bytes from both sources will match.

---

## 5. Test Cases

### 5.1 TC-01: Live Advertisement Capture — Channel 37 (2402 MHz)

**Objective:** Verify dewhitening produces correct PDU on adv channel 37.

**Steps:**
1. Start Ubertooth capture on channel 37:
   ```bash
   ubertooth-btle -n -A37 -q /tmp/tc01_ch37.pcap &
   ```
2. Start `tshark` live decode from the PCAP:
   ```bash
   tshark -r /tmp/tc01_ch37.pcap -Y "btle.advertising_address" \
     -T fields \
     -e btle.channel_idx \
     -e btle.pdu_type \
     -e btle.advertising_address \
     -e btle.advertising_data \
     -e btle.crc \
     2>/dev/null | head -20 &
   ```
3. Flash ESP32 with new `dewhiten()`, configure to listen **only on channel 37**:
   ```cpp
   static const uint8_t extra[] = {37};  // override: stay on ch37
   ```
4. Capture 10 unique advertisements from ESP32 serial output.
5. For each captured packet, compare the dewhitened PDU (header + MAC + data)
   against the Ubertooth-decoded packet.

**Expected:** PDU bytes, MAC addresses, and advertising data from ESP32 match
Ubertooth output exactly.

**Pass criterion:** ≥8 of 10 packets match byte-for-byte.

---

### 5.2 TC-02: Live Advertisement Capture — Channel 38 (2426 MHz)

**Objective:** Verify dewhitening on a different channel (different LFSR seed).

**Steps:**
1. Start Ubertooth on channel 38:
   ```bash
   ubertooth-btle -n -A38 -q /tmp/tc02_ch38.pcap &
   ```
2. Configure ESP32 to listen on channel 38 only.
3. Capture 5+ packets, compare against Ubertooth.

**Expected:** Same byte-level match as TC-01.

**Pass criterion:** All captured packets match.

**Why this matters:** Channel 38 uses LFSR seed `swapbits(38) | 2 = 0x64 | 0x02 = 0x66`,
which differs from seed for ch37 (`0xA4 | 0x02 = 0xA6`). Different seed →
different whitening sequence → reveals seed-dependent bugs.

---

### 5.3 TC-03: Live Advertisement Capture — Channel 39 (2480 MHz)

**Objective:** Verify dewhitening on the highest-frequency advertising channel.

**Steps:**
1. Start Ubertooth on channel 39:
   ```bash
   ubertooth-btle -n -A39 -q /tmp/tc03_ch39.pcap &
   ```
2. Configure ESP32 to listen on channel 39 only.
3. Capture 5+ packets, compare.

**Expected:** Byte-level match. The seed for ch39 is `swapbits(39) | 2 = 0xE4 | 0x02 = 0xE6`.

**Pass criterion:** All captured packets match.

---

### 5.4 TC-04: Round-Trip Consistency (Whiten → Dewhiten)

**Objective:** Verify that whiten and dewhiten are perfect inverses.

**Steps:**
1. Create a known test vector in a host-side test script:
   ```python
   def btLeWhitenStart(chan):
       return swapbits(chan) | 2

   def btLeWhiten(data, whiten_coeff):
       result = bytearray(data)
       idx = 0
       for byte_val in data:
           w = 0
           for m in range(8):
               if whiten_coeff & 0x80:
                   whiten_coeff ^= 0x11
                   w ^= (1 << m)
               whiten_coeff <<= 1
           result[idx] = byte_val ^ w
           idx += 1
       return bytes(result), whiten_coeff
   ```
2. For each channel in [0, 1, 10, 11, 37, 38, 39]:
   - Start with known plaintext: `{0x40, 0x0B, 0xEF, 0xFF, 0xC0, 0xAA, 0x18, 0x00}`
   - Whiten it with `btLeWhiten(data, btLeWhitenStart(ch))`
   - Dewhiten the result with the SAME function
   - Verify the output equals the original plaintext
3. Also test: all-zeros, all-0xFF, and 32-byte maximum payload.

**Expected:** Every round-trip produces identical data to the input.

**Pass criterion:** 100% match on all channel × test vector combinations.

---

### 5.5 TC-05: Edge Case — Channel 0 (lowest data channel)

**Objective:** Test the minimum valid BLE channel index.

**Note:** Channel 0 maps to RF_CH=4 (2404 MHz). Most BLE advertisers don't
send on data channels during advertising, so this test uses the host-side
round-trip verification only.

**Steps:**
1. Run the round-trip test with `channel_idx = 0`.
2. Seed for ch0: `swapbits(0) | 2 = 0x00 | 0x02 = 0x02`.
3. Verify this edge-case seed doesn't cause LFSR to produce all-zeros output
   (it should not — the `| 2` guarantees non-zero seed).

**Expected:** Round-trip succeeds. LFSR generates non-trivial whitening sequence.

**Pass criterion:** Whiten(dewhiten(data, 0), 0) == data for all test vectors.

---

### 5.6 TC-06: Edge Case — Channel 39 (maximum channel index)

**Objective:** Test the maximum BLE channel index (39 = last adv channel).

**Steps:**
1. Run round-trip test with `channel_idx = 39`.
2. Seed: `swapbits(39) | 2 = 0xE4 | 0x02 = 0xE6` (high seed value).
3. Verify the LFSR doesn't wrap or overflow incorrectly.

**Expected:** Round-trip succeeds.

**Pass criterion:** Same as TC-05 format.

---

### 5.7 TC-07: Cross-Channel Independence

**Objective:** Verify that dewhitening with the wrong channel produces
different (garbage) output — proving the channel seed actually matters.

**Steps:**
1. Capture a real BLE packet on channel 37 (from Ubertooth PCAP).
2. Extract the raw whitened bytes from the over-the-air capture.
3. Apply `dewhiten(raw, len, 37)` → should produce valid PDU.
4. Apply `dewhiten(raw, len, 38)` → should produce garbage (not a valid PDU).
5. Apply `dewhiten(raw, len, 39)` → should produce garbage.

**Expected:** Only the correct channel index produces a valid BLE header
(lower 4 bits of first byte ∈ {0x00–0x06}, length field ≤ 37).

**Pass criterion:** Correct channel produces valid header; wrong channels
produce invalid headers for ≥95% of test packets.

---

### 5.8 TC-08: Empty PDU (SCAN_RSP with no data)

**Objective:** Verify dewhitening works on the shortest possible advertising PDU.

**Steps:**
1. Wait for a SCAN_RSP with `pdu_len = 0` from Ubertooth capture
   (seen in office: `64 06 9b 5a 6a c0 d4 5f 44 ba e6` — 6-byte payload).
2. Compare the 6-byte dewhitened result from ESP32.

**Expected:** PDU header + AdvA matches Ubertooth decode.

**Pass criterion:** Byte-exact match.

---

### 5.9 TC-09: Maximum-Length PDU (37-byte advertising payload)

**Objective:** Verify dewhitening works on the longest advertising PDU
(PDU header 2 bytes + 37 bytes payload = 39 bytes total, 6 of those are CRC).

**Steps:**
1. Monitor for ADV_IND or ADV_NONCONN_IND with `pdu_len` close to 37.
2. Compare the full 39-byte dewhitened output against Ubertooth.

**Expected:** All 39 bytes match, including the CRC bytes after dewhitening.

**Pass criterion:** Byte-exact match on all bytes including CRC.

---

### 5.10 TC-10: Known-Vector Validation (Offline)

**Objective:** Validate dewhitening against known whitening test vectors
computed offline, independent of any captured data.

**Steps:**
1. Write a Python script implementing **Dmitry Grinberg's btLeWhiten** exactly:

   ```python
   def swapbits(v):
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
       return swapbits(chan) | 2

   def btLeWhiten(data, whitenCoeff):
       result = bytearray()
       for byte_val in data:
           w = 0
           for b in range(8):
               m = 1 << b
               if whitenCoeff & 0x80:
                   whitenCoeff ^= 0x11
                   w ^= m
               whitenCoeff = (whitenCoeff << 1) & 0xFF  # 8-bit wrap
           result.append(byte_val ^ w)
       return bytes(result), whitenCoeff
   ```

2. Generate whitened test vectors for channels 0, 1, 10, 11, 37, 38, 39
   using known plaintext inputs.
3. Run the C++ `dewhiten()` on the same whitened inputs on the host
   (compile the function in a standalone test harness).
4. Compare outputs.

**Expected:** Perfect match for all channel × input combinations.

**Pass criterion:** 100% byte-exact match.

---

## 6. Ubertooth Command Reference

### 6.1 Passive Capture (No Injection Needed)

| Command | Purpose |
|---------|---------|
| `ubertooth-btle -n -A37 -q out.pcap` | Capture ch37 adverts to PCAP |
| `ubertooth-btle -n -A38 -q out.pcap` | Capture ch38 adverts to PCAP |
| `ubertooth-btle -n -A39 -q out.pcap` | Capture ch39 adverts to PCAP |
| `ubertooth-btle -n -A37 -r out.pcapng` | Same but PCAPNG format |
| `ubertooth-util -S` | Stop current operation |
| `ubertooth-util -v` | Check firmware version |
| `ubertooth-util -I` | Flash LEDs (identify device) |
| `ubertooth-util -c2402` | Set frequency manually (MHz) |
| `ubertooth-util -C37` | Set BT channel number (0–78) |

### 6.2 PCAP Analysis with tshark

| Command | Purpose |
|---------|---------|
| `tshark -r out.pcap -T fields -e btle.channel_idx -e btle.pdu_type -e btle.advertising_address -e btle.advertising_data` | Extract decoded PDU fields |
| `tshark -r out.pcap -V -O btle` | Full verbose BLE decode |
| `tshark -r out.pcap -x` | Hex + ASCII dump of raw frames |

### 6.3 Reading Raw Whitened Bytes from Ubertooth

The `ubertooth-btle` tool prints raw hex **after** dewhitening. To get the
raw whitened bytes over the air, use:

```bash
# Method 1: ubertooth-btle hex output is of the DECODED (dewhitened) data
# The raw bytes before dewhitening are not directly shown

# Method 2: Use ubertooth-dump for raw captured data
ubertooth-dump -c - > /tmp/raw_capture.bin

# Method 3: Capture to PCAP and use tshark to extract raw link-layer bytes
tshark -r out.pcap -T fields -e frame.time_epoch -e btle_rf.channel -e btle -e data.data
```

**Important:** Ubertooth's `ubertooth-btle` output shows **dewhitened** data.
This is exactly what our ESP32 output should match after `dewhiten()`.
No additional dewhitening step is needed for the Ubertooth side.

---

## 7. Comparison Procedure

### 7.1 Automated Comparison Script

```python
#!/usr/bin/env python3
"""compare_pdus.py — Compare ESP32 sniffer output with Ubertooth ground truth."""
import re
import sys

# Pattern for ESP32 serial output:
# [ch37] ADV_IND  8d:33:11:b3:2d:00  len=33
#   pdu: 00 23 8d 33 11 b3 2d 00 02 01 1e ...

def parse_esp32_line(line):
    """Parse a single pdu: line from ESP32 serial output."""
    m = re.match(r'\[ch(\d+)\]\s+(\S+)\s+([0-9a-f:]+)\s+len=(\d+)', line)
    if not m:
        return None
    return {
        'channel': int(m.group(1)),
        'pdu_type': m.group(2),
        'mac': m.group(3),
        'pdu_len': int(m.group(4)),
    }

def parse_pdu_hex(line):
    """Parse the '  pdu:' hex dump line."""
    m = re.match(r'\s*pdu:\s+(.*)', line)
    if not m:
        return None
    return bytes.fromhex(m.group(1).replace(' ', ''))

def parse_ubertooth_pcap(pcap_file, channel):
    """Extract decoded PDUs from Ubertooth PCAP for a given channel."""
    # Use tshark to extract
    import subprocess
    result = subprocess.run(
        ['tshark', '-r', pcap_file,
         '-Y', f'btle.channel_idx=={channel}',
         '-T', 'fields',
         '-e', 'btle.pdu_type',
         '-e', 'btle.advertising_address',
         '-e', 'btle.advertising_data',
         '-e', 'btle.crc'],
        capture_output=True, text=True
    )
    packets = []
    for line in result.stdout.strip().split('\n'):
        if not line.strip():
            continue
        parts = line.split('\t')
        packets.append({
            'pdu_type': parts[0] if len(parts) > 0 else '',
            'adv_addr': parts[1] if len(parts) > 1 else '',
            'adv_data': parts[2] if len(parts) > 2 else '',
            'crc': parts[3] if len(parts) > 3 else '',
        })
    return packets

def match_packet(esp32_pkt, ubertooth_pkts):
    """Find matching Ubertooth packet by MAC address + PDU type."""
    for up in ubertooth_pkts:
        # Compare MAC addresses (ESP32 format: XX:XX:XX:XX:XX:XX)
        if esp32_pkt['mac'].lower() == up.get('adv_addr', '').lower().replace(':', ''):
            return up
    return None

def main():
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <esp32_log> <ubertooth_pcap> <channel>")
        sys.exit(1)
    
    esp32_log = sys.argv[1]
    pcap_file = sys.argv[2]
    channel = int(sys.argv[3])
    
    ubertooth_pkts = parse_ubertooth_pcap(pcap_file, channel)
    print(f"Loaded {len(ubertooth_pkts)} Ubertooth packets for channel {channel}")
    
    matched = 0
    total = 0
    
    with open(esp32_log) as f:
        current_pkt = None
        for line in f:
            if '[ch' in line:
                current_pkt = parse_esp32_line(line)
            elif 'pdu:' in line:
                pdu_bytes = parse_pdu_hex(line)
                if current_pkt and pdu_bytes and current_pkt['channel'] == channel:
                    total += 1
                    # Look up matching Ubertooth packet
                    up = match_packet(current_pkt, ubertooth_pkts)
                    if up:
                        matched += 1
                        print(f"  ✓ MAC {current_pkt['mac']} matched")
                    else:
                        print(f"  ✗ MAC {current_pkt['mac']} NOT found in Ubertooth capture")
    
    print(f"\nResult: {matched}/{total} packets matched")
    if total > 0 and matched / total >= 0.8:
        print("PASS")
        sys.exit(0)
    else:
        print("FAIL")
        sys.exit(1)

if __name__ == '__main__':
    main()
```

### 7.2 Manual Comparison

When automated comparison isn't available, use this manual procedure:

1. From Ubertooth output:
   ```
   00 23 8d 33 11 b3 2d 00 02 01 1e 0b 09 46 75 72 62 6f 33 43 2d 53 33 0d ff 30 30 32 64 62 33 31 31 33 33 38 63 9e 94 4d
   ```
   This is the **dewhitened** PDU (header + AdvA + AdvData + CRC).

2. From ESP32 serial output:
   ```
   [ch37] ADV_IND  8d:33:11:b3:2d:00  len=33
     pdu: 00 23 8d 33 11 b3 2d 00 02 01 1e ...
   ```

3. Compare:
   - PDU type byte (byte 0): `0x00` (ADV_IND) — both should match
   - MAC address (bytes 2–7): `8d 33 11 b3 2d 00` — must match
   - AdvData (bytes 8+): should match

---

## 8. LFSR Seed Verification Table

For each BLE channel, the LFSR seed must be `swapbits(channel) | 2`.

| Channel | channel (bin) | swapbits(chan) | swapbits \| 2 | RF_CH | Freq (MHz) |
|---------|--------------|----------------|---------------|-------|------------|
| 0 | 00000000 | 0x00 | **0x02** | 4 | 2404 |
| 1 | 00000001 | 0x80 | **0x82** | 6 | 2406 |
| 10 | 00001010 | 0x50 | **0x52** | 24 | 2424 |
| 11 | 00001011 | 0xD0 | **0xD2** | 28 | 2428 |
| 37 | 00100101 | 0xA4 | **0xA6** | 2 | 2402 |
| 38 | 00100110 | 0x64 | **0x66** | 26 | 2426 |
| 39 | 00100111 | 0xE4 | **0xE6** | 80 | 2480 |

Note: For ch37, `swapbits(37) = swapbits(0x25) = 0xA4`, and `0xA4 | 2 = 0xA6`
(bit 1 of 0xA4 = 10100100 is 0, so OR with 0x02 sets it → 0xA6 = 10100110).
For ch38, `swapbits(38) = swapbits(0x26) = 0x64` (NOT 0x4C — 0x26 = 00100110,
reversed = 01100100 = 0x64), and `0x64 | 2 = 0x66`.

**Verification:** For each channel, compute the seed and confirm the first 8
whitening bits produce the expected XOR pattern.

---

## 9. Pass/Fail Criteria Summary

| Test | Channel | Min Packets | Pass Rate | Blocking |
|------|---------|-------------|-----------|----------|
| TC-01 | 37 | 10 | 80% | **Yes** |
| TC-02 | 38 | 5 | 100% | **Yes** |
| TC-03 | 39 | 5 | 100% | **Yes** |
| TC-04 | All | N/A | 100% | **Yes** |
| TC-05 | 0 | N/A | 100% | Yes |
| TC-06 | 39 | N/A | 100% | Yes |
| TC-07 | 37/38/39 | 5 | 95% | Yes |
| TC-08 | Any | 1 | 100% | No |
| TC-09 | Any | 1 | 100% | No |
| TC-10 | All | N/A | 100% | **Yes** |

**Overall verdict:**
- All **blocking** tests must PASS
- Non-blocking test failures are advisory (flag for PM)
- If any blocking test fails: → route to code-architect for dewhitening fix

---

## 10. Execution Order

1. **TC-10** (offline known-vector) — run FIRST on host, no hardware needed
2. **TC-04** (round-trip) — run on host, no hardware needed
3. **TC-05** / **TC-06** (edge cases) — run on host
4. Flash ESP32 with new dewhiten() implementation
5. **TC-01** (ch37 live capture) — first hardware test
6. **TC-02** (ch38 live capture)
7. **TC-03** (ch39 live capture)
8. **TC-07** (cross-channel independence)
9. **TC-08** / **TC-09** (short/long PDU) — opportunistic, during other captures

---

## 11. Known Limitations

1. **Ubertooth API version mismatch** — Host lib supports API 1.06, firmware
   reports 1.07. Warning printed but operation is confirmed functional.

2. **No custom packet injection** — The `ubertooth-tx` tool is NOT installed
   (not included in the 2018.12.R1 package). We rely on ambient BLE advertisers.
   If controlled test packets are needed in the future, either:
   - Install a custom Ubertooth firmware with TX capability, or
   - Use a second ESP32+nRF24 as a transmitter, or
   - Use `hcitool` / `bluez` to configure LE advertising from the laptop's
     built-in Bluetooth adapter (requires `sudo hcitool cmd`).

3. **Packet timing** — Ambient advertisers may not transmit on all 3 adv
   channels. If a device is only heard on ch37 and ch38, TC-03 cannot
   be completed for that device. Use the phone's Bluetooth settings to
   force re-advertising if needed.

4. **nRF24L01+ 32-byte limit** — The nRF24 can only capture packets up to
   32 bytes. BLE advertising PDUs can be up to 39 bytes (PDU + CRC). If
   CRC verification is needed, the last 3 CRC bytes may be truncated.
   This is a known hardware limitation, not a dewhitening bug.

5. **Bit-swap after dewhiten** — Dmitry Grinberg's `btLePacketEncode`
   performs `swapbits` **after** whitening (for TX) and before dewhitening
   (for RX on the nRF24). Our `dewhiten()` should handle this internally
   as documented in the header. If the bit-swap is missing, all bytes
   will be bit-reversed relative to the Ubertooth ground truth.

---

## 12. Bluetooth Core Spec Cross-References

| Topic | Spec Reference | Key Detail |
|-------|----------------|------------|
| Channel mapping | Vol 6 Part B §1.4.1 | 40 channels: 37 data, 3 advertising |
| Advertising channels | Vol 6 Part B §1.4.1 | ch37=2402, ch38=2426, ch39=2480 MHz |
| Data whitening | Vol 6 Part B §3.2 | Polynomial x^7+x^4+1, seed=channel_idx+64 |
| Whitening seed | Vol 6 Part B §3.2 | "initialized with the value of the channel index plus 64" |
| Advertising PDU | Vol 6 Part B §2.3 | Header 2 bytes, PDU body up to 37 bytes |
| Access address | Vol 6 Part B §2.1.2 | adv: 0x8E89BED6 |
| Bit order | Vol 6 Part B §1.2 | LSbit first on air |

> **⚠️ Critical Note on Seed Formula:**
> The Bluetooth Core Spec §3.2 states the seed is "channel index + 64" (i.e.,
> `channel_idx | 0x40`, since 64 = 0x40). However, Dmitry Grinberg's working
> implementation uses `swapbits(chan) | 2`. These are NOT the same:
>
> | Channel | Spec: ch+64 | Grinberg: swapbits(ch)|2 |
> |---------|------------|------------------------|
> | 37 | 37+64=101=0x65 | swapbits(37)\|2=0xA6 |
> | 38 | 38+64=102=0x66 | swapbits(38)\|2=0x66 |
> | 39 | 39+64=103=0x67 | swapbits(39)\|2=0xE6 |
>
> The difference arises because the spec describes the seed as a 7-bit value
> loaded into the LFSR in a specific bit order. The nRF24 delivers data
> MSbit-first per byte, while BLE transmits LSbit-first. The `swapbits`
> accounts for this bit-order mismatch.
>
> **Dmitry Grinberg's seed formula produces working code on the nRF24.**
> The current `ble.cpp` implementation uses `(channel_idx & 0x3F) | 0x40`
> which is closer to the spec's literal formula — but the LFSR direction
> and feedback polynomial also differ. The entire algorithm must be replaced
> as a unit; mixing seed formulas with mismatched LFSR implementations
> will produce incorrect results.

---

## Appendix A: Current vs. Required Implementation

### Current (`ble.cpp` — INCORRECT)

```cpp
void dewhiten(uint8_t *data, uint8_t len, uint8_t channel_idx)
{
    uint8_t lfsr = (channel_idx & 0x3F) | 0x40;        // Fibonacci seed
    for (uint8_t i = 0; i < len; i++) {
        uint8_t w = 0;
        for (int b = 0; b < 8; b++) {
            if (lfsr & 0x01) {                          // Check bit 0
                w |= (1 << b);
                lfsr ^= 0x48;                           // Fibonacci feedback
            }
            lfsr >>= 1;                                 // Right-shift
        }
        data[i] ^= w;
    }
    // NO bit-swap step!
}
```

### Required (Dmitry Grinberg's btLeWhiten — Galois LFSR)

```cpp
void dewhiten(uint8_t *data, uint8_t len, uint8_t channel_idx)
{
    uint8_t lfsr = swapbits(channel_idx) | 2;          // Galois seed
    for (uint8_t i = 0; i < len; i++) {
        uint8_t w = 0;
        for (uint8_t m = 1; m; m <<= 1) {              // MSB to LSB
            if (lfsr & 0x80) {                          // Check bit 7
                lfsr ^= 0x11;                           // Galois feedback
                w ^= m;
            }
            lfsr <<= 1;                                 // Left-shift
        }
        data[i] ^= w;
    }
}
```

**Differences:**
1. **Seed:** `swapbits(ch)|2` vs `(ch&0x3F)|0x40`
2. **LFSR direction:** Left-shift (Galois) vs right-shift (Fibonacci)
3. **Feedback check:** Bit 7 vs bit 0
4. **Feedback XOR:** `0x11` vs `0x48`
5. **Bit-swap:** Included in Galois form (the swapbits in the seed handles
   the bit-order conversion, so no separate byte-wise swap is needed — the
   Galois LFSR generates whitening bits in MSB→LSB order matching nRF24's
   byte orientation)

---

## Appendix B: Ubertooth Capture Quick Reference

```bash
# 1. Verify Ubertooth is connected
lsusb | grep -i 1d50:6002
ubertooth-util -v

# 2. Capture advertisements on channel 37 to PCAP
ubertooth-btle -n -A37 -q /tmp/ch37.pcap &
BTLE_PID=$!

# 3. Wait for some captures
sleep 30

# 4. Stop capture
kill $BTLE_PID

# 5. Decode with tshark
tshark -r /tmp/ch37.pcap -T fields \
  -e frame.time_relative \
  -e btle.channel_idx \
  -e btle.pdu_type \
  -e btle.advertising_address \
  -e btle.length \
  -e btle.advertising_data \
  -e btle.crc

# 6. Live capture with text output (no PCAP)
ubertooth-btle -n -A37

# 7. Capture all 3 advertising channels (rotate manually)
for ch in 37 38 39; do
  ubertooth-btle -n -A${ch} -q /tmp/ch${ch}.pcap &
  sleep 20
  kill $!
done
```
