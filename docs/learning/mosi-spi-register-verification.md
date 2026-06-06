# MOSI SPI Pin Direction Bug, Register Verification, and Multi-Agent Review Findings

## 1. Overview

This document captures findings from multi-agent review and debugging sessions on the ESP32 nRF24L01+ BLE sniffer project. The most critical finding is a bug where setting the MOSI GPIO pin to `GPIO_MODE_INPUT` after SPI initialization silently broke all SPI write operations. Additional findings cover BLE dewhitening bugs, access address byte order verification, register write-and-verify debugging, zero-packets diagnosis, memory safety fixes, and a summary of the challenger review process.

These findings demonstrate the value of structured multi-agent review: the dual-model challenge caught both true positives (the MOSI bug) and correctly resolved false positives (the access address concern).

---

## 2. MOSI GPIO_MODE_INPUT Bug (CRITICAL)

### 2.1 Finding

Setting `gpio_set_direction(mosi_pin, GPIO_MODE_INPUT)` after SPI initialization breaks all subsequent SPI write operations. The MOSI pin disconnects from the SPI peripheral, making all write commands silently fail.

### 2.2 Root Cause

On the ESP32, `gpio_set_direction()` calls `gpio_output_disable()` internally, which changes the pin's function select from the SPI peripheral (IOMUX) to GPIO mode. This disconnects the MOSI pin from the SPI hardware, so the nRF24L01+ never receives any command data on its SI (MOSI) input line.

The ESP-IDF SPI Master Driver documentation states that peripheral drivers handle the necessary I/O configuration for pins used as peripheral signal inputs or outputs (see [GPIO & RTC GPIO — ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/gpio.html)). Calling `gpio_set_direction()` on a pin already claimed by the SPI driver overwrites this configuration.

### 2.3 Symptoms

| Symptom | Explanation |
|---------|-------------|
| `switch_channel()` appears to work, but the nRF24L01+ doesn't actually change channels | The channel-switch command is sent on MOSI, which the radio never receives |
| `clear_irq_flags()` doesn't clear IRQ flags | The STATUS register write command is never received |
| Any register write after `gpio_set_direction()` is silently dropped | MOSI is disconnected from the SPI peripheral |
| Read operations may still return valid data | The nRF24L01+ always outputs STATUS on MISO during the command byte phase, even if the command on MOSI is garbled or absent |

### 2.4 Original Buggy Code

```cpp
/* Release MOSI to reduce 2.4 GHz noise during RX-only operation */
gpio_set_direction(hal.mosi_pin(), GPIO_MODE_INPUT);
printf("MOSI (GPIO%d) released to input -- RF noise reduced\n", hal.mosi_pin());
```

The intent was to reduce electrical noise on the MOSI line during RX-only operation, which is a reasonable concern for RF-sensitive designs. However, the ESP32 SPI driver does not support this pattern — once a pin is assigned to the SPI bus via `spi_bus_initialize()`, its direction must remain managed by the driver.

### 2.5 Fix

**Remove the `gpio_set_direction()` calls on MOSI entirely.** The ESP32 SPI peripheral manages pin direction automatically during transactions. After `spi_bus_initialize()` and `spi_bus_add_device()`, the ESP-IDF SPI driver controls the IOMUX routing and GPIO configuration for all SPI pins (MOSI, MISO, SCK, CSN).

```cpp
// REMOVED: gpio_set_direction(hal.mosi_pin(), GPIO_MODE_INPUT);
// The SPI driver manages MOSI pin direction automatically.
// Manually changing it disconnects the pin from the SPI peripheral.
```

### 2.6 Lesson

> **Never manually change GPIO direction on SPI pins after `spi_bus_initialize()`.** The ESP-IDF SPI driver handles IOMUX routing. Setting a SPI pin to `GPIO_MODE_INPUT` disconnects it from the SPI peripheral entirely, causing all write operations to silently fail.

For reducing RF noise during RX-only operation, the correct approaches are:

