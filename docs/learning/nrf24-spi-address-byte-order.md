# nRF24L01+ SPI Address Register Byte Order — LSByte First

> **Note:** Code examples in this document use the typed library API (`nrf24::ble::ADV_ACCESS_ADDR`, `nrf24::reg::RX_ADDR_P0`) for production-quality code. Where raw hex values appear, they are annotated with their meaning — never use raw hex in production code.

## 1. What Was Learned

The nRF24L01+ SPI protocol writes multi-byte address registers **LSByte first** — the opposite of the on-air transmission order (MSByte first). This is stated explicitly in the nRF24L01+ Product Specification §8.3.1: *"LSByte is written first"* for multi-byte registers like `RX_ADDR_P0`, `RX_ADDR_P1`, and `TX_ADDR`.

When configuring the nRF24L01+ to receive BLE advertising packets, we compute the access address by:
1. Starting with the BLE advertising access address `0x8E89BED6`
2. Converting to on-air LSByte-first order (BLE convention)
3. Bit-swapping each byte (BLE LSBit-first → nRF24 MSBit-first)
4. Writing to the nRF24 SPI in LSByte-first order (nRF24 SPI convention)

**The bug:** Step 4 was wrong — the code originally wrote the bytes in MSByte-first order, which is the *on-air* order, not the *SPI write* order. This caused the nRF24L01+ to match the wrong 4-byte address, resulting in **zero BLE packets received for months**.

---

## 2. The Bug

The `ADV_ACCESS_ADDR` constant was originally defined as:

```cpp
// WRONG — MSByte-first SPI write order (matches on-air order, not SPI order)
inline constexpr uint8_t ADV_ACCESS_ADDR[4] = {0x6B, 0x7D, 0x91, 0x71};
```

The correct LSByte-first SPI write order is:

```cpp
// CORRECT — LSByte-first SPI write order (per nRF24L01+ datasheet §8.3.1)
inline constexpr uint8_t ADV_ACCESS_ADDR[4] = {0x71, 0x91, 0x7D, 0x6B};
```

### Why This Is Wrong

The nRF24L01+ SPI interface maps the **first data byte sent** to the register's **LSByte position** (bits 7:0 of the lowest address). When you write `{0x6B, 0x7D, 0x91, 0x71}` over SPI:

| SPI byte # | Value sent | Placed in register | What it should be |
|------------|-----------|-------------------|-------------------|
| 1 (first)  | `0x6B`    | LSByte (reg byte 0) | `0x71` (bit-swapped `0x8E`) |
| 2          | `0x7D`    | Byte 1             | `0x91` (bit-swapped `0x89`) |
| 3          | `0x91`    | Byte 2             | `0x7D` (bit-swapped `0xBE`) |
| 4 (last)   | `0x71`    | MSByte (reg byte 3) | `0x6B` (bit-swapped `0xD6`) |

The nRF24 stores `0x6B7D9171` instead of the correct `0x71917D6B`. On-air, the nRF24 transmits addresses MSByte-first, so the radio matches `0x6B 7D 91 71` (MSByte → LSByte) instead of the correct `0x71 91 7D 6B`. Since no BLE device transmits with the wrong access address, **no packets are ever received**.

---

## 3. Transformation Chain — End to End

The BLE advertising access address `0x8E89BED6` must go through four transformation steps to become the bytes sent over SPI:

### Step 1: BLE AA as a 32-bit value

```
BLE Advertising Access Address = 0x8E89BED6
  MSByte = 0x8E (byte 3)
  Byte 2 = 0x89
  Byte 1 = 0xBE
  LSByte = 0xD6 (byte 0)
```

### Step 2: BLE on-air byte order (LSByte first)

BLE transmits the LSByte of the access address first on air:

```
On-air byte 0 (first transmitted) = 0xD6  (LSByte)
On-air byte 1                    = 0xBE
On-air byte 2                    = 0x89
On-air byte 3 (last transmitted) = 0x8E  (MSByte)
```

Reference: Bluetooth Core Spec Vol 6 Part B §1.3.1 — LSBit-first transmission per byte, LSByte-first at the packet level for the access address.

### Step 3: Per-byte bit-swap (BLE LSBit-first → nRF24 MSBit-first)

The nRF24L01+ addresses are MSBit-first. BLE transmits LSBit-first. Each on-air byte must be bit-reversed:

