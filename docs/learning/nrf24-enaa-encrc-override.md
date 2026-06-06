# nRF24L01+ EN_AA / EN_CRC Register Override Trap

## What Was Learned

The nRF24L01+ has a hardware-enforced dependency between the EN_AA (Enable Auto-Acknowledgment) register and the EN_CRC (Enable CRC) bit in CONFIG. The datasheet states:

> *"If the EN_AA is set for any pipe, the EN_CRC bit in the CONFIG register is forced high."*

This means that if ANY pipe has auto-ACK enabled (EN_AA bit = 1), the CRC is **unconditionally** enabled, regardless of what value you write to CONFIG.EN_CRC.

### The Bug

When configuring the nRF24L01+ for BLE passive reception, we need:
- EN_AA = 0x00 (no auto-ACK — we're sniffing, not participating)
- EN_CRC = 0 (BLE uses its own CRC-24, the nRF24's CRC-1/CRC-2 is incompatible)

The original code wrote CONFIG **before** EN_AA:

```cpp
// BROKEN ORDER
radio.write_reg(cfg);     // CONFIG: EN_CRC=0 — but EN_AA=0x3F (POR) → EN_CRC forced to 1!
radio.write_reg(en_aa);   // EN_AA = 0x00 — too late, CONFIG already stored with EN_CRC=1
```

At power-on, EN_AA defaults to 0x3F (all 6 pipes enabled). When CONFIG is written with EN_CRC=0 while EN_AA=0x3F, the hardware silently overrides EN_CRC to 1. The CONFIG register now stores 0x0B instead of 0x03.

After subsequently clearing EN_AA to 0x00, there's no longer a forcing condition — but CONFIG was already stored with EN_CRC=1, and **nobody re-writes CONFIG**. The nRF24 operates with CRC enabled, and BLE packets (which use a different CRC scheme) are rejected.

### The Fix

Write EN_AA = 0x00 **before** writing CONFIG:

```cpp
// CORRECT ORDER
radio.write_reg(en_aa);   // EN_AA = 0x00 — no forcing condition exists
radio.write_reg(cfg);     // CONFIG: EN_CRC=0 — accepted as-is
```

Then verify by reading CONFIG back:

```cpp
Config readback = radio.read_reg(Config{});
if (readback.crc_mode != CrcMode::Disabled) {
    // Warning: CRC still forced on — possible clone chip
}
```

## Code Examples

### Detecting the Symptom

| Symptom | Root Cause |
|---------|-----------|
| RX FIFO returns 0xFE for all 32 bytes | Reading from empty FIFO (CRC rejection → no packets) |
| RX_DR flag set but no valid data | Spurious interrupt or reading garbage from empty FIFO |
| RPD signal rare (<1%) | nRF24 can detect RF energy but can't demodulate |
| Dewhitened 0xFE × 32 produces fake PDU headers | Artifacts of dewhitening a constant pattern |
| CONFIG read-back shows 0x0B instead of 0x03 | EN_CRC forced on by EN_AA at write time |

### Clone Chip Detection

On genuine nRF24L01+, writing EN_AA=0x00 then CONFIG with EN_CRC=0 works correctly.
On Si24R1 clone chips, the CRC forcing may persist even after EN_AA=0x00.
Always read back CONFIG after writing and verify EN_CRC matches the intended value.

### FIFO_STATUS vs STATUS.RX_DR

When checking if data is available, prefer FIFO_STATUS.RX_EMPTY over STATUS.RX_DR:
- RX_DR can be set spuriously
- RX_DR may not reflect the actual FIFO state after flush_rx()
- Reading from an empty FIFO returns 0xFE (power-on default), not zero

```cpp
// PREFERRED — check FIFO_STATUS
auto fs = radio.read_reg(FifoStatus{});
if (!fs.rx_empty) { radio.read_payload(buf, 32); }

// LESS RELIABLE — check STATUS.RX_DR only
auto st = radio.read_reg(Status{});
if (st.rx_dr) { radio.read_payload(buf, 32); }  // may read from empty FIFO
```

## Pitfalls

1. **Never write CONFIG before EN_AA** — if EN_AA is non-zero, EN_CRC is forced on
2. **Always verify CONFIG read-back** — especially EN_CRC, after configuration
3. **Reading from an empty RX FIFO returns 0xFE** — not zeros, not an error code
4. **Dewhitened 0xFE produces fake but deterministic PDUs** — don't trust decoded data without checking raw FIFO contents
5. **The CONFIG override is silent** — no error flag, no status bit. You must read back to detect it
6. **Clone chips may override differently** — Si24R1 may persist the forcing even after EN_AA=0x00

## References

- nRF24L01+ Product Specification v1.0, §CONFIG register (page 54): EN_CRC forcing by EN_AA
- nRF24L01+ Product Specification v1.0, §6.1.2: State machine diagram and timing
- AGENTS.md: Register write order section (project-specific)