# 012 — Challenger Hardware Engineer Adversarial Review

**Date:** 2026-06-06  
**Reviewer:** Challenger Hardware Engineer (independent adversarial review)  
**Scope:** Register models, datasheet fidelity, SPI protocol, BLE Access Address byte order, dewhiten() LFSR verification, GPIO/SPI timing, nRF24L01+ FIFO handling  
**Model:** Fresh perspective — specifically tasked to find what the primary review MISSED  
**Datasheet reference:** nRF24L01+ Product Specification v1.0 (local copy: `docs/datasheets/nRF24L01P_PS_v1.0.pdf`)

---

## Executive Summary

This review identifies **8 findings** across 3 severity levels. The most critical is a **reversed byte order in the BLE advertising Access Address** (`ADV_ACCESS_ADDR`) that would prevent the nRF24L01+ from matching BLE advertising packets at all. The dewhiten() implementation is **bitwise verified correct** against Dmitry Grinberg's reference. Register models are faithful to the datasheet. There is one SPI timing concern (MOSI direction change) and one learning doc error (RF_SETUP reset value).

**Overall Verdict: REJECTED** — the Access Address byte order bug must be fixed before the code can receive any BLE packets.

---

## Issue Register

### CRITICAL

#### C-1: BLE Advertising Access Address Byte Order is Reversed

**File:** `components/nrf24l01plus/include/nrf24l01plus/ble_config.h:46`  
**Severity:** CRITICAL — nRF24L01+ will not match any BLE advertising packets

```cpp
inline constexpr uint8_t ADV_ACCESS_ADDR[4] = {0x6B, 0x7D, 0x91, 0x71};
```

The BLE advertising Access Address is `0x8E89BED6`. Each byte must be bit-swapped for the nRF24L01+ (MSbit-first to LSbit-first). The bit-swap values are correct per-byte:
- `0x8E → 0x71` ✓
- `0x89 → 0x91` ✓
- `0xBE → 0x7D` ✓
- `0xD6 → 0x6B` ✓

**However, the byte ORDER is reversed.** The nRF24L01+ SPI protocol requires multi-byte address registers to be written **LSByte first** (datasheet §8.3.1: "LSByte is written first"). The current array stores the bytes in **MSByte-first** order.

**Root cause analysis:**

The BLE AA `0x8E89BED6` is transmitted on-air LSByte-first:
- Air byte 0 (first transmitted) = `0xD6` (LSByte, LSbit-first within byte)
- Air byte 1 = `0xBE`
- Air byte 2 = `0x89`
- Air byte 3 (last) = `0x8E` (MSByte)

The nRF24L01+ receives MSByte-first and MSbit-first. Bit-swapping each on-air byte:
- MSByte (first on-air, first matched) = `swapbits(0xD6) = 0x6B`
- Next = `swapbits(0xBE) = 0x7D`
- Next = `swapbits(0x89) = 0x91`
- LSByte (last on-air) = `swapbits(0x8E) = 0x71`

So the nRF24L01+ address MSByte is `0x6B` and LSByte is `0x71`.

**Correct LSByte-first SPI order:** `{0x71, 0x91, 0x7D, 0x6B}`  
**Current (wrong) MSByte-first order:** `{0x6B, 0x7D, 0x91, 0x71}`

Cross-reference with Dmitry Grinberg's "Bit-banging Bluetooth Low Energy" reference implementation confirms this. Grinberg writes:
```c
buf[1] = swapbits(0x8E);  // 0x71 — LSByte first
buf[2] = swapbits(0x89);  // 0x91
buf[3] = swapbits(0xBE);  // 0x7D
buf[4] = swapbits(0xD6);  // 0x6B — MSByte last
```

**Impact:** The nRF24L01+ will match the Access Address `0x71910x6B` on-air instead of `0x6B7D9171` on-air (nRF24 MS-bit interpretation). Since no BLE device transmits with the reversed address, **no BLE packets will ever be received.**

**Note:** This bug may have gone unnoticed during testing if the `diag::verify_ble_rx()` check reads back the register and compares against the same `ADV_ACCESS_ADDR` array — both write and read-back would use the same (wrong) byte order, making the diagnostic pass.

