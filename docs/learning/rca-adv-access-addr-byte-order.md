# Root Cause Analysis: ADV_ACCESS_ADDR Byte Order Bug

## Executive Summary

The nRF24L01+ BLE sniffer project carried a critical bug for months: the `ADV_ACCESS_ADDR` constant was stored in MSByte-first on-air order `{0x6B, 0x7D, 0x91, 0x71}` instead of the LSByte-first SPI write order `{0x71, 0x91, 0x7D, 0x6B}` required by datasheet §8.3.1. A challenger HW-engineer correctly identified this bug in review 012, but a subsequent wireless-expert reviewer (review 013) dismissed the finding by stopping the transformation chain one step short — confirming the on-air byte order but not applying the final LSByte-first SPI reversal. The bug was then cemented in a learning doc. The root causes are: (1) a cognitive conflation of "on-air byte order" with "SPI write order," (2) a missing procedural safeguard requiring independent derivation of expected values when dismissing a challenger finding, and (3) insufficient escalation criteria when a challenger cites a specific datasheet section.

---

## 1. Timeline of Events

### 1.1 Bug Introduction

The `ADV_ACCESS_ADDR` constant was originally written as:

```cpp
inline constexpr uint8_t ADV_ACCESS_ADDR[4] = {0x6B, 0x7D, 0x91, 0x71};
```

This byte sequence represents the nRF24L01+ on-air address order (MSByte first), but the nRF24 SPI protocol requires LSByte-first ordering per datasheet §8.3.1. The developer computed the correct bit-swap values but stopped at step 3 of the transformation chain (on-air order) without completing step 4 (SPI write order).

### 1.2 Symptom: Zero Packets Received

The bug caused the nRF24 to match the inverted access address `0x71917D6B` on-air instead of the correct `0x6B7D9171`. Since no BLE device transmits with the wrong access address, the sniffer received zero valid packets for months. Diagnostic readback passed because the same wrong constant was used for both writing and verifying the register — a self-consistent error.

### 1.3 Review 012 — Challenger HW-Engineer (CORRECT)

**File:** `docs/pipeline/logs/012-challenger-hardware-engineer.md`

The challenger HW-engineer identified finding **C-1**:

> *"C-1: ADV_ACCESS_ADDR bytes in MSByte-first order; nRF24 SPI requires LSByte-first."*
> *"Current: {0x6B, 0x7D, 0x91, 0x71}. Correct: {0x71, 0x91, 0x7D, 0x6B}."*

The challenger:
- Cited datasheet §8.3.1: *"LSByte is written first"*
- Cited datasheet §7.3: MSByte transmitted first on-air
- Traced the complete transformation chain (BLE AA → on-air LSByte-first → bit-swap → nRF24 on-air MSByte-first → SPI LSByte-first)
- Cross-referenced Dmitry Grinberg's reference implementation showing `buf[1] = swapbits(0x8E) = 0x71` as the first SPI data byte
- Issued verdict: **REJECTED** — byte order must be fixed

This was a correct, well-evidenced finding.

### 1.4 Review 013 — Challenger Wireless Expert (WRONG — DISMISSED C-1)

**File:** `docs/pipeline/logs/013-challenger-wireless-expert.md`

The wireless-expert reviewer's §4 "Access Address Configuration — CORRECT ✓" concluded:

> *"Byte-by-byte reversal: {0x6B, 0x7D, 0x91, 0x71} — ✓"*
> *"Byte order: LSByte-first on air → nRF24 MSByte first — {0x6B,0x7D,0x91,0x71} — ✓"*

**The specific reasoning error (traced step-by-step):**

1. **Correct:** BLE AA `0x8E89BED6` → on-air LSByte-first: `D6 BE 89 8E`
2. **Correct:** Per-byte bit-swap: `0xD6→0x6B, 0xBE→0x7D, 0x89→0x91, 0x8E→0x71`
3. **Correct:** nRF24 on-air byte order (MSByte first): `6B 7D 91 71`
4. **WRONG — stopped here:** Concluded that `{0x6B, 0x7D, 0x91, 0x71}` is the correct SPI write order.

