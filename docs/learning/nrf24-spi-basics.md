# NRF24L01+ SPI Basics

## Overview

The NRF24L01+ is a 2.4 GHz ISM band transceiver controlled entirely via SPI. This document covers wiring, the SPI protocol, the NRF24 register map, and writing an ESP-IDF SPI driver component.

---

## 1. NRF24L01+ Architecture

### Block diagram (simplified)

```
┌─────────────────────────────────────────────┐
│                 NRF24L01+                    │
│                                             │
│  ┌─────────┐   ┌──────────┐   ┌─────────┐  │
│  │  SPI    │──▶│ Register │──▶│  RF     │  │
│  │Interface│   │   Bank   │   │Frontend │──── Antenna
│  └─────────┘   └──────────┘   └─────────┘  │
│       │              │                      │
│       ▼              ▼                      │
│  ┌─────────┐   ┌──────────┐                │
│  │  TX/RX  │   │  Packet  │                │
│  │  FIFOs  │   │  Engine  │                │
│  └─────────┘   └──────────┘                │
│                                             │
│  CE pin: activates TX/RX                    │
│  IRQ pin: interrupt output (active low)     │
└─────────────────────────────────────────────┘
```

### Operating modes

| Mode | CE | CONFIG.PWR_UP | CONFIG.PRIM_RX | Description |
|---|---|---|---|---|
| Power Down | x | 0 | x | Minimal current (900 nA). SPI still active |
| Standby-I | 0 | 1 | x | Low current (26 µA). Ready to transition |
| RX Mode | 1 | 1 | 1 | Listening for packets |
| TX Mode | 1 (pulse ≥10µs) | 1 | 0 | Transmits packet from TX FIFO |
| Standby-II | 1 | 1 | 0 | TX FIFO empty, CE still high |

### Key specs
- Frequency: 2400–2525 MHz (126 channels, 1 MHz apart)
- Data rates: 250 kbps, 1 Mbps, 2 Mbps
- TX power: -18, -12, -6, 0 dBm (NRF24L01+ without PA)
- FIFO: 3-level TX, 3-level RX (32 bytes each)
- 6 data pipes for multiceiver mode

---

## 2. SPI Protocol

### NRF24L01+ SPI characteristics
- **Mode**: CPOL=0, CPHA=0 (SPI Mode 0)
- **Max clock**: 10 MHz
- **Byte order**: MSB first
- **CSN**: Active LOW (chip select, active when LOW)
- **Data on MOSI**: Command byte first, then data bytes (MSByte first for multi-byte)
- **Data on MISO**: STATUS register always returned on first byte

### SPI command set

| Command | Binary | Hex | Description |
|---|---|---|---|
| R_REGISTER | `000A AAAA` | 0x00–0x1F | Read register at address AAAAA |
| W_REGISTER | `001A AAAA` | 0x20–0x3F | Write register at address AAAAA |
| R_RX_PAYLOAD | `0110 0001` | 0x61 | Read RX payload (1–32 bytes) |
| W_TX_PAYLOAD | `1010 0000` | 0xA0 | Write TX payload (1–32 bytes) |
| FLUSH_TX | `1110 0001` | 0xE1 | Flush TX FIFO |
| FLUSH_RX | `1110 0010` | 0xE2 | Flush RX FIFO |
| REUSE_TX_PL | `1110 0011` | 0xE3 | Reuse last TX payload |
| R_RX_PL_WID | `0110 0000` | 0x60 | Read RX payload width |
| W_ACK_PAYLOAD | `1010 1PPP` | 0xA8–0xAF | Write ACK payload for pipe PPP |
| W_TX_PAYLOAD_NOACK | `1011 0000` | 0xB0 | TX payload, no ACK requested |
| NOP | `1111 1111` | 0xFF | No operation, returns STATUS |

### Transaction format

```
CSN ─┐                                    ┌─── (high)
     └────────────────────────────────────┘
MOSI:  [Command Byte] [Data0] [Data1] ... [DataN]
MISO:  [STATUS]       [Reg0]  [Reg1]  ... [RegN]
```

Every SPI transaction returns the STATUS register on the first MISO byte. This gives you real-time info about:
- `RX_DR` (bit 6): Data ready in RX FIFO
- `TX_DS` (bit 5): Data sent (TX success)
- `MAX_RT` (bit 4): Maximum retransmits exceeded
- `RX_P_NO` (bits 3:1): Pipe number of received payload
- `TX_FULL` (bit 0): TX FIFO full

---

## 3. Register Map (essential registers)