```
swapbits(0xD6) = 0x6B    // 1101_0110 → 0110_1011
swapbits(0xBE) = 0x7D    // 1011_1110 → 0111_1101
swapbits(0x89) = 0x91    // 1000_1001 → 1001_0001
swapbits(0x8E) = 0x71    // 1000_1110 → 0111_0001
```

Result — nRF24 on-air address (MSByte first): **6B 7D 91 71**

### Step 4: SPI write order (LSByte first)

The nRF24L01+ datasheet §8.3.1 states: *"LSByte is written first."* The nRF24 on-air order is MSByte first, but SPI writes are LSByte first. So the bytes must be **reversed** for SPI:

```
nRF24 on-air (MSByte first):  6B 7D 91 71
                                  ↓ reverse byte order ↓
SPI write   (LSByte first):   71 91 7D 6B
```

### Summary Table

| Step | Representation | Byte Order |
|------|---------------|------------|
| BLE AA value | `0x8E89BED6` | MSByte first (conventional hex) |
| BLE on-air | `D6 BE 89 8E` | LSByte first |
| After bit-swap | `6B 7D 91 71` | LSByte first (same byte positions, bits flipped) |
| nRF24 on-air | `6B 7D 91 71` | MSByte first (nRF24 transmits addr MSByte first) |
| **SPI write** | **`71 91 7D 6B`** | **LSByte first** (datasheet §8.3.1) |

### Visual Diagram

```
BLE AA 0x8E89BED6
        │
        ▼  BLE on-air: LSByte first, LSBit first
    D6  BE  89  8E
        │
        ▼  Per-byte bit-swap (swapbits)
    6B  7D  91  71
        │
        ▼  nRF24 on-air: MSByte first, MSBit first
    6B  7D  91  71          ← this is what the nRF24 matches on air
        │
        ▼  SPI write: LSByte first (datasheet §8.3.1)
    71  91  7D  6B          ← this is what you send over SPI
```

---

## 4. Code Examples

### Correct: Using the library constant

```cpp
// The ADV_ACCESS_ADDR constant in nrf24::ble already stores the bytes
// in the correct LSByte-first SPI write order.
radio.write_reg_multi(nrf24::reg::RX_ADDR_P0,
                      nrf24::ble::ADV_ACCESS_ADDR, 4);
// Writes: 0x71  0x91  0x7D  0x6B  (LSByte → MSByte)
```

### Wrong: MSByte-first on-air order (the original bug)

```cpp
// WRONG — this is the on-air byte order, not the SPI write order
// The nRF24 would store 0x6B in the LSByte position, reversing the address
uint8_t wrong_addr[4] = {0x6B, 0x7D, 0x91, 0x71};
radio.write_reg_multi(nrf24::reg::RX_ADDR_P0, wrong_addr, 4);
```

### Manual derivation (for understanding, not production)

```cpp
// Step-by-step derivation of the SPI write order:
// 1. BLE AA: 0x8E (MSByte) 0x89 0xBE 0xD6 (LSByte)
// 2. On-air LSByte-first: D6 BE 89 8E
// 3. Per-byte bit-swap:    6B 7D 91 71
// 4. SPI LSByte-first:    71 91 7D 6B  ← reverse the on-air byte order
```

### Cross-reference with Dmitry Grinberg's reference

Dmitry Grinberg's "Bit-banging Bluetooth Low Energy" implementation writes:

```c
buf[0] = 0x30;            // W_REGISTER + RX_ADDR_P0 address (0x0A | 0x20)
buf[1] = swapbits(0x8E);  // 0x71 — LSByte first in SPI write
buf[2] = swapbits(0x89);  // 0x91
buf[3] = swapbits(0xBE);  // 0x7D
buf[4] = swapbits(0xD6);  // 0x6B — MSByte last in SPI write
nrf_manybytes(buf, 5);
```

Note that Grinberg's `0x8E` is written as `buf[1]` (the *first* SPI data byte), which maps to the register LSByte. This confirms the LSByte-first SPI convention: `swapbits(0x8E) = 0x71` goes into the LSByte position.

---

## 5. Why the Diagnostic Readback Passed

The `diag::verify_ble_rx()` function reads back `RX_ADDR_P0` and compares it against `ADV_ACCESS_ADDR`:

```cpp
uint8_t addr[4];
radio.read_reg_multi(nrf24::reg::RX_ADDR_P0, addr, 4);
bool rx_ok = (memcmp(addr, ble::ADV_ACCESS_ADDR, 4) == 0);
```