The reviewer explained:

> *"The array is ordered {first-on-air,...,last-on-air} which maps directly to nRF24 SPI write order (MSByte of address = first-on-air byte)."*

This is the logical error: **the nRF24 SPI write order is NOT the same as the on-air order.** The datasheet §8.3.1 explicitly states "LSByte is written first," meaning the first SPI data byte maps to the register's LSByte. The on-air order is MSByte first (§7.3). These are opposite conventions — the byte order must be **reversed** between on-air and SPI.

The correct step 4 should have been:
- nRF24 on-air: `6B 7D 91 71` (MSByte first)
- SPI write: `71 91 7D 6B` (LSByte first — reverse the on-air order)

### 1.5 Learning Doc Cemented the Wrong Answer

**File:** `docs/learning/mosi-spi-register-verification.md` (original §4.2)

The learning doc stated:

> *"The challenger HW-engineer flagged this as a concern during the dual-model challenge, but detailed analysis confirmed the byte order is correct."*

And then provided the same incomplete transformation chain as review 013 — steps 1-3 without step 4.

This learning doc was particularly damaging because:
- It was a **persistent reference document** that future reviewers would trust
- It explicitly dismissed the challenger's correct finding
- It presented the wrong answer with confidence ("confirmed the byte order is correct")
- It was the canonical reference for understanding the access address transformation

### 1.6 Bug Eventually Corrected

On 2026-06-06, the bug was finally identified and corrected. The `ADV_ACCESS_ADDR` was changed to `{0x71, 0x91, 0x7D, 0x6B}`, and both learning docs were updated with correction notes and the full transformation chain.

---

## 2. Root Causes

### 2.1 Technical Root Cause: Conflation of Two Distinct Byte-Order Conventions

The nRF24L01+ has **two different byte-order conventions** that apply simultaneously:

| Context | Byte Order | Reference |
|---------|-----------|-----------|
| **On-air transmission** | MSByte first | Datasheet §7.3: "MSB to the left" |
| **SPI register write** | LSByte first | Datasheet §8.3.1: "LSByte is written first" |

The error was treating these as the same convention — assuming that because the nRF24 is "MSB first," all byte orders are MSByte first. In reality, the SPI interface and the radio transmitter use **opposite** byte orders for multi-byte address registers.

This conflation is understandable because:
- For **symmetric addresses** (e.g., nRF24 default pipe addresses like `0xE7E7E7E7E7`), the byte order doesn't matter — all bytes are the same
- Most nRF24L01+ tutorials and example code use symmetric addresses
- The BLE access address `0x8E89BED6` has all different bytes, making this the first case where the order matters

### 2.2 Procedural Root Cause: Challenger Dismissal Without Completing the Evidentiary Chain

The challenger HW-engineer's finding C-1 cited:
1. Datasheet §8.3.1 ("LSByte is written first")
2. Datasheet §7.3 (MSByte transmitted first on-air)
3. Dmitry Grinberg's reference implementation

The wireless-expert reviewer dismissed C-1 without:
1. **Reproducing the challenger's step-by-step derivation** — the reviewer simply stated "byte-by-byte reversal: ✓" without showing the transformation chain
2. **Addressing the cited datasheet section** — §8.3.1 was not mentioned in the dismissal
3. **Addressing the reference implementation** — Grinberg's code showing `buf[1] = swapbits(0x8E) = 0x71` (first SPI byte) was not compared against the code's first byte `0x6B`

The pipeline had **no procedural requirement** that a reviewer dismissing a challenger finding must:
- Reproduce the challenger's argument step-by-step
- Explicitly address each cited piece of evidence
- Show the complete transformation chain

### 2.3 Cognitive Root Cause: Premature Confidence and Anchoring

The reviewer in 013 had already verified the dewhitening algorithm (LFSR, seed, bit-swap) as correct in the same review. This likely produced a confidence halo effect — having confirmed complex numerical correctness in one domain, the reviewer was more inclined to confirm the (seemingly simpler) byte order without the same rigor.

