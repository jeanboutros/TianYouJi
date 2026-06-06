# Learning Index

## FreeRTOS
- [Pinning a task to a specific core](freertos-task-pinning.md)

## ESP-IDF Basics
- [ESP32 firmware entry point and mandatory building blocks](esp32-app-main-basics.md)

## RF & Legal
- [RF Legal Boundaries: illegal code analysis and safety checklist](rf-legal-boundaries.md)

## NRF24L01+
- [SPI basics: wiring, register map, ESP-IDF driver](nrf24-spi-basics.md)
- [Point-to-point communication: pipes, addressing, auto-ACK](nrf24-point-to-point.md)
- [Passive spectrum scan: RPD register, channel energy map](nrf24-spectrum-scan.md)
- [BLE packet crafting: raw ADV_NONCONN_IND from NRF24](nrf24-ble-packet-crafting.md)
- [Register map: complete nRF24L01+ register reference](nrf24l01plus-register-map.md)
- [SPI address byte order: LSByte-first write convention, ADV_ACCESS_ADDR bug](nrf24-spi-address-byte-order.md)

## BLE Protocol
- [Data whitening on nRF24L01+: bugs found, correct Galois LFSR, seed derivation](ble-data-whitening-nrf24.md)
- [Ubertooth One as BLE test tool: USB commands, packet injection, dewhitening validation](ubertooth-ble-testing.md)

## Hardware Tools
- [Ubertooth One BLE testing: TX, sniffing, firmware validation](ubertooth-ble-testing.md)
- [Ubertooth update process, TX failure diagnosis, and nRF Sniffer cross-validation](ubertooth-update-and-diagnostics.md)

## BLE (ESP32 Bluetooth Stack)
- [BLE advertising with ESP-IDF: GAP API, non-connectable beacons](ble-advertising-esp32.md)

## C/C++ Basics
- [enum class and struct: design patterns for embedded library types](cpp-enum-class-and-struct.md)

## Debugging
- [MOSI SPI pin direction bug, register verification, and multi-agent review findings](mosi-spi-register-verification.md)
- [nRF24L01+ EN_AA/EN_CRC register write order trap: CRC override, 0xFE FIFO artifacts, clone detection](nrf24-enaa-encrc-override.md)
- [nRF24L01+ SPI communication failure & clone chip diagnostic: 3-stage test, clock speed, Si24R1](nrf24-spi-clone-chip-diagnostic.md)