**Fix:** Change `ADV_ACCESS_ADDR` to:
```cpp
inline constexpr uint8_t ADV_ACCESS_ADDR[4] = {0x71, 0x91, 0x7D, 0x6B};
```

**Datasheet ref:** nRF24L01+ PS v1.0 §8.3.1 (multi-byte register write order), §7.3 (address byte order, MSByte transmitted first on-air).

---

#### C-2: MOSI Pin Direction Change Breaks Subsequent SPI Transfers

**File:** `main/main.cpp:133`  
**Severity:** CRITICAL — potentially breaks all SPI communication after initialisation

```cpp
gpio_set_direction(hal.mosi_pin(), GPIO_MODE_INPUT);
```

After initialising the SPI bus and configuring the nRF24L01+ for BLE RX mode, `main.cpp` changes the MOSI pin to `GPIO_MODE_INPUT` to "reduce 2.4GHz noise during RX-only operation."

Immediately after this, `ble_sniffer_task()` runs and calls `switch_channel()` every 50 ms, which in turn calls `write_reg()`, `flush_rx()`, and `clear_irq_flags()` — all of which require MOSI to send SPI command bytes. Additionally, `read_reg()` in the heartbeat requires MOSI for the R_REGISTER command byte.

**The critical question:** On ESP32, does `gpio_set_direction()` override the SPI peripheral's control of the MOSI pin?

On the ESP32, when a pin is assigned to the SPI peripheral via `spi_bus_initialize()`, the IO MUX routes the pin's output to the SPI controller. The `gpio_set_direction()` call configures the GPIO matrix, which is a separate path. On ESP32 (not ESP32-C3/S3), the VSPI/HSPI pins are routed through the IO MUX, and the SPI peripheral **may** override the GPIO direction setting. However:

1. **If MOSI truly becomes an input:** All subsequent SPI writes (register configuration, flush commands) will send garbage command bytes. The nRF24L01+ will ignore or misinterpret them. Channel switching, status reading, and FIFO flushing will all silently fail.

2. **If MOSI direction is overridden by SPI MUX:** The `gpio_set_direction()` call is a no-op, and SPI continues to work — but the intended noise reduction is also not achieved, making the line misleading dead code.

**In neither case is the code correct.** Either it silently breaks SPI (dangerous) or it does nothing (misleading).

**Datasheet ref:** ESP32 Technical Reference Manual, Chapter 10 (GPIO and IO MUX); nRF24L01+ PS v1.0 §8.3 (SPI command byte requirements).

**Fix:** Either:
1. Remove the MOSI direction change entirely (safest), or
2. Verify on the actual ESP32 hardware that `gpio_set_direction()` does NOT break SPI by reading STATUS register after the change, and document the platform-dependent behaviour with a comment explaining why it's safe.

---

### HIGH

#### H-1: Learning Doc RF_SETUP Reset Value Discrepancy

**File:** `docs/learning/nrf24-spi-basics.md:106`  
**Severity:** HIGH — incorrect reference information

The learning doc `nrf24-spi-basics.md` states in the register summary table:

```
| 0x06 | RF_SETUP | 0x0F | RF Setup Register |
```

The correct reset value per the nRF24L01+ Product Specification v1.0, Table 28, is **`0x0E`** (0b0000_1110). Bit 0 (labeled "Obsolete, don't care") resets to 0, not 1. The register model in `rf_setup.h` correctly uses `RESET_VALUE = 0x0E`.

Some clone/fake nRF24L01+ chips reset bit 0 to 1 (giving 0x0F), which may be the source of the confusion. But the genuine Nordic part resets to 0x0E.

**Fix:** Update `docs/learning/nrf24-spi-basics.md` line 106 from `0x0F` to `0x0E`.

---

#### H-2: dewhiten() LFSR — Verified Correct Against Reference But Documentation Gap on Whiten Path

**File:** `components/nrf24l01plus/src/ble.cpp`  
**Severity:** HIGH — not a bug, but an API completeness gap

The dewhiten() implementation has been **bitwise verified** against Dmitry Grinberg's reference implementation. Every component matches:

| Component | Grinberg Reference | Our Code | Match? |
|-----------|-------------------|----------|--------|
| Seed | `swapbits(chan) \| 2` | `swapbits(channel_idx) \| 0x02` | ✅ |
| LFSR form | Galois, left-shift | Galois, left-shift | ✅ |
| Feedback check | `whitenCoeff & 0x80` | `lfsr & 0x80` | ✅ |
| Feedback XOR | `whitenCoeff ^= 0x11` | `lfsr ^= 0x11` | ✅ |
| Mask iteration | `for (m = 1; m; m <<= 1)` | `for (uint8_t m = 1; m; m <<= 1)` | ✅ |
| Bit-swap | Separate `swapbits()` call before `btLeWhiten` | Integrated as step 1 of `dewhiten()` | ✅ |

**Verified output for channel 37, first 8 XOR masks:**
- Seed: `swapbits(37) | 2 = 0xA6`
- Step-by-step LFSR trace matches the manual calculation in the learning doc (§4.3)
- First-byte mask: `0x8D` ✅

**Gap:** The library provides `dewhiten()` for RX but has **no `whiten()` function for TX**. While the current application only does passive RX, the `ble.h` documentation mentions "every byte written for TX must be bit-swapped after BLE processing," but no API exists to perform the TX whitening pipeline:
1. CRC24 computation
2. Bit-swap CRC bytes
3. Whiten PDU+CRC with LFSR
4. Bit-swap all bytes

This means any future TX functionality would need to be built from scratch, with no tested reference in the codebase.

**Recommendation:** Add a `whiten()` function that performs the inverse pipeline, or at minimum add a Doxygen note stating "For TX, apply LFSR then bit-swap; dewhiten() cannot be directly used for whitening unless the bit-swap step is already accounted for externally."

---

### MEDIUM

#### M-1: No Power-On Delay Before SPI Communication

**Severity:** MEDIUM — potential SPI failure at startup

The nRF24L01+ Product Specification v1.0 §6.1.7 states that after VDD reaches 1.9V, the device needs **1.5 ms** (typ) to **10.3 ms** (max) before SPI commands are stable. The current code in `EspIdfHal::init()` calls `spi_bus_initialize()` and immediately writes registers via `ble::configure_rx()` with no delay.

If the nRF24L01+ is not fully powered up when SPI commands arrive, the initial register writes may be ignored or corrupted, leading to an unconfigured radio.

**Fix:** Add a `vTaskDelay(pdMS_TO_TICKS(20))` (or at minimum 15ms) after `hal.init()` and before `configure_rx()` in `app_main()`. Per the datasheet, 10.3ms is the worst-case startup time; 20ms provides margin.

**Datasheet ref:** nRF24L01+ PS v1.0 §6.1.7 (Power on reset delay), Table 16 (Timings).

---

#### M-2: CONFIG Register Reserved Bit 7 Not Explicitly Preserved

**File:** `components/nrf24l01plus/include/nrf24l01plus/registers/config.h`  
**Severity:** MEDIUM — potential issue when modifying CONFIG on a live device

The `Config::to_byte()` function reconstructs the full register byte from individual fields. Bit 7 of CONFIG is documented as reserved ("Only '0' allowed"), and `to_byte()` correctly does not include bit 7 in any field, so it always writes 0 to bit 7. This is correct per the datasheet.

However, the general pattern of `to_byte()` (reconstruct from fields, zeroing all unaccounted bits) could be hazardous for other registers if reserved bits have non-zero reset values. The codebase handles this inconsistently:
- `RfCh::to_byte()` masks with `& 0x7F` (explicit) ✓
- `Config::to_byte()` zeros bit 7 by omission (acceptable for CONFIG where bit 7 must be 0) ✓
- `Status::to_byte()` zeros bit 7 (acceptable for STATUS which is write-1-to-clear on bits 6:4) ✓

**Recommendation:** Add a comment in `Config::to_byte()` stating: "Bit 7 is reserved (must be 0) and is correctly omitted. See nRF24L01+ PS v1.0 Table 9."

---

#### M-3: SPI Device Configuration Missing `post_cb` and Has Queue Size 1

**File:** `components/nrf24_espidf/src/hal_espidf.cpp:28–30`  
**Severity:** MEDIUM — SPI transaction ordering concern

```cpp
dev_cfg.queue_size = 1;
```

