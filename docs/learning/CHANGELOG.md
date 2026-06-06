# Changelog

**Project:** ESP32 nRF24L01+ BLE Sniffer
**Date range:** 2026-06-05 – 2026-06-06
**Commit range:** `d3a5aae` → `HEAD`
**Stats:** 46 commits · 102 files changed · ~39,181 lines added · ~280 lines deleted

---

## 1. Critical Bug Fixes

These are root-cause fixes that resolved fundamental issues preventing the sniffer from functioning correctly.

### `2503eca` fix(ble): correct ADV_ACCESS_ADDR byte order — ROOT CAUSE of zero packet reception

**Impact:** The BLE advertising Access Address (`0x8E89BED6`) was being sent to the nRF24L01+ in MSByte-first SPI order instead of the LSByte-first order required by the datasheet (§8.3.1). Because the nRF24 also reads back registers in LSByte-first order, readback verification always passed — the wrong value was self-consistent. This meant the radio never matched any BLE advertising packets, resulting in a permanently empty RX FIFO. RPD still triggered (physical-layer RF detection), but no packets ever entered the FIFO. The fix applies the correct byte reversal chain: BLE on-air LSByte-first → per-byte bit swap → nRF24 SPI LSByte-first write. This was the single root cause blocking all packet reception from the beginning of the project.

### `82bbc45` fix(nrf24): correct BLE dewhiten() — add bit-swap, fix LFSR algorithm

**Impact:** Two independent bugs in the BLE data dewhitening routine: (1) the `swapbits()` per-byte bit reversal was missing — BLE uses LSBit-first, nRF24 expects MSBit-first, so every received byte was bit-reversed; (2) the Galois LFSR polynomial and feedback were incorrectly implemented, producing the wrong whitening sequence. The combination meant dewhitened output was pure noise — any packet that did reach the FIFO would have been discarded as gibberish. After this fix, dewhitened PDU headers match the BLE specification and advertising MAC addresses become readable.

### `5b61aff` fix(main): remove MOSI GPIO_MODE_INPUT bug that breaks SPI writes

**Impact:** The MOSI pin (GPIO7) was configured as `GPIO_MODE_INPUT` instead of `GPIO_MODE_OUTPUT` or `GPIO_MODE_INPUT_OUTPUT`. This meant the ESP32 could never drive data on the MOSI line — all SPI writes were electrically invalid. The nRF24L01+ received no commands, so every register read returned the power-on default (0x00 or 0xFF depending on register). This bug made the entire SPI communication path one-directional (reads only, and only POR-default registers). After the fix, all SPI register writes succeed and readback verification passes.

### `c87f7e8` fix(ble): reorder register writes — EN_AA=0 before CONFIG to prevent CRC override

**Impact:** The nRF24L01+ datasheet states: *"If EN_AA is set for any pipe, the EN_CRC bit in CONFIG is forced high."* The power-on default of EN_AA is `0x3F` (all pipes auto-ACK enabled). Writing CONFIG with `EN_CRC=0` while EN_AA was still `0x3F` caused the hardware to override EN_CRC to 1 internally. After subsequently clearing EN_AA to `0x00`, the stored CONFIG still had `EN_CRC=1`. The nRF24 then expected CRC in every received packet, but BLE uses CRC-24 (3 bytes), not nRF24's CRC-1/CRC-2. Every BLE packet failed CRC check and was silently discarded. Fix: write EN_AA=0x00 before CONFIG, so no forcing condition exists when EN_CRC is written.

---

## 2. Bug Fixes

| Commit | Scope | Description |
|--------|-------|-------------|
| `0a64f40` | nrf24 | Enforce typed vocabulary in all code and docs — eliminate raw `uint8_t` register addresses from public API |
| `3d77019` | nrf24 | Replace LFSR magic numbers with named constants in `ble.cpp` and Python reference |
| `ff42810` | nrf24 | Address memory-safety review findings F1/F3/F4 (buffer bounds, stack depth, DMA safety) |
| `4370714` | diag | Correct RF_CH POR expected value from `0x00` to `0x02` in `spi_comm_test` |
| `5b3689f` | diag | Mask EN_AA comparison to valid bits [5:0] in `spi_comm_test` Stage 2 |
| `0926f2d` | diag | Handle warm boot (registers not at POR defaults) in `spi_comm_test` Stage 1 |
| `17e599c` | ble | Add CE_HIGH and PWR_UP settling delay to `configure_rx()` — nRF24 requires ≥130 µs after PWR_UP |

---

## 3. Features

### Driver & Register API

| Commit | Scope | Description |
|--------|-------|-------------|
| `1b0910a` | nrf24 | Move `nrf24_diag` from `main/` into the library as a proper diagnostic module |
| `e26423e` | nrf24 | Add `Status::format()` helper for human-readable status register printing |
| `718e521` | nrf24 | Add `write_and_verify()` debug method and `format()` to all register structs |
| `13522ff` | driver | Add typed register overloads to Driver API — `write_reg(Config{})`, `read_reg(Config{})` |
| `2edb7d7` | driver | Make raw `uint8_t` overloads `private`, use typed API everywhere in public interface |

### BLE Protocol Support

