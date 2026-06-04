# NRF24L01+ Point-to-Point Communication

## Overview

This document covers sending data between TWO NRF24L01+ modules connected to the same ESP32 via shared VSPI bus with separate CE/CSN pins.

---

## 1. Hardware Setup: Two Modules, One Bus

```
ESP32 VSPI Bus (shared)
═══════════════════════════════════════════
GPIO 18 (SCK)  ──┬── Module A: SCK
                  └── Module B: SCK
GPIO 19 (MISO) ──┬── Module A: MISO
                  └── Module B: MISO
GPIO 23 (MOSI) ──┬── Module A: MOSI
                  └── Module B: MOSI

Separate control lines:
GPIO 17 (CSN_A) ───── Module A: CSN
GPIO 5  (CE_A)  ───── Module A: CE
GPIO 4  (CSN_B) ───── Module B: CSN
GPIO 16 (CE_B)  ───── Module B: CE

Power:
3V3 ──┬── Module A: VCC (+ 10µF + 100nF caps)
      └── Module B: VCC (+ 10µF + 100nF caps)
GND ──┬── Module A: GND
      └── Module B: GND
```

**Key point:** Both modules share MOSI/MISO/SCK but have their OWN CSN pin. The SPI master driver handles chip-select automatically — when talking to device A, only CSN_A goes low.

---

## 2. Addressing and Pipes

### How NRF24 addressing works

Each NRF24 can listen on up to 6 "pipes" simultaneously. Each pipe has its own address (up to 5 bytes). When a TX module sends a packet, it targets a specific address — only the receiver with a matching pipe address will accept it.

```
Transmitter (Module A)                 Receiver (Module B)
┌──────────────────┐                  ┌──────────────────┐
│ TX_ADDR:         │     RF wave      │ RX_ADDR_P0:      │
│ 0xAABBCCDDEE     │ ─────────────▶   │ 0xAABBCCDDEE     │ ← Match! Accepts packet
│                  │                  │ RX_ADDR_P1:      │
│                  │                  │ 0x1122334455     │ ← No match, ignores
└──────────────────┘                  └──────────────────┘
```

### Address width

Set via `SETUP_AW` register (0x03):
- `0x01` = 3-byte addresses
- `0x02` = 4-byte addresses
- `0x03` = 5-byte addresses (default, recommended)

### Pipe specifics

| Pipe | Address register | Address bytes | Notes |
|---|---|---|---|
| 0 | RX_ADDR_P0 (0x0A) | 5 full bytes | Also used for auto-ACK on TX side |
| 1 | RX_ADDR_P1 (0x0B) | 5 full bytes | |
| 2 | RX_ADDR_P2 (0x0C) | 1 byte (MSBytes shared with P1) | |
| 3 | RX_ADDR_P3 (0x0D) | 1 byte (MSBytes shared with P1) | |
| 4 | RX_ADDR_P4 (0x0E) | 1 byte (MSBytes shared with P1) | |
| 5 | RX_ADDR_P5 (0x0F) | 1 byte (MSBytes shared with P1) | |

**Important for auto-ACK:** When transmitting, set `RX_ADDR_P0 = TX_ADDR` on the transmitter so it can receive the acknowledgment back.

---

## 3. Auto-Acknowledgment (Enhanced ShockBurst)

NRF24L01+ supports hardware auto-ACK:
1. TX sends packet
2. TX switches to RX mode automatically and listens for ACK
3. RX receives packet → automatically sends ACK back
4. If TX doesn't get ACK within timeout → retransmit

### Configuration

```c
// Enable auto-ACK on pipe 0 (register 0x01)
nrf24_write_reg(0x01, 0x01);  // EN_AA: only pipe 0

// Set retransmit: 500µs delay, 3 retries (register 0x04)
// ARD = 0001 (500µs), ARC = 0011 (3 retries)
nrf24_write_reg(0x04, 0x13);
```

### SETUP_RETR register (0x04)

| Bits | Name | Values |
|---|---|---|
| 7:4 | ARD | Auto Retransmit Delay: (N+1) × 250µs. 0000=250µs, 0100=1250µs, 1111=4000µs |
| 3:0 | ARC | Auto Retransmit Count: 0000=disabled, 0001=1, ..., 1111=15 |

---

## 4. Communication Flow: TX Side

