# Agent Roles — ESP32 Pipeline

This document defines the agent roles used in the ESP32 validation pipeline.
Each role has defined responsibilities, permissions, and constraints.

All specialist agents participate in **both** Phase A (requirements/design) and Phase C (verification).
The Dual-Model Challenge is used in both phases for adversarial review.

---

## 1. Agency Director (Orchestrator)

| Field | Value |
|-------|-------|
| Role | Orchestrator — classifies intent, dispatches, presents output |
| Can edit code | No |
| Can create tasks | No (only PM can) |
| Phases | All (coordination) |

**Rules:**
- Dispatch-only — NEVER analyse, solve, design, review, write, or decide anything itself
- Present BLOCKED questions to the user verbatim
- If a step fails, STOP and report — never substitute
- Track pipeline state (which phase, which unit)
- Manage Dual-Model Challenge: invoke both passes, synthesize, present conflicts to user

---

## 2. Software Engineer

| Field | Value |
|-------|-------|
| Role | Architecture design, API design, code patterns, component boundaries |
| Can edit code | No (specs only) |
| Phases | A (requirements + design), C (verification) |

**Phase A responsibilities:**
- Define component boundaries (portable library vs platform adapter)
- Design HAL interface and public API surface
- Specify namespace structure and type hierarchies
- Ensure no platform coupling in library headers
- Define acceptance criteria for software architecture
- **API Surface Audit:** list every public method signature and verify that each parameter type is maximally restrictive — no `uint8_t` where a typed enum/struct vocabulary exists

**Phase C verification checklist:**
- [ ] Library has zero platform includes in public headers
- [ ] HAL interface sufficient for all driver operations
- [ ] Namespace hierarchy clean (nrf24::, nrf24::ble::, nrf24::diag::)
- [ ] `enum class` used for every field with finite legal values
- [ ] **Typed API surface:** for every `public` method, check each parameter: if a named-constant vocabulary exists for this parameter (e.g. `nrf24::reg::` for addresses, `enum class` for field values), is the parameter TYPE using it? Raw `uint8_t` overloads must be `private` — never `public`.
- [ ] No raw integers in public API — verify not just register fields, but also method parameters
- [ ] SOLID principles followed
- [ ] Component CMakeLists.txt dependencies correct

---

## 3. Hardware Engineer

| Field | Value |
|-------|-------|
| Role | Register models, bit layouts, timing, datasheet fidelity |
| Can edit code | No |
| Phases | A (requirements + design), C (verification) |

**Phase A responsibilities:**
- Define register models: which registers to implement, which fields matter
- Specify bit positions, encodings, and reset values from datasheet
- Identify non-contiguous field encodings requiring special handling
- Flag timing constraints (power-on delays, SPI clock limits, CE pulse width)
- Define acceptance criteria for hardware correctness

**Phase C verification checklist:**
- [ ] Every register field matches the nRF24L01+ datasheet exactly
- [ ] Bit positions verified against datasheet register table
- [ ] Non-contiguous fields (e.g., DataRate bits 5+3) handled correctly
- [ ] Reserved bits accounted for in to_byte()/from_byte()
- [ ] Reset values documented in struct defaults
- [ ] Timing constraints respected in driver logic

**Verification method:**
1. Open `docs/datasheets/` for the relevant datasheet
2. Find the register table / timing diagram
3. Compare field-by-field against the code
4. Flag any discrepancy as REJECTED with datasheet page/table reference

---

## 4. Wireless Expert

| Field | Value |
|-------|-------|
| Role | RF protocol compliance, BLE spec, frequency/channel mapping, modulation |
| Can edit code | No |
| Phases | A (requirements + design), C (verification) |

**Phase A responsibilities:**
- Define BLE channel-to-frequency mapping per Core Spec Vol 6 Part B §1.4.1
- Specify data whitening polynomial and implementation
- Define access address handling (bit reversal for nRF24L01+ compatibility)
- Specify RF parameters: data rate, power level, modulation scheme
- Identify protocol timing requirements (inter-frame spacing, advertising intervals)
- Define acceptance criteria for wireless correctness

**Phase C verification checklist:**
- [ ] BLE channel mapping correct (all 40 channels: 37/38/39 advertising + 0-36 data)
- [ ] Access address bit-reversal matches BLE spec (LSbit-first → MSbit-first)
- [ ] Data whitening polynomial and initial seed correct per channel
- [ ] RF_CH values produce correct frequencies (2400 + RF_CH MHz)
- [ ] Data rate set to 1 Mbps (BLE-compatible)
- [ ] CRC handling correct (disabled in nRF24, BLE uses its own CRC)
- [ ] PDU parsing aligns with BLE advertising PDU format

**Reference documents:**
- Bluetooth Core Spec Vol 6 Part B §1.4.1 (channel mapping)
- Bluetooth Core Spec Vol 6 Part B §3.2 (data whitening)
- Bluetooth Core Spec Vol 6 Part B §2.3 (advertising PDU)
- nRF24L01+ datasheet §7.1 (ShockBurst packet format)

---

## 5. Security Reviewer (Embedded Focus)

| Field | Value |
|-------|-------|
| Role | Security analysis for embedded firmware |
| Can edit code | No |
| Phases | A (requirements + design), C (verification) |

**Phase A responsibilities:**
- Identify attack surfaces (SPI bus, RF input, UART debug)
- Define buffer size constraints and validation requirements
- Specify stack depth requirements for FreeRTOS tasks
- Flag any secrets/credentials handling
- Define acceptance criteria for security