The `queue_size = 1` means only one SPI transaction can be queued. Since the driver uses `spi_device_polling_transmit()` (synchronous, blocking), this is acceptable for single-threaded use. However, if two tasks ever share the radio handle (see C-2 from 011-challenger-sw-engineer), concurrent `spi_xfer()` calls would fail silently or corrupt data.

Combined with C-2 (no thread safety), the `queue_size = 1` is correct for the current single-task design but represents a latent risk.

**Recommendation:** Document that `EspIdfHal` is NOT thread-safe and must only be called from a single task context.

---

## Dewhiten() Verification — Detailed

### Bitwise Comparison with Dmitry Grinberg's Reference

I verified every implementation detail of `dewhiten()` against Grinberg's published C source code (retrieved from `dmitry.gr`, verified 2026-06-06):

1. **`swapbits()` function** — Verified identical. Our if-else cascade matches Grinberg's original:
   ```c
   // Grinberg:
   if(a & 0x80) v |= 0x01;
   if(a & 0x40) v |= 0x02;
   // ... (all 8 bits, identical mapping)
   ```
   Our implementation at `ble.h:35–43` is character-for-character identical (modulo C vs C++ syntax and parameter naming).

2. **Seed computation** — Verified identical:
   - Grinberg: `return swapbits(chan) | 2;`
   - Ours: `uint8_t lfsr = swapbits(channel_idx) | 0x02;`
   - Both use the `| 2` constant (setting bit 1 in the Galois LFSR register).

3. **LFSR inner loop** — Verified identical:
   - Grinberg: `for(m = 1; m; m <<= 1) { if(whitenCoeff & 0x80) { whitenCoeff ^= 0x11; (*data) ^= m; } whitenCoeff <<= 1; }`
   - Ours: `for (uint8_t m = 1; m; m <<= 1) { if (lfsr & 0x80) { lfsr ^= 0x11; data[i] ^= m; } lfsr <<= 1; }`
   - Same polynomial (`0x11`), same check (`& 0x80`), same left-shift, same XOR-into-data pattern.

4. **Bit-swap step** — Grinberg's `btLePacketEncode` does the bit-swap as a separate pass (step 4: `for(i = 0; i < len; i++) packet[i] = swapbits(packet[i])`). Our `dewhiten()` integrates the bit-swap as step 1. The net effect is identical because XOR and bit-swap are independent operations on separate bytes.

**Conclusion:** The dewhiten() implementation is **certified correct** against the reference implementation. No functional differences exist.

### Step-by-Step LFSR Trace for Channel 37

Verified the full 8-step LFSR evolution for channel 37, matching the learning doc (§4.3):

| Step | LFSR before | Bit 7? | XOR 0x11? | LFSR after XOR | After `<<= 1` | Output bit |
|------|-------------|--------|-----------|----------------|---------------|------------|
| 0 | `0xA6` | 1 | Yes | `0xB7` | `0x6E` | bit 0 = 1 |
| 1 | `0x6E` | 0 | No | — | `0xDC` | bit 1 = 0 |
| 2 | `0xDC` | 1 | Yes | `0xCD` | `0x9A` | bit 2 = 1 |
| 3 | `0x9A` | 1 | Yes | `0x8B` | `0x16` | bit 3 = 1 |
| 4 | `0x16` | 0 | No | — | `0x2C` | bit 4 = 0 |
| 5 | `0x2C` | 0 | No | — | `0x58` | bit 5 = 0 |
| 6 | `0x58` | 0 | No | — | `0xB0` | bit 6 = 0 |
| 7 | `0xB0` | 1 | Yes | `0xA1` | `0x42` | bit 7 = 1 |

**First-byte XOR mask: `0b10001101 = 0x8D`** ✅ (matches learning doc §4.4)

---

## Register Model Verification — Datasheet Cross-Reference

Every register model was checked field-by-field against the nRF24L01+ Product Specification v1.0, Table 28 and individual register descriptions (§8.3):