```c
void nrf24_tx_setup(spi_device_handle_t handle, const uint8_t *addr) {
    // 1. Power down first
    nrf24_write_reg(0x00, 0x08);  // CONFIG: PWR_UP=0, CRC on

    // 2. Set TX address (5 bytes)
    nrf24_write_multi(0x10, addr, 5);  // TX_ADDR

    // 3. Set RX_ADDR_P0 = TX_ADDR (for auto-ACK)
    nrf24_write_multi(0x0A, addr, 5);  // RX_ADDR_P0

    // 4. Set channel
    nrf24_write_reg(0x05, 76);   // RF_CH: channel 76 (2476 MHz)

    // 5. Set data rate and power
    nrf24_write_reg(0x06, 0x06); // RF_SETUP: 1Mbps, 0dBm

    // 6. Enable auto-ACK on pipe 0
    nrf24_write_reg(0x01, 0x01); // EN_AA

    // 7. Set payload size for pipe 0
    nrf24_write_reg(0x11, 4);    // RX_PW_P0: 4 bytes

    // 8. Set retransmit
    nrf24_write_reg(0x04, 0x13); // SETUP_RETR: 500µs, 3 retries

    // 9. Power up as TX
    nrf24_write_reg(0x00, 0x0E); // CONFIG: PWR_UP=1, PRIM_RX=0, CRC=2bytes
    vTaskDelay(pdMS_TO_TICKS(2)); // 1.5ms power-up delay
}

bool nrf24_send(spi_device_handle_t handle, const uint8_t *data, uint8_t len) {
    // 1. Write payload to TX FIFO
    nrf24_write_payload(data, len);  // W_TX_PAYLOAD command (0xA0)

    // 2. Pulse CE high for ≥10µs to trigger transmission
    nrf24_ce_high();
    esp_rom_delay_us(15);
    nrf24_ce_low();

    // 3. Wait for TX_DS or MAX_RT
    vTaskDelay(pdMS_TO_TICKS(5));
    uint8_t status = nrf24_read_reg(0x07);

    // 4. Clear flags
    nrf24_write_reg(0x07, 0x70);  // Clear all IRQ flags

    if (status & 0x20) {  // TX_DS bit set
        return true;      // Success!
    }
    if (status & 0x10) {  // MAX_RT bit set
        nrf24_flush_tx(); // Flush the failed payload
        return false;     // Failed after retries
    }
    return false;
}
```

---

## 5. Communication Flow: RX Side

```c
void nrf24_rx_setup(spi_device_handle_t handle, const uint8_t *addr) {
    // 1. Power down first
    nrf24_write_reg(0x00, 0x08);

    // 2. Set RX address pipe 0
    nrf24_write_multi(0x0A, addr, 5);  // RX_ADDR_P0

    // 3. Enable pipe 0
    nrf24_write_reg(0x02, 0x01);  // EN_RXADDR: pipe 0

    // 4. Set channel (must match TX)
    nrf24_write_reg(0x05, 76);    // RF_CH: channel 76

    // 5. Set data rate (must match TX)
    nrf24_write_reg(0x06, 0x06);  // RF_SETUP: 1Mbps, 0dBm

    // 6. Enable auto-ACK on pipe 0
    nrf24_write_reg(0x01, 0x01);

    // 7. Set payload size
    nrf24_write_reg(0x11, 4);     // RX_PW_P0: 4 bytes

    // 8. Power up as RX
    nrf24_write_reg(0x00, 0x0F);  // CONFIG: PWR_UP=1, PRIM_RX=1, CRC=2bytes
    vTaskDelay(pdMS_TO_TICKS(2));

    // 9. Set CE high to start listening
    nrf24_ce_high();
}

bool nrf24_data_available(void) {
    uint8_t status = nrf24_read_reg(0x07);
    return (status & 0x40) != 0;  // RX_DR bit
}

void nrf24_read_payload(uint8_t *buf, uint8_t len) {
    // R_RX_PAYLOAD command (0x61) + read len bytes
    nrf24_read_multi(0x61, buf, len);

    // Clear RX_DR flag
    nrf24_write_reg(0x07, 0x40);
}
```

---

## 6. Putting It Together: Two Modules on One ESP32

Since both modules share the SPI bus, we register two SPI devices:

```c
spi_device_handle_t nrf_tx;  // Module A (CSN=GPIO17, CE=GPIO5)
spi_device_handle_t nrf_rx;  // Module B (CSN=GPIO4, CE=GPIO16)

void init_both_modules(void) {
    // Initialize shared SPI bus (once)
    spi_bus_config_t bus_cfg = {
        .miso_io_num = 19,
        .mosi_io_num = 23,
        .sclk_io_num = 18,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 33,
    };
    spi_bus_initialize(SPI3_HOST, &bus_cfg, SPI_DMA_DISABLED);

    // Add Module A (TX)
    spi_device_interface_config_t dev_a = {
        .clock_speed_hz = 8000000,
        .mode = 0,
        .spics_io_num = 17,   // CSN_A
        .queue_size = 1,
        .command_bits = 8,
    };
    spi_bus_add_device(SPI3_HOST, &dev_a, &nrf_tx);

    // Add Module B (RX)
    spi_device_interface_config_t dev_b = {
        .clock_speed_hz = 8000000,
        .mode = 0,
        .spics_io_num = 4,    // CSN_B
        .queue_size = 1,
        .command_bits = 8,
    };
    spi_bus_add_device(SPI3_HOST, &dev_b, &nrf_rx);

    // Configure CE pins
    gpio_set_direction(5, GPIO_MODE_OUTPUT);   // CE_A
    gpio_set_direction(16, GPIO_MODE_OUTPUT);  // CE_B
}
```

### Main loop example

```c
void app_main(void) {
    init_both_modules();

    uint8_t addr[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE};

    nrf24_tx_setup(nrf_tx, addr);  // Module A = transmitter
    nrf24_rx_setup(nrf_rx, addr);  // Module B = receiver

    uint8_t msg[] = "Hi!";
    while (1) {
        bool ok = nrf24_send(nrf_tx, msg, 4);
        printf("TX %s\n", ok ? "OK" : "FAIL");

        if (nrf24_data_available()) {
            uint8_t buf[4];
            nrf24_read_payload(buf, 4);
            printf("RX: %s\n", buf);
        }

        vTaskDelay(pdMS_TO_TICKS(1000));  // Once per second
    }
}
```

---

## 7. Key Parameters That Must Match

| Parameter | Register | TX value must equal RX value |
|---|---|---|
| RF Channel | RF_CH (0x05) | Yes — must be identical |
| Data Rate | RF_SETUP (0x06) bits 5,3 | Yes — must be identical |
| Address Width | SETUP_AW (0x03) | Yes — must be identical |
| Address | TX_ADDR / RX_ADDR_Px | TX_ADDR must equal one of receiver's pipe addresses |
| Payload Size | RX_PW_Px (0x11–0x16) | Must match (or use dynamic payloads) |
| CRC | CONFIG (0x00) bits 3:2 | Yes — must be identical |

---

## 8. Dynamic Payloads (Optional)

Instead of fixed payload sizes, you can enable dynamic payloads:

```c
// Enable dynamic payload feature
nrf24_write_reg(0x1D, 0x04);  // FEATURE: EN_DPL=1
nrf24_write_reg(0x1C, 0x01);  // DYNPD: enable on pipe 0
```

With dynamic payloads:
- TX can send 1–32 bytes per packet
- RX reads payload width with `R_RX_PL_WID` (command 0x60) before reading data
- No need to pad short messages

---

## 9. Verification

**Success criteria:**
- Module A sends "Hello" → Module B receives "Hello" 
- STATUS on TX shows TX_DS (bit 5 set) after successful send
- STATUS on RX shows RX_DR (bit 6 set) when data arrives
- OBSERVE_TX (0x08) shows retransmit count — should be 0 or low

**Debug tips:**
- If TX always shows MAX_RT: receiver is not listening, or channel/address/rate mismatch
- If RX never gets data: check that CE_B is HIGH (RX mode active)
- Use OBSERVE_TX to see if packets are being lost

---

## References

- [ESP-IDF SPI Master Driver](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/spi_master.html) *(verified 2026-06-04)*
- [NRF24L01+ Product Specification v1.0](https://infocenter.nordicsemi.com/pdf/nRF24L01P_PS_v1.0.pdf) — Chapter 7: Data and Control Interface
- NRF24L01 datasheet: `docs/datasheets/az087_c_20.pdf`