| Commit | Scope | Description |
|--------|-------|-------------|
| `7b08feb` | ble | Add `ADV_EXT_IND` PDU type and typed `BleAdvPduType` enum |
| `ee9ea07` | ble | Add `adv_address()` and `format_address()` for proper AdvA extraction from advertising PDUs |
| `4b2f0a7` | nrf24 | Add 200 µs settling delay after `ce_high()` in `switch_channel()` for receiver settling |

### Diagnostics & Main Application

| Commit | Scope | Description |
|--------|-------|-------------|
| `87a9cf4` | diag | Add 3-stage SPI communication test and clone chip detection (Si24R1 vs genuine nRF24L01+) |
| `118a664` | main | Add BLE diagnostic output — RPD monitoring and raw FIFO hex dump |
| `491abea` | main | Add CE GPIO readback verification and improved diagnostic output |

---

## 4. Tests & Validation

| Commit | Scope | Description |
|--------|-------|-------------|
| `5589fb5` | ble | Add host-side unit tests for `swapbits()` and `dewhiten()` — validate bit reversal and Galois LFSR |
| `636a984` | nrf24l01plus | Add exhaustive compile-time round-trip `static_assert`s for all register structs (`from_byte`→`to_byte` identity) |
| `a64c6c6` | tools | Add end-to-end cross-validation test script — compare nRF24 captures against Ubertooth ground truth |
| `7d27bd8` | captures | Add nRF Sniffer cross-validation capture (5 s, 3488 packets) for baseline comparison |

---

## 5. Documentation

### Learning Guides (`docs/learning/`)

| Commit | File | Description |
|--------|------|-------------|
| `991bf7f` | `ble-data-whitening-nrf24.md` | BLE data whitening comprehensive guide with Galois LFSR algorithm, seed derivation, and bug analysis |
| `d1b606c` | `ubertooth-ble-testing.md` | Ubertooth One BLE testing guide — USB commands, packet injection, dewhitening validation |
| `7d7d267` | `mosi-spi-register-verification.md` | MOSI SPI pin direction bug analysis and multi-agent review findings |
| `747ac38` | `nrf24-enaa-encrc-override.md` | EN_AA/EN_CRC register write order trap — CRC override, 0xFE FIFO artifacts, clone detection |
| `f7cc75d` | `nrf24-spi-address-byte-order.md` | nRF24 SPI address byte order guide — LSByte-first write convention and ADV_ACCESS_ADDR bug |
| `a9c7b52` | `ubertooth-update-and-diagnostics.md` | Ubertooth firmware update process, TX failure diagnosis, and nRF Sniffer cross-validation guide |

### Pipeline & Governance (`docs/pipeline/`)

| Commit | File | Description |
|--------|------|-------------|
| `0ebb892` | `pipeline.md` | Multi-agent validation pipeline adapted for ESP32 nRF24L01+ project |
| `c261e05` | `TODO.md` | Update TODO with dewhiten fix status and pipeline log index |
| `a771308` | `logs/` | Challenger pass reviews 011–013 |
| `723a1e0` | `TODO.md` | Update TODO with challenger review results and MOSI bug finding |
| `c4b9178` | `logs/` | Ubertooth-based dewhitening validation test plan |
| `cc32977` | governance | Strengthen typed API rules after raw `uint8_t` register address oversight |
| `cda0479` | `agents.md` | Add EN_AA/EN_CRC register write order trap documentation |
| `7fb1b92` | `agents.md` | Add SPI address byte order and CE GPIO lessons |

### Project Documentation

| Commit | File | Description |
|--------|------|-------------|
| `3fbf063` | `README.md` | Comprehensive project README for nRF24L01+ BLE sniffer — setup, wiring, usage |

---

## 6. Infrastructure

| Commit | Scope | Description |
|--------|-------|-------------|
| `20072f4` | pipeline | Add `.opencode/` agents and skills for OpenCode workflow — director, architect, reviewers |
| `16086f0` | agents | Add memory-safety skill and agent, update agent configuration files |
| `eeebb73` | pipeline | Add memory-safety review log to pipeline logs |
| `7ae9722` | tools | Add Ubertooth BLE advertising transmitter script (Python, `ubertooth-util` based) |
| `8d146af` | tools | Add BLE diagnostic test script for automated RF validation |

---

## Summary of Impact

The 46 commits in this range represent the transition from a **non-functional sniffer** (zero packets received) to a **working BLE advertising receiver** with comprehensive diagnostics and validation. Three independent root-cause bugs were identified and fixed:

1. **MOSI pin misconfiguration** (`GPIO_MODE_INPUT`) — SPI writes were electrically impossible
2. **ADV_ACCESS_ADDR byte order** — LSByte-first SPI write convention violated, self-consistent readback made it invisible
3. **BLE dewhitening** — missing `swapbits()` and broken Galois LFSR produced noise from valid captures

A fourth critical issue — **EN_AA/EN_CRC write order** — would have silently re-enabled CRC even after the other bugs were fixed.

The project also established:
- A **typed register API** with compile-time safety (no raw `uint8_t` addresses in public API)
- **Compile-time round-trip static_asserts** for all register structs
- **Host-side unit tests** for bit manipulation and dewhitening
- **Cross-validation tooling** against Ubertooth ground truth
- A **multi-agent review pipeline** that caught several of these bugs before deployment

These changes are documented in detail across 6 learning guides, with the critical traps (byte order, CRC override, MOSI direction, dewhitening) preserved as permanent reference material to prevent recurrence.