| Register | Header | Address | Reset Value | Bit Positions | Encodings | Verdict |
|----------|--------|---------|-------------|---------------|-----------|---------|
| CONFIG | `config.h` | 0x00 ✅ | 0x08 ✅ | Bits 7:0 ✅ | IrqMask, CrcMode, CrcEncoding, PowerMode, PrimaryMode ✅ | PASS |
| EN_AA | `en_aa.h` | 0x01 ✅ | 0x3F ✅ | Bits 5:0 ✅ | Per-pipe enable ✅ | PASS |
| EN_RXADDR | `en_rxaddr.h` | 0x02 ✅ | 0x03 ✅ | Bits 5:0 ✅ | Per-pipe enable ✅ | PASS |
| SETUP_AW | `setup_aw.h` | 0x03 ✅ | 0x03 ✅ | Bits 1:0 ✅ | 01=3B, 10=4B, 11=5B ✅ | PASS |
| SETUP_RETR | `setup_retr.h` | 0x04 ✅ | 0x03 ✅ | Bits 7:4=ARD, 3:0=ARC ✅ | All 16 delay values, 16 count values ✅ | PASS |
| RF_CH | `rf_ch.h` | 0x05 ✅ | 0x02 ✅ | Bits 6:0 ✅ | 0–125 → 2400–2525 MHz ✅ | PASS |
| RF_SETUP | `rf_setup.h` | 0x06 ✅ | 0x0E ✅ | Bit 7=CONT_WAVE, 6=rsvd, 5=RF_DR_LOW, 4=PLL_LOCK, 3=RF_DR_HIGH, 2:1=RF_PWR, 0=obs ✅ | DataRate non-contiguous encoding ✅, TxPower ✅ | PASS |
| STATUS | `status.h` | 0x07 ✅ | 0x0E ✅ | Bits 6:4=IRQ, 3:1=RX_P_NO, 0=TX_FULL ✅ | RxPipeNo enum ✅ | PASS |
| OBSERVE_TX | `observe_tx.h` | 0x08 ✅ | 0x00 ✅ | Bits 7:4=PLOS_CNT, 3:0=ARC_CNT ✅ | Read-only ✅ | PASS |
| RPD | `rpd.h` | 0x09 ✅ | 0x00 ✅ | Bit 0 ✅ | Read-only, threshold -64 dBm ✅ | PASS |
| FIFO_STATUS | `fifo_status.h` | 0x17 ✅ | 0x11 ✅ | Bits 6=TX_REUSE, 5=TX_FULL, 4=TX_EMPTY, 1=RX_FULL, 0=RX_EMPTY ✅ | Reset defaults tx_empty=true, rx_empty=true ✅ | PASS |
| DYNPD | `dynpd.h` | 0x1C ✅ | 0x00 ✅ | Bits 5:0 ✅ | Per-pipe DPL enable ✅ | PASS |
| FEATURE | `feature.h` | 0x1D ✅ | 0x00 ✅ | Bit 2=EN_DPL, 1=EN_ACK_PAY, 0=EN_DYN_ACK ✅ | All three enum classes ✅ | PASS |
| RX_PW_Px | `rx_pw.h` | 0x11–0x16 ✅ | 0x00 ✅ | Bits 5:0 ✅ | 0–32 bytes, pipe-specific address constants ✅ | PASS |

**Summary:** All register models are **datasheet-faithful**. Bit positions, reset values, encodings, and non-contiguous fields (DataRate) are correct.

---

## SPI Command Verification

Cross-referenced against nRF24L01+ PS v1.0, Table 16 (SPI Commands):

| Command | Code | Header Value | Match? |
|---------|------|-------------|--------|
| R_REGISTER | 0x00–0x1F | `cmd::R_REGISTER_BASE = 0x00` ✅ | ✅ |
| W_REGISTER | 0x20–0x3F | `cmd::W_REGISTER_BASE = 0x20` ✅ | ✅ |
| R_RX_PAYLOAD | 0x61 | `cmd::R_RX_PAYLOAD = 0x61` ✅ | ✅ |
| W_TX_PAYLOAD | 0xA0 | `cmd::W_TX_PAYLOAD = 0xA0` ✅ | ✅ |
| FLUSH_TX | 0xE1 | `cmd::FLUSH_TX = 0xE1` ✅ | ✅ |
| FLUSH_RX | 0xE2 | `cmd::FLUSH_RX = 0xE2` ✅ | ✅ |
| REUSE_TX_PL | 0xE3 | `cmd::REUSE_TX_PL = 0xE3` ✅ | ✅ |
| R_RX_PL_WID | 0x60 | `cmd::R_RX_PL_WID = 0x60` ✅ | ✅ |
| W_TX_PAYLOAD_NOACK | 0xB0 | `cmd::W_TX_PAYLOAD_NOACK = 0xB0` ✅ | ✅ |
| NOP | 0xFF | `cmd::NOP = 0xFF` ✅ | ✅ |
| W_ACK_PAYLOAD | 0xA8–0xAF | `cmd::W_ACK_PAYLOAD_BASE = 0xA8` ✅ | ✅ |

