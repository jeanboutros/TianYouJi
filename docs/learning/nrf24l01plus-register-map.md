# nRF24L01+ Register Map Reference

Source: nRF24L01+ Product Specification v1.0, Table 28

## Register Summary

| Addr | Mnemonic    | Reset  | Bytes | Description                        |
|------|-------------|--------|-------|------------------------------------|
| 0x00 | CONFIG      | 0x08   | 1     | Configuration Register             |
| 0x01 | EN_AA       | 0x3F   | 1     | Enable Auto Acknowledgment         |
| 0x02 | EN_RXADDR   | 0x03   | 1     | Enabled RX Addresses               |
| 0x03 | SETUP_AW    | 0x03   | 1     | Setup of Address Widths            |
| 0x04 | SETUP_RETR  | 0x03   | 1     | Setup of Automatic Retransmission  |
| 0x05 | RF_CH       | 0x02   | 1     | RF Channel                         |
| 0x06 | RF_SETUP    | 0x0E   | 1     | RF Setup Register                  |
| 0x07 | STATUS      | 0x0E   | 1     | Status Register                    |
| 0x08 | OBSERVE_TX  | 0x00   | 1     | Transmit Observe Register          |
| 0x09 | RPD         | 0x00   | 1     | Received Power Detector            |
| 0x0A | RX_ADDR_P0  | 0xE7E7E7E7E7 | 3–5 | Receive address pipe 0       |
| 0x0B | RX_ADDR_P1  | 0xC2C2C2C2C2 | 3–5 | Receive address pipe 1       |
| 0x0C | RX_ADDR_P2  | 0xC3   | 1     | Receive address pipe 2 (LSByte)    |
| 0x0D | RX_ADDR_P3  | 0xC4   | 1     | Receive address pipe 3 (LSByte)    |
| 0x0E | RX_ADDR_P4  | 0xC5   | 1     | Receive address pipe 4 (LSByte)    |
| 0x0F | RX_ADDR_P5  | 0xC6   | 1     | Receive address pipe 5 (LSByte)    |
| 0x10 | TX_ADDR     | 0xE7E7E7E7E7 | 3–5 | Transmit address               |
| 0x11 | RX_PW_P0    | 0x00   | 1     | Number of bytes in RX payload P0   |
| 0x12 | RX_PW_P1    | 0x00   | 1     | Number of bytes in RX payload P1   |
| 0x13 | RX_PW_P2    | 0x00   | 1     | Number of bytes in RX payload P2   |
| 0x14 | RX_PW_P3    | 0x00   | 1     | Number of bytes in RX payload P3   |
| 0x15 | RX_PW_P4    | 0x00   | 1     | Number of bytes in RX payload P4   |
| 0x16 | RX_PW_P5    | 0x00   | 1     | Number of bytes in RX payload P5   |
| 0x17 | FIFO_STATUS | 0x11   | 1     | FIFO Status Register               |
| 0x1C | DYNPD       | 0x00   | 1     | Enable Dynamic Payload Length       |
| 0x1D | FEATURE     | 0x00   | 1     | Feature Register                   |

---

## CONFIG (0x00) — Reset: 0x08

| Bit | Field       | Reset | Description                                       |
|-----|-------------|-------|---------------------------------------------------|
| 7   | Reserved    | 0     | Only '0' allowed                                  |
| 6   | MASK_RX_DR  | 0     | Mask RX_DR interrupt on IRQ pin. 1=masked         |
| 5   | MASK_TX_DS  | 0     | Mask TX_DS interrupt on IRQ pin. 1=masked         |
| 4   | MASK_MAX_RT | 0     | Mask MAX_RT interrupt on IRQ pin. 1=masked        |
| 3   | EN_CRC      | 1     | Enable CRC. Forced high if EN_AA enabled          |
| 2   | CRCO        | 0     | CRC encoding scheme. 0=1 byte, 1=2 bytes          |
| 1   | PWR_UP      | 0     | 1=Power Up, 0=Power Down                          |
| 0   | PRIM_RX     | 0     | 1=PRX (receiver), 0=PTX (transmitter)             |