1. **Use `spi_bus_remove_device()` + `gpio_set_direction()` only if the SPI bus is no longer needed** — but this requires full re-initialization to resume.
2. **Let the SPI driver manage the pins** — the noise reduction benefit of tri-stating MOSI is minimal compared to the risk of breaking SPI communication.
3. **Add a series resistor on the MOSI trace** — this is a hardware-level fix that doesn't affect the SPI driver's ability to drive the pin.

### 2.7 References

- ESP-IDF SPI Master Driver: [https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/spi_master.html](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/spi_master.html) *(verified 2026-06-06)*
- ESP-IDF GPIO & RTC GPIO: [https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/gpio.html](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/gpio.html) *(verified 2026-06-06)*
- ESP32 Technical Reference Manual, Section "IO MUX and GPIO Matrix" *(local copy)*

---

## 3. BLE Dewhitening Algorithm (2 Bugs Fixed)

Two independent bugs existed in the `dewhiten()` function. Both must be fixed together; neither alone produces correct results.

### 3.1 Bug A — Missing Bit-Swap

**Finding:** nRF24L01+ sends data MSB-first per byte, but BLE expects LSB-first per byte. The `dewhiten()` function was missing a `swapbits()` call before the LFSR. Without this swap, every dewhitened byte is bit-reversed relative to what BLE expects.

**Evidence:** The nRF24L01+ Product Specification §7.3 states the packet format has "MSB to the left." The BLE specification (Vol 6 Part B §1.3.1) specifies LSBit-first transmission. These are opposite conventions, requiring a bit-swap step.

**Fix:** Call `swapbits()` on each byte before dewhitening. The library function `nrf24::ble::dewhiten()` now includes this step internally.

### 3.2 Bug B — Wrong LFSR Form and Seed

**Finding:** The original code used a Fibonacci LFSR with seed `(ch & 0x3F) | 0x40`. The correct implementation uses a Galois LFSR with seed `swapbits(ch) | 0x02` and polynomial `x^7 + x^4 + 1` (mask `0x11`). This matches the Bluetooth Core Spec and all 6 known reference implementations.

**Five differences between buggy and correct implementations:**

| Aspect | Buggy (Fibonacci) | Correct (Galois) |
|--------|-------------------|------------------|
| Shift direction | Right (`>>= 1`) | Left (`<<= 1`) |
| Feedback check | Bit 0 (`& 0x01`) | Bit 7 (`& 0x80`) |
| Feedback XOR | `0x48` | `0x11` |
| Seed | `(ch & 0x3F) \| 0x40` | `swapbits(ch) \| 2` |
| Bit-swap | **Missing** | Performed first |

### 3.3 Correct Algorithm

```cpp
/**
 * @code
 *   // Correct dewhitening: bit-swap first, then Galois LFSR
 *   void dewhiten(uint8_t *data, uint8_t len, uint8_t channel) {
 *       // Step 1: Swap bits (nRF24 MSB-first -> BLE LSB-first)
 *       for (uint8_t i = 0; i < len; i++)
 *           data[i] = nrf24::ble::swapbits(data[i]);
 *
 *       // Step 2: Galois LFSR dewhitening with correct seed
 *       uint8_t lfsr = nrf24::ble::swapbits(channel) | 0x02;
 *       for (uint8_t i = 0; i < len; i++) {
 *           for (uint8_t m = 1; m; m <<= 1) {
 *               if (lfsr & 0x80) {
 *                   lfsr ^= 0x11;  // polynomial x^7 + x^4 + 1
 *                   data[i] ^= m;
 *               }
 *               lfsr <<= 1;
 *           }
 *       }
 *   }
 * @endcode
 */
```

For the full analysis including step-by-step LFSR traces and reference implementation comparison, see `docs/learning/ble-data-whitening-nrf24.md`.

### 3.4 References