When the bug existed, `ADV_ACCESS_ADDR` was `{0x6B, 0x7D, 0x91, 0x71}`. The SPI write sent these four bytes in this exact order, so the nRF24 stored:
- Reg byte 0 (LSByte) = `0x6B`
- Reg byte 1 = `0x7D`
- Reg byte 2 = `0x91`
- Reg byte 3 (MSByte) = `0x71`

The SPI readback returns the register bytes in LSByte-first order too, so `addr` = `{0x6B, 0x7D, 0x91, 0x71}` — which matches `ADV_ACCESS_ADDR` exactly. **The diagnostic passed because the same wrong array was used for both writing and comparing.**

This is a textbook case of a **self-consistent error**: the data written and the expected data came from the same source, so the comparison could never fail. To catch this class of bug, the verification value must be derived **independently** — for example, by computing the expected register bytes from the BLE AA value `0x8E89BED6` using a different code path.

### How to Catch This Class of Bug

1. **Derive the expected value independently.** Instead of comparing against the same constant, derive the expected register bytes from the known BLE AA:
   ```cpp
   // Independently compute what the register should contain
   uint8_t expected[4] = {
       nrf24::ble::swapbits(0x8E),  // 0x71 — LSByte
       nrf24::ble::swapbits(0x89),  // 0x91
       nrf24::ble::swapbits(0xBE),  // 0x7D
       nrf24::ble::swapbits(0xD6),  // 0x6B — MSByte
   };
   bool rx_ok = (memcmp(addr, expected, 4) == 0);
   ```

2. **Test against known traffic.** Transmit a known BLE packet (e.g., via Ubertooth) and verify the nRF24 receives it. A register readback cannot catch byte-order errors that are self-consistent.

3. **Trace the full transformation chain manually.** As shown in §3 of this document, write out every step of the byte/bit transformation and verify that the final SPI bytes produce the correct on-air address.

---

## 6. Why the Earlier Challenger Review Was Dismissed Incorrectly

During the dual-model challenge, the challenger HW-engineer (log `012-challenger-hardware-engineer.md`) correctly identified the byte order as WRONG in finding **C-1**:

> *"C-1: ADV_ACCESS_ADDR bytes in MSByte-first order; nRF24 SPI requires LSByte-first."*

The primary review dismissed C-1, concluding in `mosi-spi-register-verification.md` §4.2:

> *"The challenger HW-engineer flagged this as a concern during the dual-model challenge, but detailed analysis confirmed the byte order is correct."*

This conclusion was **wrong**. The primary analysis traced the transformation chain but made a critical error at step 4: it stopped after computing the nRF24 on-air byte order (`6B 7D 91 71`) and assumed this was also the SPI write order. The additional reversal step — from on-air MSByte-first to SPI LSByte-first — was missed.

The primary analysis in §4.2 correctly computed:
- BLE AA → on-air LSByte-first → per-byte bit-swap → nRF24 on-air order `{0x6B, 0x7D, 0x91, 0x71}`

But then **incorrectly concluded** that this was also the SPI write order, without applying the final LSByte-first reversal mandated by datasheet §8.3.1.

### How the Error Propagated

| Step | Primary Analysis | Correct Analysis |
|------|-----------------|-----------------|
| BLE AA | `0x8E89BED6` | `0x8E89BED6` |
| On-air LSByte-first | `D6 BE 89 8E` | `D6 BE 89 8E` |
| Per-byte bit-swap | `6B 7D 91 71` | `6B 7D 91 71` |
| nRF24 on-air order | `6B 7D 91 71` ← stopped here | `6B 7D 91 71` (MSByte first) |
| **SPI write order** | Not computed | **`71 91 7D 6B`** (LSByte first) |
| **Conclusion** | Byte order "confirmed correct" | **Byte order was WRONG** |

### Lesson

> **When a challenger flags an issue backed by a datasheet citation, the burden of proof is on the primary reviewer.** The challenger cited §8.3.1 ("LSByte is written first") and Dmitry Grinberg's reference implementation. The primary dismissal relied on an incomplete transformation chain that stopped one step short. A claim that "detailed analysis confirmed" a result should include *every* step of the chain, with an explicit annotation of where byte order conventions change.

---

## 7. Pitfalls

### 7.1 Byte Order vs Bit Order — Two Independent Axes

The nRF24L01+ has **two** order conventions that compound:
1. **Bit order**: nRF24 is MSBit-first on air; BLE is LSBit-first → requires `swapbits()`
2. **Byte order**: nRF24 SPI writes LSByte-first; nRF24 on-air is MSByte-first → requires byte reversal