---

## EN_AA (0x01) — Reset: 0x3F

| Bit | Field   | Reset | Description                          |
|-----|---------|-------|--------------------------------------|
| 7:6 | Reserved| 0     |                                      |
| 5   | ENAA_P5 | 1     | Enable auto-ack on pipe 5            |
| 4   | ENAA_P4 | 1     | Enable auto-ack on pipe 4            |
| 3   | ENAA_P3 | 1     | Enable auto-ack on pipe 3            |
| 2   | ENAA_P2 | 1     | Enable auto-ack on pipe 2            |
| 1   | ENAA_P1 | 1     | Enable auto-ack on pipe 1            |
| 0   | ENAA_P0 | 1     | Enable auto-ack on pipe 0            |

---

## EN_RXADDR (0x02) — Reset: 0x03

| Bit | Field   | Reset | Description                          |
|-----|---------|-------|--------------------------------------|
| 7:6 | Reserved| 0     |                                      |
| 5   | ERX_P5  | 0     | Enable data pipe 5                   |
| 4   | ERX_P4  | 0     | Enable data pipe 4                   |
| 3   | ERX_P3  | 0     | Enable data pipe 3                   |
| 2   | ERX_P2  | 0     | Enable data pipe 2                   |
| 1   | ERX_P1  | 1     | Enable data pipe 1                   |
| 0   | ERX_P0  | 1     | Enable data pipe 0                   |

---

## SETUP_AW (0x03) — Reset: 0x03

| Bit | Field    | Reset | Description                                    |
|-----|----------|-------|------------------------------------------------|
| 7:2 | Reserved | 0     |                                                |
| 1:0 | AW       | 11    | Address width: 01=3, 10=4, 11=5 bytes          |

---

## SETUP_RETR (0x04) — Reset: 0x03

| Bit | Field | Reset | Description                                         |
|-----|-------|-------|-----------------------------------------------------|
| 7:4 | ARD   | 0000  | Auto Retransmit Delay: (value+1)×250 µs (250–4000) |
| 3:0 | ARC   | 0011  | Auto Retransmit Count: 0=disabled, 1–15             |

ARD encoding: 0000=250µs, 0001=500µs, ..., 1111=4000µs

---

## RF_CH (0x05) — Reset: 0x02

| Bit | Field    | Reset   | Description                                  |
|-----|----------|---------|----------------------------------------------|
| 7   | Reserved | 0       |                                              |
| 6:0 | RF_CH    | 0000010 | Channel frequency = 2400 + RF_CH [MHz]       |

Valid range: 0–125 (2400–2525 MHz)

---

## RF_SETUP (0x06) — Reset: 0x0E

See [rf_setup.h](../../components/nrf24l01plus/include/nrf24l01plus/registers/rf_setup.h)

| Bit | Field      | Reset | Description                          |
|-----|------------|-------|--------------------------------------|
| 7   | CONT_WAVE  | 0     | Continuous carrier transmit          |
| 6   | Reserved   | 0     |                                      |
| 5   | RF_DR_LOW  | 0     | Data rate low bit (with bit 3)       |
| 4   | PLL_LOCK   | 0     | Force PLL lock (test only)           |
| 3   | RF_DR_HIGH | 1     | Data rate high bit (with bit 5)      |
| 2:1 | RF_PWR     | 11    | TX power: 00=-18, 01=-12, 10=-6, 11=0 dBm |
| 0   | Obsolete   | 0     | Don't care                           |

---

## STATUS (0x07) — Reset: 0x0E

| Bit | Field   | Reset | Description                                         |
|-----|---------|-------|-----------------------------------------------------|
| 7   | Reserved| 0     |                                                     |
| 6   | RX_DR   | 0     | Data Ready RX FIFO. Write 1 to clear.               |
| 5   | TX_DS   | 0     | Data Sent TX FIFO complete. Write 1 to clear.       |
| 4   | MAX_RT  | 0     | Max retransmits reached. Write 1 to clear.          |
| 3:1 | RX_P_NO | 111   | Pipe# for available RX payload. 111=empty           |
| 0   | TX_FULL | 0     | TX FIFO full: 1=full, 0=available                   |