| Addr | Name | Reset | Description |
|---|---|---|---|
| 0x00 | CONFIG | 0x08 | Power, RX/TX mode, CRC, interrupts |
| 0x01 | EN_AA | 0x3F | Enable auto-acknowledgment per pipe |
| 0x02 | EN_RXADDR | 0x03 | Enable RX addresses (pipes) |
| 0x03 | SETUP_AW | 0x03 | Address width: 01=3, 10=4, 11=5 bytes |
| 0x04 | SETUP_RETR | 0x03 | Auto retransmit delay and count |
| 0x05 | RF_CH | 0x02 | RF channel (0–125 → 2400–2525 MHz) |
| 0x06 | RF_SETUP | 0x0F | Data rate, TX power, LNA gain |
| 0x07 | STATUS | 0x0E | Status flags (R/W to clear) |
| 0x08 | OBSERVE_TX | — | Lost packets and retransmit count |
| 0x09 | RPD | — | Received Power Detector (carrier detect) |
| 0x0A | RX_ADDR_P0 | 0xE7E7E7E7E7 | Receive address pipe 0 (5 bytes) |
| 0x0B | RX_ADDR_P1 | 0xC2C2C2C2C2 | Receive address pipe 1 (5 bytes) |
| 0x10 | TX_ADDR | 0xE7E7E7E7E7 | Transmit address (5 bytes) |
| 0x11 | RX_PW_P0 | 0x00 | Payload width pipe 0 (1–32 bytes) |
| 0x17 | FIFO_STATUS | — | FIFO status flags |
| 0x1C | DYNPD | 0x00 | Enable dynamic payload per pipe |
| 0x1D | FEATURE | 0x00 | Dynamic payload, ACK payload, no-ACK TX |

### CONFIG register (0x00) bit layout

| Bit | Name | Description |
|---|---|---|
| 7 | — | Reserved |
| 6 | MASK_RX_DR | 1 = mask IRQ for RX_DR |
| 5 | MASK_TX_DS | 1 = mask IRQ for TX_DS |
| 4 | MASK_MAX_RT | 1 = mask IRQ for MAX_RT |
| 3 | EN_CRC | Enable CRC (forced high if EN_AA is set) |
| 2 | CRCO | CRC encoding: 0=1byte, 1=2bytes |
| 1 | PWR_UP | 1=power up, 0=power down |
| 0 | PRIM_RX | 1=RX mode, 0=TX mode |

### RF_SETUP register (0x06) bit layout