It is not sufficient to handle only the bit-swap. The byte-order reversal is equally important and is specified by the datasheet, not by intuition.

### 7.2 Self-Consistent Errors Evade Readback Verification

If you use the same constant to write and verify a register, the readback will always pass — even if the constant is wrong. This is because the nRF24 SPI read command also returns bytes in LSByte-first order, so the round-trip is self-consistent.

**Prevention:** Derive the expected value independently (from the BLE AA spec value) or validate against actual on-air traffic.

### 7.3 The "On-Air Order Looks Correct" Trap

After computing the nRF24 on-air address bytes `{0x6B, 0x7D, 0x91, 0x71}`, it is tempting to stop and say "this is what we need." And it *is* what the nRF24 needs to match on air — but it is NOT the order in which those bytes must be sent over SPI. The SPI write order is the **reverse** of the on-air order for multi-byte registers.

### 7.4 Dismissing Challenger Findings Without Completing the Chain

The challenger correctly identified the bug, but the primary review dismissed it. This happened because:
- The primary analysis traced 3 of 4 transformation steps
- The missing step (SPI LSByte-first) was the one that contradicted the expected result
- The conclusion "detailed analysis confirmed" was stated without completing the full chain

**Rule:** When disputing a challenger finding that cites a datasheet section, reproduce the challenger's argument step-by-step and explicitly address the cited section. If you cannot explain *why* the datasheet text does not apply, the challenger's finding stands.

---

## 8. Comparison with Other Multi-Byte Registers

The LSByte-first SPI convention applies to all 5-byte and multi-byte address registers on the nRF24L01+:

| Register | Address | Width | Byte Order |
|----------|---------|-------|-----------|
| `RX_ADDR_P0` | `0x0A` | 3–5 bytes | LSByte first |
| `RX_ADDR_P1` | `0x0B` | 3–5 bytes | LSByte first |
| `TX_ADDR` | `0x10` | 3–5 bytes | LSByte first |

All other registers are single-byte and unaffected by byte ordering.

For standard nRF24L01+ point-to-point communication (e.g., pipe addresses like `0xE7E7E7E7E7`), the LSByte-first convention means the first SPI byte is `0xE7` (LSByte), which is the same value as all other bytes. This makes the byte order invisible for symmetric addresses — which is why the byte-order convention is rarely noticed in normal ShockBurst usage.

The BLE access address `0x8E89BED6` has **all different bytes**, making the order reversal immediately visible. This is why the bug was specific to the BLE use case and would not manifest with standard nRF24 pipe addresses.

---

## 9. References

- nRF24L01+ Product Specification v1.0 §8.3.1 — Multi-byte register write order: *"LSByte is written first."* Local copy: `docs/datasheets/nRF24L01P_PS_v1.0.pdf`
- nRF24L01+ Product Specification v1.0 §7.3 — Packet format: *"MSB to the left"* (MSByte transmitted first on-air). Local copy: `docs/datasheets/nRF24L01P_PS_v1.0.pdf`
- Bluetooth Core Specification Vol 6 Part B §1.3.1 — Bit order: LSBit-first transmission. Available at: [https://www.bluetooth.com/specifications/specs/core-specification/](https://www.bluetooth.com/specifications/specs/core-specification/) *(verified 2026-06-06)*
- Dmitry Grinberg, "Bit-banging Bluetooth Low Energy": [http://dmitry.gr/?r=05.Projects&proj=11.%20Bluetooth%20LE%20fakery](http://dmitry.gr/?r=05.Projects&proj=11.%20Bluetooth%20LE%20fakery) *(verified 2026-06-06)* — Reference implementation confirms LSByte-first SPI write order for `swapbits(0x8E)` in `buf[1]`
- Challenger HW-engineer review log: `docs/pipeline/logs/012-challenger-hardware-engineer.md` — Finding C-1 (originally dismissed, now confirmed correct)
- Related learning docs:
  - [MOSI SPI register verification](mosi-spi-register-verification.md) — §4.2 corrected (byte order was WRONG)
  - [BLE Data Whitening on nRF24L01+](ble-data-whitening-nrf24.md) — swapbits() and dewhitening pipeline
  - [nRF24L01+ SPI Basics](nrf24-spi-basics.md) — SPI wiring and register map
  - [nRF24L01+ Register Map](nrf24l01plus-register-map.md) — register reference