Additionally, the learning doc §4.2 showed a 3-step transformation (BLE native → on-air → bit-swap → result) that appeared complete but was missing the 4th step (SPI reversal). The 3 steps produced a result that "looks right" — each byte value is correct in isolation, and the byte order happens to match the nRF24 on-air order. This created a **false sense of completeness**: the transformation appeared exhaustive because it covered bit-reversal AND byte-order considerations, when in fact a second byte-order application was needed.

This is a form of **anchoring bias**: once `{0x6B, 0x7D, 0x91, 0x71}` was established as "the answer" in step 3, the reviewer anchored on it and interpreted step 4 as confirming the same value rather than recognizing it required reversal.

### 2.4 Systemic Root Cause: Self-Consistent Errors Evade Diagnostic Verification

The `diag::verify_ble_rx()` function compared the SPI readback against the same `ADV_ACCESS_ADDR` constant used for writing:

```cpp
uint8_t addr[4];
radio.read_reg_multi(nrf24::reg::RX_ADDR_P0, addr, 4);
bool rx_ok = (memcmp(addr, ble::ADV_ACCESS_ADDR, 4) == 0);
```

When the bug existed, this check always passed because:
- SPI write sends: `0x6B 0x7D 0x91 0x71` (LSByte-first → stores reversed in register)
- SPI read returns: `0x6B 0x7D 0x91 0x71` (LSByte-first → returns same byte sequence as sent)
- Comparison: `memcmp` matches because write and expected are the same constant

The diagnostic was a **tautology**: it verified that the SPI round-trip is self-consistent, not that the correct address was configured.

---

## 3. Detailed Analysis: The Logical Error in Review 013

### 3.1 The Reviewer's Argument

Review 013, §4, line 121-126:

```
| Byte-by-byte reversal | {0x6B, 0x7D, 0x91, 0x71} | `ADV_ACCESS_ADDR[4]` | ✓ |
| Byte order | LSByte-first on air → nRF24 MSByte first | {0x6B,0x7D,0x91,0x71} | ✓ |
```

The reviewer's explanation:

> *"BLE transmits 0xD6 first on air (LSByte of 0x8E89BED6). nRF24 receives MSbit-first, interpreting first 8 bits as byte with swapbits(0xD6)=0x6B. The array is ordered {first-on-air,...,last-on-air} which maps directly to nRF24 SPI write order (MSByte of address = first-on-air byte)."*

### 3.2 Where the Logic Breaks

The statement "which maps directly to nRF24 SPI write order" is the error. Let's trace why:

1. "nRF24 receives MSbit-first" — **correct** (§7.3)
2. "interpreting first 8 bits as byte with swapbits(0xD6)=0x6B" — **correct** (bit-swap)
3. "The array is ordered {first-on-air, ..., last-on-air}" — **correct description of the current array**
4. "which maps directly to nRF24 SPI write order" — **WRONG**

The reviewer conflated two things:
- The **on-air address byte order**, which IS `{0x6B, 0x7D, 0x91, 0x71}` (MSByte first)
- The **SPI write byte order**, which must be `{0x71, 0x91, 0x7D, 0x6B}` (LSByte first, per §8.3.1)

These are **reversed** from each other. The phrase "MSByte of address = first-on-air byte" is true for the on-air convention but false for the SPI convention. In the SPI write, the LSByte (0x71) must come first, not the MSByte (0x6B).

### 3.3 The Missing Step

The reviewer correctly computed:
- BLE AA: `0x8E89BED6`
- BLE on-air LSByte-first: `D6 BE 89 8E`
- After bit-swap: `6B 7D 91 71`
- nRF24 on-air (MSByte first): `6B 7D 91 71`

But then stopped. The correct chain continues:
- nRF24 SPI write (LSByte first, per §8.3.1): **`71 91 7D 6B`** (reverse the on-air order)

The reviewer skipped this final reversal because the on-air and SPI conventions happen to produce different orders, and the reviewer assumed they were the same.

### 3.4 Cross-Reference Check That Was Not Performed

Dmitry Grinberg's reference implementation (cited by the challenger in review 012) contains:

```c
buf[1] = swapbits(0x8E);  // 0x71 — LSByte first
buf[2] = swapbits(0x89);  // 0x91
buf[3] = swapbits(0xBE);  // 0x7D
buf[4] = swapbits(0xD6);  // 0x6B — MSByte last
```

Here, `buf[1]` (the first SPI data byte) is `0x71`, not `0x6B`. The code being reviewed had `ADV_ACCESS_ADDR[0] = 0x6B`. A simple comparison against the reference would have caught the discrepancy:

| Position | Grinberg | Code Under Review | Match? |
|----------|----------|-------------------|--------|
| Byte 0 (LSByte) | `0x71` | `0x6B` | **NO** |
| Byte 1 | `0x91` | `0x7D` | **NO** |
| Byte 2 | `0x7D` | `0x91` | **NO** |
| Byte 3 (MSByte) | `0x6B` | `0x71` | **NO** |

Every byte was in the wrong position — the array was entirely reversed. This cross-reference was not performed by review 013.

---

## 4. Why the Learning Doc Cemented the Wrong Answer

### 4.1 Structural Flaws in the Original §4.2

The learning doc `mosi-spi-register-verification.md` §4.2 contained the following claims:

1. *"The BLE advertising access address `{0x6B, 0x7D, 0x91, 0x71}` is correct."* — **False.**
2. *"It is stored LSByte-first (matching nRF24L01+ SPI convention)"* — **False. It was MSByte-first.**
3. *"The challenger HW-engineer flagged this as a concern during the dual-model challenge, but detailed analysis confirmed the byte order is correct."* — **False. The analysis was incomplete.**

### 4.2 Cognitive Biases at Play

**Availability bias:** The transformation chain (BLE AA → on-air → bit-swap → result) was already documented and appeared exhaustive. The reviewer did not consider that a fourth step might be missing because the three-step chain seemed complete.

**Authority bias:** The primary review had already "confirmed" the byte order, creating a default position that the challenger had to disprove rather than the other way around. This is backwards — in a dual-model challenge, the challenger's finding should require the primary to prove it wrong, not the other way around.

**Confirmation bias:** The reviewer in 013 was looking for confirmation of the existing byte order, not disconfirmation. The analysis traced the chain forward from the BLE AA to the existing code, not from the BLE AA to an independent derivation of what the SPI bytes should be.

### 4.3 Missing Cross-Check Independent Derivation

The correct way to verify the byte order would have been to derive the expected SPI bytes **independently** from the BLE AA value, without referencing the current code:

```
BLE AA:           0x8E89BED6
BLE on-air order: D6 BE 89 8E   (LSByte first)
After bit-swap:   6B 7D 91 71   (nRF24 MSBit-first per byte)
nRF24 on-air:     6B 7D 91 71   (MSByte first)
SPI write order:  71 91 7D 6B   (LSByte first, per §8.3.1)
```

Independent derivation produces `{0x71, 0x91, 0x7D, 0x6B}` — not what the code had. This check was never performed because the reviewer compared against the existing code rather than deriving from first principles.

---

## 5. Procedural Safeguards That Were Missing

### 5.1 No Requirement to Address Challenger Cited Evidence

The pipeline specification in `docs/pipeline/pipeline.md` and `docs/pipeline/agents.md` defines the Dual-Model Challenge protocol but does not require:
- Step-by-step reproduction of the challenger's argument
- Explicit addressal of each datasheet section cited by the challenger
- Independent derivation of the correct value before dismissing a challenger finding

**Recommendation:** Add a "Challenger Dismissal Protocol" that requires:
1. Reproduce the challenger's argument in full, step by step
2. For each cited reference (datasheet section, external implementation), either show it doesn't apply or show it supports the dismissal
3. Provide an independent derivation of the correct answer from first principles
4. If any cited evidence cannot be addressed, the finding CANNOT be dismissed

### 5.2 No Mandatory Cross-Reference Against Known Implementations

The challenger cited Dmitry Grinberg's reference implementation. The reviewer did not compare the code byte-by-byte against this reference.