**All SPI commands match the datasheet.**

---

## Self-Audit Checklist

| Category | Checked? | Finding or PASS |
|----------|----------|-----------------|
| Build passes (`idf.py build` exit 0) | not run in this session | NOT VERIFIED — requires build |
| Typed enums (no raw integers in API) | yes | PASS — all register fields use `enum class` |
| Doxygen on new public symbols | yes | PASS — all structs, enums, functions have `@brief`, `@param`, `@return` |
| Datasheet fidelity (fields match) | yes | **PASS** — all register models verified field-by-field against nRF24L01+ PS v1.0 Table 28 |
| HAL decoupling (no platform headers in library) | yes | PASS — `nrf24l01plus` only includes `<cstdint>` and own headers |
| Reserved bits handled | partial | **FINDING:** `RfCh::to_byte()` masks `& 0x7F` (correct); `Status::to_byte()` does NOT mask bit 7 (acceptable for STATUS but inconsistent); `Config::to_byte()` omits reserved bit 7 (correct per datasheet "only 0 allowed") |
| No magic numbers in @code examples | yes | PASS — examples use typed constants |
| Buffer safety (bounded copies) | yes | PASS — `memcpy` bounds match; `len` is `uint8_t` (max 255) |
| AGENTS.md compliance | yes | PASS |
| Conventional commit ready | yes | PASS |

---

## Summary of All Findings

| ID | Severity | Description | File |
|----|----------|-------------|------|
| C-1 | **CRITICAL** | BLE ADV Access Address byte order reversed — nRF24 will not match any BLE packets | `ble_config.h:46` |
| C-2 | **CRITICAL** | MOSI GPIO direction change may break all post-init SPI transfers | `main.cpp:133` |
| H-1 | HIGH | Learning doc cites RF_SETUP reset value as 0x0F; datasheet and code say 0x0E | `nrf24-spi-basics.md:106` |
| H-2 | HIGH | No `whiten()` TX function exposed — dewhiten() only covers RX path | `ble.h` |
| M-1 | MEDIUM | No power-on delay before first SPI communication | `main.cpp` / `hal_espidf.cpp` |
| M-2 | MEDIUM | Config::to_byte() does not mask reserved bit 7 (correct but undocumented) | `config.h` |
| M-3 | MEDIUM | SPI device queue_size=1 without thread-safety documentation | `hal_espidf.cpp:29` |

---

## Verdict

```
VERDICT: REJECTED
DATASHEET REF: nRF24L01+ PS v1.0 §8.3.1 (LSByte-first register write), §7.3 (address byte order)
FINDINGS:
  C-1: ADV_ACCESS_ADDR bytes in MSByte-first order; nRF24 SPI requires LSByte-first.
       Current: {0x6B, 0x7D, 0x91, 0x71}. Correct: {0x71, 0x91, 0x7D, 0x6B}.
       Verified against Dmitry Grinberg reference implementation.
  C-2: gpio_set_direction(MOSI, INPUT) after SPI init may break all subsequent
       register reads/writes in ble_sniffer_task. Platform-dependent; must verify.
  H-1: Learning doc incorrectly states RF_SETUP reset = 0x0F; datasheet says 0x0E.
  H-2: No whiten() function for TX path; dewhiten() only covers RX.
  M-1: No power-on delay (1.5–10.3ms per datasheet §6.1.7) before first SPI command.
  M-2: Reserved bit handling inconsistency in Config vs RfCh to_byte().
  M-3: Missing thread-safety documentation for EspIdfHal with queue_size=1.
ROUTING: C-1 and C-2 to code-architect for immediate fix. H-1 to docs-writer.
         M-1 to code-architect for startup delay addition. M-2 and M-3 advisory.
```