- Bluetooth Core Spec Vol 6 Part B §3.2 — Data Whitening
- nRF24L01+ Product Specification §7.3 — "MSB to the left"
- Dmitry Grinberg, "Bit-banging Bluetooth Low Energy": [http://dmitry.gr/?r=05.Projects&proj=11.%20Bluetooth%20LE%20fakery](http://dmitry.gr/?r=05.Projects&proj=11.%20Bluetooth%20LE%20fakery) *(verified 2026-06-06)*

---

## 4. Access Address Byte Order

### 4.1 Finding

The BLE advertising access address `{0x6B, 0x7D, 0x91, 0x71}` is correct. It is stored LSByte-first (matching nRF24L01+ SPI convention) with each byte bit-reversed per the BLE spec for on-air transmission.

**Do NOT change to {0x8E, 0x89, ...}** — that would represent the access address in native BLE format without the nRF24-required transformations.

### 4.2 Verification

The challenger HW-engineer flagged this as a concern during the dual-model challenge, but detailed analysis confirmed the byte order is correct. The reasoning:

1. The nRF24L01+ sends bytes LSByte-first over SPI and MSBit-first on air
2. BLE expects LSBit-first on air
3. Therefore, each byte must be bit-reversed for the nRF24 to transmit correctly
4. The current byte order accounts for both the LSByte-first SPI convention and the per-byte bit-reversal

The access address `0x8E89BED6` in BLE native format is stored as `{0x6B, 0x7D, 0x91, 0x71}` after applying:
- LSByte-first byte ordering (reversing byte order: `0xD6, 0xBE, 0x89, 0x8E`)
- Per-byte bit reversal (e.g., `0x8E` → `0x71`, `0x89` → `0x91`, `0xBE` → `0x7D`, `0xD6` → `0x6B`)

Resulting in `{0x6B, 0x7D, 0x91, 0x71}` — which matches the code.

### 4.3 Lesson

> **Bit-order and byte-order transformations compound.** When working across two protocols (nRF24 SPI and BLE on-air) with different bit- and byte-ordering conventions, always trace the transformation chain end-to-end. A single transformation step (byte swap OR bit swap) that appears "correct" in isolation may be wrong when combined with the other.

---

## 5. Register Write-and-Verify for Debugging

### 5.1 Finding

When debugging SPI communication issues, it is essential to verify that register writes actually "stick" — i.e., read back the same value that was written. Silent SPI write failures are particularly insidious in embedded systems because the SPI peripheral returns STATUS on MISO regardless of whether the MOSI command was received correctly.

### 5.2 Usage

```cpp
/**
 * @code
 *   // Verify a critical register write during development
 *   nrf24::Driver radio(hal);
 *   bool ok = radio.write_and_verify(nrf24::reg::CONFIG, nrf24::Config().to_byte());
 *   if (!ok) {
 *       printf("CONFIG write failed: expected 0x%02X, read back 0x%02X\n",
 *              nrf24::Config().to_byte(),
 *              radio.read_reg(nrf24::reg::CONFIG));
 *   }
 * @endcode
 */
```

### 5.3 Lesson

> **Always verify critical register writes during development**, even if you later remove the verification for production. The `write_and_verify()` pattern catches:
> - Disconnected MOSI (like the GPIO_MODE_INPUT bug)
> - Loose wiring or bad solder joints
> - SPI clock frequency too high for the slave
> - Wrong SPI mode (CPOL/CPHA)
> - Power supply issues causing intermittent resets

In production, you may remove these checks for performance, but during bring-up and debugging, they are indispensable.

### 5.4 Diagnostic: Zero-Packets Checklist

When all register read-backs PASS (CONFIG=0x03, RF_CH=0x02, EN_RXADDR=0x01, etc.) but `pkts=0` continuously, follow this diagnostic checklist:

| # | Check | Why It Matters |
|---|-------|---------------|
| 1 | CONFIG = 0x03 (PWR_UP=1, PRIM_RX=1, CRC disabled) | Radio must be powered up in RX mode for BLE sniffing |
| 2 | RF_CH matches expected BLE frequency | Channel must map correctly: ch37→RF_CH=2, ch38→RF_CH=26, ch39→RF_CH=80 |
| 3 | EN_RXADDR has pipe 0 enabled | RX pipe 0 must be enabled to receive |
| 4 | RX_ADDR_P0 = BLE access address | Must be `{0x6B, 0x7D, 0x91, 0x71}` (bit-reversed, LSByte-first) |
| 5 | RX_PW_P0 = 32 | Maximum payload width to capture full BLE packets |
| 6 | CE is HIGH during RX mode | nRF24L01+ requires CE HIGH to remain in RX mode |
| 7 | **MOSI pin is NOT set to GPIO_MODE_INPUT** | This was the root cause — disconnects MOSI from SPI |
| 8 | No SPI pin conflicts with ESP32 GPIO routing | Check for strapping pins, flash pins, etc. |
| 9 | BLE devices are actually advertising nearby | Use Ubertooth or phone BLE scanner app to confirm |
| 10 | nRF24L01+ antenna and power supply (3.3V, stable) | Add 10µF + 100nF decoupling caps near VCC/GND |

---

## 6. Ubertooth BLE TX Validation

### 6.1 Approach

Use the Ubertooth One to transmit known BLE advertising packets, then verify the ESP32 nRF24L01+ sniffer receives and decodes them correctly.

### 6.2 Tool

`tools/ubertooth_btle_tx.py` transmits ADV_IND packets on channels 37/38/39 with a configurable MAC address. The Ubertooth firmware handles CRC-24, data whitening, and access address insertion automatically.

### 6.3 Limitation

Ubertooth One cannot TX and RX simultaneously. For end-to-end testing, use:
- Two Ubertooths (one TX, one RX as ground truth)
- Or combine Ubertooth RX with ESP32 RX (both sniff the same over-the-air traffic)

For the full Ubertooth testing guide, see `docs/learning/ubertooth-ble-testing.md`.

---

## 7. Memory Safety Findings

From the memory-safety review of the nRF24l01+ library:

### 7.1 EspIdfHal Missing Virtual Destructor (Advisory)

**Finding:** The `EspIdfHal` class (platform adapter) has no virtual destructor. If polymorphic deletion were ever needed, this would cause undefined behavior.

**Risk:** Advisory — no polymorphic deletion occurs in practice. The HAL is constructed once and lives for the program's entire duration.

**Resolution:** Not blocking. Flagged for future hardening if the HAL allocation pattern changes.

### 7.2 `channel_at()` Missing Bounds Check → Undefined Behavior

**Finding:** The `channel_at()` function had no bounds check — accessing index >= `total_channels()` caused undefined behavior (out-of-bounds array access).

**Fix:** Added a guard that returns channel 37 (the first advertising channel) as a safe default when the index is out of range.

### 7.3 `RxConfig::total_channels()` uint8_t Overflow

**Finding:** If `extra_channel_count` exceeds 252, the addition `3 + extra_channel_count` wraps around (uint8_t is 0–255). This could silently produce an incorrect channel count.

**Fix:** Added `static_assert` to cap `extra_channel_count` at compile time, preventing the overflow.

### 7.4 No ASAN Flags in Test CMakeLists.txt

**Finding:** The host-side unit test CMakeLists.txt lacked AddressSanitizer flags, so memory errors in tests would not be caught.

**Fix:** Added `-fsanitize=address` for host-side unit tests.

---

## 8. Challenger Review Findings Summary

Three challenger reviews were completed (software-engineer, hardware-engineer, wireless-expert):

| ID | Challenger | Finding | Resolution |
|----|-----------|---------|------------|
| C-1 | HW-engineer | Access address byte order is wrong | **INCORRECT** — Verified correct after detailed analysis. The byte order `{0x6B, 0x7D, 0x91, 0x71}` accounts for both LSByte-first SPI ordering and per-byte bit reversal for BLE. |
| C-2 | HW-engineer | MOSI set to GPIO_MODE_INPUT breaks SPI writes | **FIXED** — Removed the offending `gpio_set_direction()` calls. The SPI driver manages pin direction. |
| C-3 | Wireless-expert | Dewhitening algorithm correctness | **APPROVED** — Correct Galois LFSR implementation with proper seed and bit-swap confirmed. |

### 8.1 The Dual-Model Challenge Process

The dual-model challenge caught:
- **True positive (C-2):** The MOSI bug was a real, critical failure that would have caused silent data loss.
- **False positive (C-1):** The access address concern was well-motivated but turned out to be incorrect after tracing the byte/bit transformation chain.

Both outcomes demonstrate the value of adversarial review: true positives prevent shipping bugs, and properly resolved false positives deepen understanding of the protocol.

### 8.2 Lesson

> **A challenger finding marked INCORRECT is not wasted effort.** Investigating C-1 led to a complete trace of the access address transformation chain, which produced a verified explanation that now serves as a permanent reference. The challenger's job is to find potential issues, not to be infallible.

---

## 9. Pitfalls and Lessons Learned

### 9.1 Never Manually Reconfigure GPIO Pins Claimed by Peripheral Drivers

The ESP-IDF peripheral drivers (SPI, I2C, UART, etc.) manage the GPIO configuration for the pins they use. Calling `gpio_set_direction()`, `gpio_output_enable()`, or `gpio_output_disable()` on those pins overwrites the driver's configuration and can silently break the peripheral.

**Rule:** After `spi_bus_initialize()` claims MOSI, MISO, SCK, and CSN pins, never call `gpio_set_direction()` or any GPIO configuration function on those pins until `spi_bus_free()` releases them.

### 9.2 Silent SPI Write Failures Are the Most Dangerous Kind

SPI write failures where the slave never receives the data but the master gets a normal-looking MISO response (STATUS byte) are particularly dangerous because:
- There is no SPI-level error indication
- Read operations may still work (MISO is still connected)
- The only symptom is subtle: registers don't hold their values, but this may not be obvious if you don't verify

**Rule:** During development, use `write_and_verify()` for all critical register writes. Remove in production only after extensive validation.

### 9.3 Bit Order and Byte Order Must Be Traced End-to-End

When bridging two protocols with different conventions (nRF24 MSB-first vs BLE LSB-first), you must trace the complete transformation chain:
1. BLE on-air format (LSBit-first per byte, native byte order)
2. nRF24 SPI format (MSB-first per byte, LSByte-first word order)
3. The relationship: each byte is bit-reversed, byte order is preserved

An isolated check of either convention can produce a false sense of correctness.

### 9.4 Dual-Model Review Catches Bugs That Single-Pass Review Misses

The MOSI bug (C-2) was caught by the challenger HW-engineer, not by the primary review. The access address concern (C-1) was a false positive, but investigating it strengthened the verification of the code's correctness.

**Rule:** Always run a challenger pass for non-trivial hardware or protocol code. The cost of an extra review cycle is far less than the cost of shipping a silent SPI failure.

### 9.5 Bounds Checks and Overflow Guards Are Not Optional

The `channel_at()` and `total_channels()` issues demonstrate that even "simple" accessor functions in embedded libraries need defensive guards. In embedded systems, undefined behavior (array out-of-bounds, integer overflow) doesn't throw an exception — it silently corrupts state.

**Rule:** Every array access in a public API must have a bounds check. Every arithmetic operation that could overflow must have a guard or static_assert.

---

## 10. References

- ESP-IDF SPI Master Driver: [https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/spi_master.html](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/spi_master.html) *(verified 2026-06-06)*
- ESP-IDF GPIO & RTC GPIO: [https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/gpio.html](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/gpio.html) *(verified 2026-06-06)*
- nRF24L01+ Product Specification v1.0 — §7.3 (packet format, MSB-first), §8 (register map). Local copy: `docs/datasheets/nRF24L01P_PS_v1.0.pdf`
- Bluetooth Core Specification Vol 6 Part B §1.3.1 (bit order), §3.2 (data whitening). Available at: [https://www.bluetooth.com/specifications/specs/core-specification/](https://www.bluetooth.com/specifications/specs/core-specification/) *(verified 2026-06-06)*
- Dmitry Grinberg, "Bit-banging Bluetooth Low Energy": [http://dmitry.gr/?r=05.Projects&proj=11.%20Bluetooth%20LE%20fakery](http://dmitry.gr/?r=05.Projects&proj=11.%20Bluetooth%20LE%20fakery) *(verified 2026-06-06)*
- Related learning docs:
  - [BLE Data Whitening on nRF24L01+](ble-data-whitening-nrf24.md) — complete dewhitening analysis
  - [NRF24L01+ SPI Basics](nrf24-spi-basics.md) — SPI wiring and register map
  - [NRF24L01+ Register Map](nrf24l01plus-register-map.md) — register reference
  - [Ubertooth BLE Testing](ubertooth-ble-testing.md) — Ubertooth TX/RX validation