**Recommendation:** When a challenger cites an external reference implementation, the primary reviewer MUST show a byte-by-byte (or field-by-field) comparison. If discrepancies exist, they must be explained before the finding can be dismissed.

### 5.3 Self-Consistent Verification Trap

The diagnostic `diag::verify_ble_rx()` compared the SPI readback against the same constant used for writing, making the test a tautology.

**Recommendation:** Require **independent derivation** for critical register values. Instead of:
```cpp
bool ok = (memcmp(addr, ADV_ACCESS_ADDR, 4) == 0);
```
Use:
```cpp
uint8_t expected[4] = {
    swapbits(0x8E),  // 0x71 — LSByte first (derived from BLE AA MSByte)
    swapbits(0x89),  // 0x91
    swapbits(0xBE),  // 0x7D
    swapbits(0xD6),  // 0x6B — MSByte last (derived from BLE AA LSByte)
};
bool ok = (memcmp(addr, expected, 4) == 0);
```

### 5.4 No Transformation Chain Completeness Check

The learning doc §4.2 showed a 3-step transformation chain that appeared complete. No checklist or template required verifying that all transformation steps (BLE → on-air → bit-swap → SPI) were accounted for.

**Recommendation:** Require a **transformation chain audit** for any multi-step protocol conversion. The audit should:
1. List every protocol conversion step
2. Identify the byte/bit order convention at each step
3. Show the transformation at each step with explicit intermediate values
4. Verify the final output against an independent reference

### 5.5 No "Second Opinion" Requirement for Challenger Dismissals

The pipeline allows a single reviewer (review 013) to dismiss a challenger finding without requiring a second opinion.

**Recommendation:** When a challenger finding is backed by datasheet citations and a reference implementation, the dismissal must be reviewed by **at least one additional specialist** before it can be rejected. The original challenger should also be given the chance to respond to the dismissal.

---

## 6. Recommendations

### 6.1 Procedural Changes

| ID | Recommendation | Addresses |
|----|---------------|-----------|
| P-1 | **Challenger Dismissal Protocol:** Require step-by-step reproduction of the challenger's argument before dismissal. Each cited evidence item must be explicitly addressed. | Root cause 2.2 |
| P-2 | **Independent Derivation Requirement:** When verifying a value, derive the expected result from first principles (spec values, not from existing code). Compare the derivation against the code. | Root causes 2.1, 2.4 |
| P-3 | **Cross-Reference Check:** When a challenger cites an external reference implementation, the reviewer must show a field-by-field comparison before dismissing. | Root cause 2.2 |
| P-4 | **Second Reviewer for Challenger Dismissals:** A challenger finding backed by datasheet citations must be confirmed by at least two reviewers before dismissal. | Root cause 2.5 |
| P-5 | **Transformation Chain Audit Template:** Require a structured template for multi-step protocol conversions that lists every step with its byte/bit order convention. | Root cause 2.3 |

### 6.2 Skill and Agent Updates

#### 6.2.1 Addition to `datasheet-verification` Skill

Add the following text to the `datasheet-verification` skill:

```markdown
## Multi-Step Transformation Audit

When verifying a value that involves multiple protocol transformations (e.g., BLE →
nRF24 on-air → nRF24 SPI), you MUST:

1. List every transformation step with its input and output values.
2. At each step, cite the specification section that defines the convention.
3. Show the complete chain with explicit intermediate values.
4. Verify the final result against an independent reference implementation.

Do NOT stop the chain early because an intermediate result "looks correct."
A value can look correct at one layer but be wrong at the next layer.

**Example (BLE Access Address → nRF24 SPI):**

| Step | Value | Convention | Cite |
|------|-------|-----------|------|
| BLE AA | 0x8E89BED6 | MSByte-first hex | Core Spec Vol 6 Part B §2.3 |
| On-air | D6 BE 89 8E | LSByte-first | Core Spec Vol 6 Part B §1.3.1 |
| After bit-swap | 6B 7D 91 71 | MSBit-first per byte | §7.3 "MSB to the left" |
| nRF24 on-air | 6B 7D 91 71 | MSByte first | §7.3 |
| **SPI write** | **71 91 7D 6B** | **LSByte first** | **§8.3.1** |

Stopping at step 4 (on-air) without computing step 5 (SPI) was the bug.
```

