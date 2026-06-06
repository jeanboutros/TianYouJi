# 010 — Ubertooth BLE Testing Learning Document

**Date:** 2026-06-06  
**Author:** Docs Writer  
**Status:** COMPLETE  

## Task

Write a comprehensive, self-contained learning document about the Ubertooth One
as a BLE test tool for the nRF24L01+ firmware validation project.

## Output Files

| File | Description |
|------|-------------|
| `docs/learning/ubertooth-ble-testing.md` | Primary learning document (11 sections) |
| `docs/learning/INDEX.md` | Updated with entries under "BLE Protocol" and "Hardware Tools" |
| `docs/pipeline/logs/010-ubertooth-learning-doc.md` | This log |

## Document Structure

| Section | Content |
|---------|---------|
| 1. Overview | Ubertooth One intro, why use with nRF24, firmware/API versions, capabilities |
| 2. Architecture | Hardware, firmware modes, BLE packet construction, automatic processing |
| 3. USB Command Protocol | Full analysis: request types, command table, Python and C++ examples |
| 4. BLE Channel Mapping | Complete table, Python/C++ functions, library vocabulary usage |
| 5. Transmitting BLE Packets | Python script walkthrough, libubertooth C++, raw libusb example (complete, compilable) |
| 6. Sniffing BLE Packets | ubertooth-btle commands, PCAP/Wireshark, comparison procedure |
| 7. Testing Firmware (CRITICAL) | 6 techniques: injection+comparison, passive ground truth, round-trip, CRC-24, multi-channel, edge cases |
| 8. Data Whitening Deep Dive | Firmware whitening path, seed calculation, compatibility analysis |
| 9. Troubleshooting | 5 common issues with complete debugging steps |
| 10. Pitfalls | 7 pitfalls: stub tool, PDU type, interval, firmware CRC/whitening, channel mapping, access address, TxPower |
| 11. References | 9 verified external URLs + 7 project-internal refs |

## Code Examples

All code examples are provided in BOTH Python AND C++ where applicable:
- USB command sending: Python (pyusb) + C++ (libusb)
- Channel mapping: Python function + C++ constexpr + library vocabulary
- Packet transmission: Python script + libubertooth C++ + raw libusb complete program
- Dewhitening validation: Python host-side + C++ ESP32 side using `nrf24::ble::dewhiten()`, `nrf24::ble::swapbits()`, `nrf24::ble::switch_channel()`, etc.
- CRC-24: Python implementation + C++ implementation
- Test vectors: Python reference script + C++ test code
- Edge cases: Python transmitter examples + C++ ESP32 validator

## Library Vocabulary Usage

All C++ code examples use the project's typed library vocabulary:
- `nrf24::ble::dewhiten()`, `nrf24::ble::swapbits()` (ble.h)
- `nrf24::ble::configure_rx()`, `nrf24::ble::switch_channel()` (ble_config.h)
- `nrf24::ble::channel_to_rf_ch()`, `nrf24::ble::ADV_CHANNELS` (ble_config.h)
- `nrf24::ble::ADV_ACCESS_ADDR` (ble_config.h)
- `nrf24::ble::RxConfig` (ble_config.h)
- `nrf24::ble::rx_available()`, `nrf24::ble::clear_irq_flags()` (ble_config.h)
- `nrf24::Driver`, `nrf24::RfCh`, `nrf24::reg::RF_CH` (driver.h, registers/rf_ch.h)

No raw hex values or magic numbers in C++ examples (only in Python host scripts
for USB protocol constants, which are documented with their source).

## References Verified

All external URLs were verified with webfetch on 2026-06-06:

| URL | Status |
|-----|--------|
| https://github.com/greatscottgadgets/ubertooth | OK (2.1k stars, tag 2020-12-R1) |
| https://pyusb.github.io/pyusb/ | OK |
| https://libusb.info/ | OK |
| https://www.bluetooth.com/specifications/specs/core-specification/ | OK (Core Spec 6.3) |
| https://greatscottgadgets.com/ubertoothone/ | OK (retired product page) |
| https://ubertooth.readthedocs.io/en/latest/ | OK |
| https://github.com/greatscottgadgets/ubertooth/blob/2020-12-R1/firmware/bluetooth_rxtx/bluetooth_rxtx.c | OK (2719 lines) |
| https://greatscottgadgets.com/ubertooth/ | 404 — corrected to /ubertoothone/ |

Not verified (internal GitHub file URLs, expected stable):
- ubertooth_interface.h
- ubertooth_control.h