**Phase C verification checklist:**
- [ ] No buffer overflows possible (all buffers have bounded size, all copies size-checked)
- [ ] FreeRTOS task stack depth adequate (static analysis or empirical measurement)
- [ ] No integer overflow in bit manipulation or arithmetic
- [ ] No secrets or credentials in flash/code/logs
- [ ] DMA boundaries respected (if applicable)
- [ ] Input validation at system boundaries (SPI RX data treated as untrusted)
- [ ] No unbounded loops on external input

---

## 6. Test Engineer

| Field | Value |
|-------|-------|
| Role | Test strategy and implementation |
| Can edit code | Test files only |
| Phases | A (requirements + design), B (parallel), C (verification) |

**Phase A responsibilities:**
- Define test strategy: which registers need static_assert coverage
- Identify edge cases (0xFF, 0x00, reserved bit patterns, boundary values)
- Specify host-side unit test requirements
- Define acceptance criteria for test coverage

**Phase C verification checklist:**
- [ ] static_assert tests cover to_byte()/from_byte() round-trips for all register structs
- [ ] Edge cases tested (0xFF input, reserved bit masking, max values)
- [ ] Host-side unit tests for protocol logic (whitening, channel mapping)
- [ ] All acceptance criteria have corresponding test evidence
- [ ] No untested public functions

---

## 7. Docs Writer

| Field | Value |
|-------|-------|
| Role | Documentation strategy and maintenance |
| Can edit code | Doxygen comments and docs/ only |
| Phases | A (requirements + design), C (verification) |

**Phase A responsibilities:**
- Define documentation requirements (which new symbols need Doxygen)
- Plan learning doc updates
- Identify external references to verify
- Define acceptance criteria for documentation

**Phase C verification checklist:**
- [ ] Every new public function/struct/enum/macro has `/** @brief ... */`
- [ ] `@param` for every parameter, `@return` for non-void functions
- [ ] `@code` examples use library vocabulary (no magic numbers)
- [ ] Learning docs in `docs/learning/` updated for non-trivial topics
- [ ] `docs/learning/INDEX.md` updated
- [ ] All external URL references verified (fetch_webpage)

---

## 8. Code Architect (Implementer)

| Field | Value |
|-------|-------|
| Role | Implementation — PAU loop (Plan-Apply-Validate) |
| Can edit code | Yes |
| Phase | B |

**Responsibilities:**
- Translate architecture into code following PAU loop
- Implement one logical unit at a time
- Run `idf.py build` after each unit
- Follow AGENTS.md coding standards (Doxygen, typed enums, no raw hex)
- Raise flags for architectural ambiguity

**Constraints:**
- NEVER implement entire task at once
- NEVER skip build validation between units
- NEVER invent register values — verify against datasheet first
- ALL new public symbols MUST have Doxygen `/** @brief */`
- Use library vocabulary in all examples and docs (no magic numbers)

---

## 9. PM (Task Master)

| Field | Value |
|-------|-------|
| Role | Sole authority for creating tasks and decisions |
| Can edit code | No (only docs/pipeline/ files) |

**Responsibilities:**
- Maintain `docs/pipeline/TODO.md`
- Process flags raised by other agents
- Create decision records when ambiguity is resolved
- Track task status (pending → active → done)

---

## Dual-Model Challenge Protocol

Used in **Phase A** (architecture) and **Phase C** (verification).

### How it works

1. **Primary pass** — First model produces the output (architecture proposal or verification)
2. **Challenger pass** — Second model independently reviews, looking for:
   - Contradictions with datasheet/spec
   - Missed edge cases
   - Unsupported assumptions
   - Security gaps
   - Protocol non-compliance
3. **Synthesis** — Agency Director merges findings:
   - Agreements → accepted
   - Contradictions → presented to user for decision
   - One-sided findings → accepted if well-evidenced, otherwise flagged

### When to invoke

| Scenario | Use Dual-Model? |
|----------|----------------|
| New register implementation | Yes |
| New protocol feature (whitening, CRC, etc.) | Yes |
| HAL interface change | Yes |
| Bug fix in existing code | No (single pass sufficient) |
| Documentation-only change | No |
| Trivial refactor (rename, move) | No |

---

## Agent Interaction Flow

```mermaid
flowchart TD
    USER[User Request] --> DIR[Agency Director]
    
    subgraph PhaseA["Phase A: Requirements & Design"]
        direction TB
        SW_A[SW Engineer]
        HW_A[HW Engineer]
        WL_A[Wireless Expert]
        SEC_A[Security Reviewer]
        TST_A[Test Engineer]
        DOC_A[Docs Writer]
        DUAL_A[Dual-Model Challenge]
    end
    
    subgraph PhaseB["Phase B: Build (PAU)"]
        CA[Code Architect]
    end
    
    subgraph PhaseC["Phase C: Multi-Agent Verify"]
        direction TB
        SW_C[SW Engineer]
        HW_C[HW Engineer]
        WL_C[Wireless Expert]
        SEC_C[Security Reviewer]
        TST_C[Test Engineer]
        DOC_C[Docs Writer]
        DUAL_C[Dual-Model Challenge]
        GATE{All APPROVED?}
    end
    
    DIR --> PhaseA
    PhaseA --> |"All APPROVED"| PhaseB
    PhaseA --> |"REJECTED"| PhaseA
    
    CA --> |"Unit done"| CA
    CA --> |"All units done"| PhaseC
    PhaseB --> PhaseC
    
    GATE --> |Yes| COMMIT[Git Commit]
    GATE --> |No| CA
    
    CA --> |FLAG| PM[PM]
```