#### 6.2.2 Addition to `assumption-trap` Skill

Add the following common trap:

```markdown
### Byte/Bit Order Compounding Trap

When bridging two protocols with different conventions:
- **NEVER** assume that handling one convention (e.g., bit-swap) is sufficient.
- **ALWAYS** check whether BOTH conventions apply (e.g., bit-reversal AND byte-reversal).
- The number of independent order transformations equals the number of convention
  mismatches. Two mismatches (bit order + byte order) require TWO transformations,
  not one.

Self-consistent readback verification CANNOT detect byte-order errors where the
same wrong constant is used for both write and read. You MUST derive the expected
value independently from the specification, not from the constant being verified.
```

#### 6.2.3 Addition to Dual-Model Challenge Protocol

Add to `docs/pipeline/agents.md`, §"Dual-Model Challenge Protocol":

```markdown
### Challenger Dismissal Requirements

When dismissing a challenger finding, the primary reviewer MUST:

1. **Reproduce the challenger's argument** step by step, including all intermediate
   values and explicit citations.
2. **Address every cited reference** (datasheet section, external implementation,
   specification clause). For each, state whether it supports the dismissal or
   supports the challenger. If a cited reference cannot be addressed, the finding
   CANNOT be dismissed.
3. **Derive the correct value independently** from specification first principles,
   not from the existing code being reviewed.
4. **Show a complete transformation chain** for any value involving multiple
   protocol conversions. Do not stop at an intermediate step.

If the challenger cites a reference implementation (e.g., Dmitry Grinberg's BLE code),
the reviewer MUST show a byte-by-byte comparison between the reference and the code
under review before concluding that the code matches.

A finding dismissed without these steps should be escalated to a second reviewer or
the PM for independent verification.
```

#### 6.2.4 Addition to `self-audit-checklist` Skill

Add a new row to the mandatory checklist:

```markdown
| Transformation chain complete? | yes/no | [For multi-protocol values, list all steps with citations] |
```

And add to the "Datasheet fidelity" row guidance:

```markdown
### Datasheet fidelity (extended)
- For values that cross protocol boundaries (BLE ↔ nRF24 SPI), trace the FULL
  transformation chain. Cite the spec section at EVERY step, not just the final step.
- Verify against an independent reference implementation when available.
```

### 6.3 AGENTS.md Principles Violated and Strengthening Needed

#### 6.3.1 "Datasheet is source of truth" — Violated

The AGENTS.md states: *"Datasheet is the source of truth — Verify all hardware details against `docs/datasheets/` before coding."*

The reviewer did not verify the SPI write order against datasheet §8.3.1 when evaluating byte order. The section was cited by the challenger but not consulted by the dismissing reviewer.

**Strengthening:** Add to AGENTS.md:

```markdown
### Datasheet Verification for Challenger Findings

When a challenger cites a specific datasheet section, the primary reviewer MUST:
1. Open and read that specific section from the local datasheet in `docs/datasheets/`
2. Quote the relevant text in their analysis
3. Explain how it applies (or doesn't) to the finding being dismissed

It is NOT sufficient to say "detailed analysis confirmed" without citing and quoting
the specific datasheet text that supports the confirmation.
```

#### 6.3.2 "No-Assumption Protocol" — Violated

The assumption-trap protocol requires halting on ambiguity. The reviewer assumed that "nRF24 MSB first" applies to both on-air and SPI, which is an unstated assumption. The datasheet states different conventions for each interface, and the reviewer should have flagged the ambiguity instead of resolving it by assumption.

**Strengthening:** Add a specific assumption-trap trigger:

```markdown
### Compounding Convention Assumption Trap

When a single datasheet describes MULTIPLE interfaces (on-air, SPI, etc.), NEVER assume
the same convention applies to all interfaces without explicit verification. Each
interface may have its own byte/bit order convention. If the datasheet says "MSB first"
for the on-air interface, do not assume the SPI interface is also MSB first. Check
the SPI section separately.
```