---

## OBSERVE_TX (0x08) — Reset: 0x00 (read-only)

| Bit | Field    | Reset | Description                                    |
|-----|----------|-------|------------------------------------------------|
| 7:4 | PLOS_CNT | 0000  | Packets lost counter (max 15, sticky)          |
| 3:0 | ARC_CNT  | 0000  | Retransmit counter for current TX              |

---

## RPD (0x09) — Reset: 0x00 (read-only, nRF24L01+ only)

| Bit | Field    | Reset | Description                                    |
|-----|----------|-------|------------------------------------------------|
| 7:1 | Reserved | 0     |                                                |
| 0   | RPD      | 0     | Received Power Detector. 1 = signal > -64 dBm |

---

## FIFO_STATUS (0x17) — Reset: 0x11

| Bit | Field    | Reset | Description                                     |
|-----|----------|-------|-------------------------------------------------|
| 7   | Reserved | 0     |                                                 |
| 6   | TX_REUSE | 0     | Reuse last TX payload (set by REUSE_TX_PL cmd)  |
| 5   | TX_FULL  | 0     | TX FIFO full flag                               |
| 4   | TX_EMPTY | 1     | TX FIFO empty flag                              |
| 3:2 | Reserved | 0     |                                                 |
| 1   | RX_FULL  | 0     | RX FIFO full flag                               |
| 0   | RX_EMPTY | 1     | RX FIFO empty flag                              |

---

## DYNPD (0x1C) — Reset: 0x00

| Bit | Field  | Reset | Description                             |
|-----|--------|-------|-----------------------------------------|
| 7:6 | Reserved| 0    |                                         |
| 5   | DPL_P5 | 0     | Enable dynamic payload length pipe 5    |
| 4   | DPL_P4 | 0     | Enable dynamic payload length pipe 4    |
| 3   | DPL_P3 | 0     | Enable dynamic payload length pipe 3    |
| 2   | DPL_P2 | 0     | Enable dynamic payload length pipe 2    |
| 1   | DPL_P1 | 0     | Enable dynamic payload length pipe 1    |
| 0   | DPL_P0 | 0     | Enable dynamic payload length pipe 0    |

---

## FEATURE (0x1D) — Reset: 0x00

| Bit | Field      | Reset | Description                                  |
|-----|------------|-------|----------------------------------------------|
| 7:3 | Reserved   | 0     |                                              |
| 2   | EN_DPL     | 0     | Enables Dynamic Payload Length               |
| 1   | EN_ACK_PAY | 0     | Enables Payload with ACK                     |
| 0   | EN_DYN_ACK | 0     | Enables W_TX_PAYLOAD_NOACK command           |

---

## Multi-byte Registers (Addresses)

### RX_ADDR_P0 (0x0A) — Reset: 0xE7E7E7E7E7
5-byte receive address for pipe 0 (LSByte first in SPI).

### RX_ADDR_P1 (0x0B) — Reset: 0xC2C2C2C2C2
5-byte receive address for pipe 1 (LSByte first in SPI).

### RX_ADDR_P2–P5 (0x0C–0x0F) — Reset: 0xC3, 0xC4, 0xC5, 0xC6
1-byte each. Only the LSByte is unique; upper bytes are shared with P1.

### TX_ADDR (0x10) — Reset: 0xE7E7E7E7E7
5-byte transmit address (LSByte first in SPI). Used for PTX device only.

### RX_PW_P0–P5 (0x11–0x16) — Reset: 0x00
Bits 5:0 = number of bytes in RX payload (1–32). 0 = pipe not used.

---

## References

- nRF24L01+ Product Specification v1.0, Nordic Semiconductor (Table 28)
- https://github.com/nRF24/RF24/blob/master/nRF24L01.h