| Bit | Name | Description |
|---|---|---|
| 7 | CONT_WAVE | Enable continuous carrier transmit (**used by jammers — never enable**) |
| 6 | — | Reserved |
| 5 | RF_DR_LOW | 1=250kbps (with RF_DR_HIGH=0) |
| 4 | PLL_LOCK | Force PLL lock (test only) |
| 3 | RF_DR_HIGH | Data rate: [LOW,HIGH]: 00=1M, 01=2M, 10=250k |
| 2:1 | RF_PWR | TX power: 00=-18dBm, 01=-12dBm, 10=-6dBm, 11=0dBm |
| 0 | — | Obsolete (don't care) |

---

## 4. Wiring: ESP32 VSPI to NRF24L01+

```
ESP32 (VSPI)              NRF24L01+
─────────────             ─────────
GPIO 18 (SCK)  ──────────  SCK
GPIO 19 (MISO) ──────────  MISO (SO)
GPIO 23 (MOSI) ──────────  MOSI (SI)
GPIO 17 (CSN)  ──────────  CSN
GPIO 5  (CE)   ──────────  CE
3V3            ──────────  VCC (⚠️ NOT 5V!)
GND            ──────────  GND
               (optional)  IRQ → GPIO (not used initially)
```

**Critical notes:**
- NRF24L01+ is 3.3V ONLY — 5V will destroy it
- Add 10µF + 100nF capacitor across VCC/GND close to the module
- Keep wires short (< 10 cm) for reliable 8 MHz SPI
- CE and CSN are NOT part of the SPI bus — they are separate GPIO controls

---

## 5. ESP-IDF SPI Master Driver Walkthrough

### Step 1: Initialize the SPI bus

```c
#include "driver/spi_master.h"
#include "driver/gpio.h"

#define NRF_SPI_HOST  SPI3_HOST   // VSPI
#define PIN_MISO      19
#define PIN_MOSI      23
#define PIN_SCLK      18
#define PIN_CSN       17
#define PIN_CE        5

spi_bus_config_t bus_cfg = {
    .miso_io_num = PIN_MISO,
    .mosi_io_num = PIN_MOSI,
    .sclk_io_num = PIN_SCLK,
    .quadwp_io_num = -1,      // Not used
    .quadhd_io_num = -1,      // Not used
    .max_transfer_sz = 33,    // 1 cmd + 32 payload bytes max
};

ESP_ERROR_CHECK(spi_bus_initialize(NRF_SPI_HOST, &bus_cfg, SPI_DMA_DISABLED));
```

**Why no DMA?** NRF24 transactions are small (max 33 bytes). DMA adds overhead for small transfers. See [What is DMA?](#what-is-dma) below.

### Step 2: Add the NRF24 device

```c
spi_device_handle_t nrf_handle;

spi_device_interface_config_t dev_cfg = {
    .clock_speed_hz = 8 * 1000 * 1000,  // 8 MHz (max 10 MHz for NRF24)
    .mode = 0,                           // SPI Mode 0 (CPOL=0, CPHA=0)
    .spics_io_num = PIN_CSN,             // CSN pin managed by driver
    .queue_size = 1,                     // Only 1 transaction at a time
    .command_bits = 8,                   // NRF24 command is 1 byte
    .address_bits = 0,                   // No address phase
};

ESP_ERROR_CHECK(spi_bus_add_device(NRF_SPI_HOST, &dev_cfg, &nrf_handle));
```

### Step 3: Read a register

```c
uint8_t nrf24_read_reg(uint8_t reg) {
    spi_transaction_t t = {
        .cmd = (0x00 | (reg & 0x1F)),     // R_REGISTER command
        .length = 8,                       // Read 1 byte (8 bits)
        .flags = SPI_TRANS_USE_RXDATA,     // Store result in rx_data
    };
    ESP_ERROR_CHECK(spi_device_polling_transmit(nrf_handle, &t));
    return t.rx_data[0];
}
```

### Step 4: Write a register

```c
void nrf24_write_reg(uint8_t reg, uint8_t value) {
    spi_transaction_t t = {
        .cmd = (0x20 | (reg & 0x1F)),     // W_REGISTER command
        .length = 8,                       // Write 1 byte
        .flags = SPI_TRANS_USE_TXDATA,
        .tx_data = {value, 0, 0, 0},
    };
    ESP_ERROR_CHECK(spi_device_polling_transmit(nrf_handle, &t));
}
```

### Step 5: CE pin control

```c
void nrf24_ce_init(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PIN_CE),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(PIN_CE, 0);  // Start with CE low (standby)
}

void nrf24_ce_high(void) { gpio_set_level(PIN_CE, 1); }
void nrf24_ce_low(void)  { gpio_set_level(PIN_CE, 0); }
```

---

## What is DMA?

**DMA (Direct Memory Access)** is a hardware feature that lets peripherals transfer data directly to/from RAM without involving the CPU.

**Without DMA (CPU-driven):**
```
SPI peripheral → CPU → RAM
```
The CPU reads each byte from the SPI register and writes it to memory. The CPU is busy/blocked for the entire transfer.

**With DMA:**
```
SPI peripheral → DMA controller → RAM
```
The DMA controller handles the copy autonomously. The CPU is free to do other work and gets an interrupt when the transfer completes.

**Why NRF24 doesn't use DMA:** NRF24 transactions are at most 33 bytes (1 command byte + 32 payload bytes). Setting up a DMA transfer has fixed overhead — allocating descriptors, configuring the DMA engine — that outweighs the benefit for such tiny payloads. Letting the CPU handle it directly is faster.

DMA becomes worthwhile for large transfers: sending hundreds of bytes to a display, streaming audio samples, writing blocks to flash storage, etc.

---

## 6. Verification: Reading the STATUS Register

After power-on reset, the STATUS register (0x07) should read **0x0E**:
- Bits [6:4] = 000 (no interrupts pending)
- Bits [3:1] = 111 (RX FIFO empty indicator)
- Bit [0] = 0 (TX not full)

```c
void app_main(void) {
    // Init SPI bus + device + CE pin...
    
    uint8_t status = nrf24_read_reg(0x07);  // STATUS register
    printf("STATUS = 0x%02X (expected 0x0E)\n", status);
    
    uint8_t config = nrf24_read_reg(0x00);  // CONFIG register
    printf("CONFIG = 0x%02X (expected 0x08)\n", config);
}
```

If you see `STATUS = 0x0E` and `CONFIG = 0x08`, your SPI communication is working.

---

## 7. Common Problems

| Symptom | Cause | Fix |
|---|---|---|
| STATUS always 0x00 | Module not powered or bad VCC | Check 3.3V, add decoupling caps |
| STATUS always 0xFF | MISO not connected or floating | Check wiring, ensure SPI mode 0 |
| STATUS = 0x0E but writes don't stick | CSN timing issue | Reduce SPI clock to 1 MHz and test |
| Module works intermittently | Power supply noise | Add 10µF electrolytic + 100nF ceramic on VCC |
| ESP32 crashes on SPI init | Wrong SPI host (SPI1) | Use SPI2_HOST or SPI3_HOST only |

---

## 8. Component File Structure

```
components/nrf24l01/
├── CMakeLists.txt
├── include/
│   └── nrf24l01.h        ← Public API
└── nrf24l01.c            ← Implementation
```

**CMakeLists.txt:**
```cmake
idf_component_register(
    SRCS "nrf24l01.c"
    INCLUDE_DIRS "include"
    REQUIRES driver
)
```

---

## References

- [ESP-IDF SPI Master Driver](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/spi_master.html) *(verified 2026-06-04)*
- [NRF24L01+ Product Specification v1.0 (Nordic Semiconductor)](https://infocenter.nordicsemi.com/pdf/nRF24L01P_PS_v1.0.pdf) — Section 8: Register Map, Section 8.3: SPI Commands
- NRF24L01 datasheet available locally: `docs/datasheets/az087_c_20.pdf`
