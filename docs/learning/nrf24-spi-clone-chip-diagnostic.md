# nRF24L01+ SPI Communication Failure & Clone Chip Diagnostic

## What was learned

When an nRF24L01+ module does not respond correctly to SPI register writes (all
read-back values are wrong), the root cause can be one of several things:

1. **SPI clock too fast** — Clone chips (Si24R1, BK2425) often fail above 1–2 MHz
2. **Broken SPI bus** — wiring, power, or MISO stuck high
3. **Clone chip with non-standard register behaviour** — e.g., EN_CRC forcing persists

### Diagnostic approach: three-stage SPI test

The `nrf24::diag::spi_comm_test()` function implements a three-stage test that
runs **before** any register configuration:

| Stage | Purpose | Method |
|-------|---------|--------|
| 1. POR check | Verify SPI reads work | Read registers in default state, compare against datasheet POR values |
| 2. Write-verify | Verify SPI writes work | Write known patterns (0x00, 0x3F, 0x55, 0x2A) to EN_AA, read back each |
| 3. Clone detection | Detect Si24R1 clones | Set EN_AA=0, write CONFIG with EN_CRC=0, check if EN_CRC stays forced on |

### Key symptom patterns and their meanings

| Symptom | Meaning |
|---------|---------|
| STATUS reads 0xFF | MISO line stuck high — no device, or MISO not connected |
| STATUS reads 0x0E | SPI reads working, device is alive (POR default) |
| POR values all wrong but STATUS is OK | SPI writes not accepted — clock too fast for clone |
| EN_CRC forced on despite EN_AA=0x00 | Si24R1 clone chip |
| FIFO always returns 0xFE | Reading from empty FIFO — CRC rejection or no RX |

### SPI clock speed: 1 MHz for clone compatibility

Genuine nRF24L01+ supports SPI clock up to 10 MHz. Clone chips (Si24R1, BK2425)
typically need 1–2 MHz maximum. At 8 MHz (the original ESP-IDF HAL default),
clones silently ignore register writes, causing:

- All register read-backs showing POR defaults (writes never took effect)
- CONFIG read-back = 0x08 (POR default with EN_CRC=1) instead of 0x03
- EN_AA read-back = 0x3F (POR default) instead of 0x00

Reducing SPI clock from 8 MHz to 1 MHz fixed register write acceptance on
clone chips.

## Code examples

### Using the SPI communication test

```cpp
#include <nrf24l01plus/diag.h>

nrf24::EspIdfHal hal;
hal.init(pins);
nrf24::Driver radio(hal);

// Run BEFORE configure_rx()
bool spi_ok = nrf24::diag::spi_comm_test(radio);
if (!spi_ok) {
    printf("SPI failed — check wiring, power, module type\n");
    return;
}

// SPI confirmed working, configure for BLE
nrf24::ble::configure_rx(radio);
```

### Interpreting clone chip detection

If stage 3 reports "CLONE CHIP DETECTED":

```cpp
// The SPI bus works, but the module is likely an Si24R1 clone.
// Impact: EN_CRC cannot be disabled, which means BLE passive RX
// will not work (nRF24 rejects BLE packets that lack its expected CRC).
// Workaround: use a genuine Nordic nRF24L01+ module.
```

## Pitfalls

- **Do NOT skip the SPI comm test** — calling `configure_rx()` on a broken
  SPI bus wastes time and produces confusing diagnostic output
- **8 MHz SPI clock is too fast for clones** — the Si24R1 datasheet claims
  8 MHz compatibility but many production modules fail at this speed
- **0xFE FIFO reads are normal for empty FIFO** — the nRF24 returns 0xFE
  (POR default) when reading from an empty RX FIFO, not 0x00
- **Clone chips may lie about register values** — writing a value and
  reading back the POR default means the write was silently dropped

## References

- nRF24L01+ Product Specification v1.0, Table 28 — Register Map (POR values)
- Si24R1 clone chip discussion: <https://forum.arduino.cc/t/nrf24l01-si24r1-clone-detection/>
- ESP-IDF SPI Master Driver: <https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/spi_master.html>