#### 6.3.3 "Dual-Model Challenge" — Ineffective

The dual-model challenge is designed specifically to catch errors that a single reviewer misses. It caught the bug correctly (review 012), but the correction was dismissed by review 013. The pipeline had no mechanism to prevent a single reviewer from dismissing a well-evidenced challenger finding.

**Strengthening:** Already addressed in P-4 (second reviewer requirement). Additionally, add to `docs/pipeline/pipeline.md`:

```markdown
### Challenger Finding Protection

A challenger finding that is:
- Sevlevel CRITICAL or HIGH, AND
- Backed by at least one datasheet citation, AND
- References a specific code location and proposed fix

CANNOT be dismissed by a single reviewer. It must be:
1. Reproduced in full by the dismissing reviewer
2. Addressed by at least two independent reviewers
3. If still disputed, escalated to the PM for resolution

The burden of proof is on the DISMISSING reviewer, not on the challenger.
```

---

## 7. Summary of Findings

| # | Root Cause | Type | Recommendation |
|---|-----------|------|----------------|
| 1 | Conflation of on-air and SPI byte-order conventions | Technical | Require transformation chain audit for all multi-protocol conversions (P-5) |
| 2 | Challenger dismissal without addressing cited evidence | Procedural | Challenger Dismissal Protocol (P-1, P-3) |
| 3 | Premature confidence and anchoring bias | Cognitive | Independent derivation requirement (P-2); transformation chain audit (P-5) |
| 4 | Self-consistent diagnostic that can't detect byte-order errors | Technical | Independent derivation for register verification values (P-2) |
| 5 | No second-reviewer requirement for challenger dismissals | Procedural | Second reviewer requirement (P-4) |
| 6 | Incomplete transformation chain documented as complete | Cognitive | Transformation chain audit template (P-5) |

---

## 8. Lessons for Agent Design

### 8.1 The "Looks Right at Step N" Trap

When a multi-step transformation produces an intermediate result that appears correct (each byte value is individually correct, byte sequence matches a known convention), the reviewer may stop early and declare success. **An intermediate result is not a final result.** The transformation must be traced to the final protocol layer.

### 8.2 The Self-Consistent Error Trap

Verification that uses the same source for both the expected value and the test value is a tautology. For register verification, the expected value must be derived independently — from the specification, not from the constant being verified.

### 8.3 The "Challenger is Probably Wrong" Bias

Challenger findings in a dual-model review system deserve heightened scrutiny, not lowered. The challenger has no incentive to find non-issues — their role is to catch what the primary missed. A dismissal should require MORE evidence than a confirmation, not less.

### 8.4 One Convention Per Interface

Never assume a convention transfers between interfaces. Each hardware interface (SPI, on-air, I2C, UART) may have its own byte/bit order rules. The nRF24 SPI is LSByte-first; the nRF24 on-air is MSByte-first. These are different and both documented.

---

## 9. References

- nRF24L01+ Product Specification v1.0 §8.3.1: *"LSByte is written first"* (multi-byte register write order)
- nRF24L01+ Product Specification v1.0 §7.3: *"MSB to the left"* (on-air packet format, MSByte first)
- Bluetooth Core Specification Vol 6 Part B §1.3.1: LSBit-first transmission
- Dmitry Grinberg, "Bit-banging Bluetooth Low Energy": [http://dmitry.gr/?r=05.Projects&proj=11.%20Bluetooth%20LE%20fakery](http://dmitry.gr/?r=05.Projects&proj=11.%20Bluetooth%20LE%20fakery)
- Challenger HW-engineer review: `docs/pipeline/logs/012-challenger-hardware-engineer.md` (finding C-1)
- Challenger wireless-expert review: `docs/pipeline/logs/013-challenger-wireless-expert.md` (§4, incorrect dismissal)
- Corrected learning doc: `docs/learning/nrf24-spi-address-byte-order.md`
- Corrected learning doc: `docs/learning/mosi-spi-register-verification.md` (§4.2 correction note)
