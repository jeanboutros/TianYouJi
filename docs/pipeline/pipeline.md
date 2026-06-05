# Agent Validation Pipeline — ESP32 nRF24L01+ Project

Multi-agent validation pipeline adapted for embedded C++ development on ESP32 with ESP-IDF.

> **AI agents performing technical tasks:** Always verify hardware register layouts, API signatures, and protocol details against datasheets and official documentation before producing code or reviews. Never rely on training data for hardware specifics.

> **Never hallucinate URLs.** Do not suggest, reference, or embed any URL without first verifying it exists.

---

## Stack

- **Target**: ESP32 (Xtensa LX6, dual-core)
- **Framework**: ESP-IDF v6.0.1
- **Language**: C++17 (GNU++26 dialect in toolchain)
- **Build system**: CMake + idf.py
- **Libraries**: nrf24l01plus (portable), nrf24_espidf (platform adapter)
- **Documentation**: Doxygen `/** ... */` blocks
- **Testing**: static_assert (compile-time), host-side unit tests
- **Hardware**: nRF24L01+ radio module, BLE passive reception

## Project Structure

```
esp32/
  main/                     # Application entry point
    main.cpp
    CMakeLists.txt
  components/
    nrf24l01plus/           # Portable library (no platform deps)
      include/nrf24l01plus/
        registers/          # Typed register headers (one per register)
        hal.h               # Abstract HAL interface
        driver.h            # Protocol-level driver
        commands.h          # SPI command constants
        ble.h               # BLE utilities
        ble_config.h        # BLE RX configuration
        diag.h              # Diagnostic utilities
      src/
      test/
    nrf24_espidf/           # ESP-IDF platform adapter
      include/nrf24_espidf/
      src/
  docs/
    datasheets/             # Hardware datasheets (source of truth)
    learning/              # Knowledge docs (per AGENTS.md)
    pipeline/              # This pipeline documentation
  build/                    # Build output (generated)
```

## Architecture Principles

| Principle | Rule |
|-----------|------|
| Platform independence | Library headers MUST NOT include platform-specific headers |
| HAL abstraction | Hardware access through abstract `Hal` class |
| Typed registers | Every register field → `enum class`, every register → struct with `to_byte()`/`from_byte()` |
| Datasheet fidelity | Every field name, bit position, encoding from the datasheet — never invented |
| No raw integers | API uses named constants and typed enums, never magic numbers |
| Doxygen mandatory | Every public symbol has `/** @brief ... */` |

---

## Pipeline Phases

The pipeline has 3 phases. Each phase must complete before the next begins.

```
┌──────────────────────────┐     ┌──────────────────┐     ┌──────────────────────────┐
│  Phase A:                │     │  Phase B:        │     │  Phase C:                │
│  REQUIREMENTS & DESIGN   │────▶│  BUILD (PAU)     │────▶│  MULTI-AGENT VERIFY      │
│                          │     │                  │     │                          │
│  All specialists review  │     │  - Plan          │     │  All specialists approve │
│  in parallel + challenge │     │  - Apply         │     │  + Dual-Model Challenge  │
│  - SW Engineer           │     │  - Validate      │     │  - SW Engineer           │
│  - HW Engineer           │     │                  │     │  - HW Engineer           │
│  - Wireless Expert       │     │                  │     │  - Wireless Expert       │
│  - Security Reviewer     │     │                  │     │  - Security Reviewer     │
│  - Test Engineer         │     │                  │     │  - Test Engineer         │
│  - Docs Writer           │     │                  │     │  - Docs Writer           │
└──────────────────────────┘     └──────────────────┘     └──────────────────────────┘
```

---

## Phase A — Requirements & Architecture (Max 3 Loops)

**Goal:** Define detailed requirements and validate "What" and "How" before writing code.
All specialist agents participate in parallel. Both models review independently (Dual-Model Challenge).

### A0: Task Definition
All agents collaborate to produce a detailed task specification:
- **Acceptance criteria** (binary pass/fail)
- **Files to create/modify**
- **Constraints and risks**
- **Test strategy**
- **Documentation plan**

### A1: Parallel Specialist Review

All six specialists review the proposal **independently and in parallel**:

| Agent | Focus |
|-------|-------|
| Software Engineer | Architecture, component boundaries, API design, code patterns |
| Hardware Engineer | Register models, bit layouts, datasheet fidelity, timing constraints |
| Wireless Expert | RF protocol compliance, BLE spec conformance, frequency/channel mapping, modulation |
| Security Reviewer | Buffer safety, stack depth, secrets exposure, input validation |
| Test Engineer | Testability, coverage gaps, edge cases, static_assert feasibility |
| Docs Writer | Doxygen coverage plan, learning doc needs, reference validity |

### A2: Dual-Model Challenge

Two model passes review the architecture independently:
1. **Primary pass** — produces the architecture proposal
2. **Challenger pass** — critiques the primary, identifies gaps and contradictions

The Agency Director synthesizes the strongest elements. If contradictions remain, the user decides.

### A3: Gate
- ALL six specialists must issue `APPROVED` or `CONDITIONAL PASS`
- Any `REJECTED` → loop back with specific critique (max 3 loops)
- After 3 iterations → escalate to user with all critiques

---

## Phase B — Build (PAU Loop)

**Goal:** Implement incrementally with self-validation.

### B1: PLAN
1. Read the task description
2. Identify files to create/modify
3. List acceptance criteria
4. Declare the logical units of work

### B2: APPLY (Per Unit)
1. Implement one logical unit
2. Run `idf.py build` — must pass with zero errors and zero warnings (`-Werror` is active)
3. Run static_assert tests if applicable
4. Report progress

### B3: VALIDATE
After all units complete:
```bash
source ~/.espressif/tools/activate_idf_v6.0.1.sh && idf.py build
```
Must exit 0. If hardware is connected:
```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

### Rules
- NEVER implement everything at once — one unit at a time
- NEVER skip validation between units
- If a unit fails to build, fix before proceeding
- All new public symbols MUST have Doxygen documentation

---

## Phase C — Verify (Multi-Agent Approval Gate)

**Goal:** Final check before commit. ALL specialist agents must approve.

### C1: Dual-Model Challenge (Verification)

Two model passes review the implemented code independently:
1. **Primary verifier** — checks against acceptance criteria and architecture
2. **Challenger verifier** — adversarial review, tries to find what the primary missed

### C2: Parallel Specialist Approval

All six specialists review the final implementation **independently**:

| Agent | Verification Focus |
|-------|-------------------|
| Software Engineer | Clean architecture, SOLID, typed enums, HAL decoupling, no raw integers |
| Hardware Engineer | Register implementations match datasheet exactly, reserved bits handled |
| Wireless Expert | RF parameters correct, BLE channel mapping verified, protocol timing valid |
| Security Reviewer | No buffer overflows, stack safe, no secrets in code, input validated |
| Test Engineer | Test coverage sufficient, edge cases covered, static_asserts pass |
| Docs Writer | Doxygen complete on all new symbols, learning docs updated, refs valid |

### C3: Approval Gate

**ALL six specialists must approve.** Verdicts:
- `APPROVED` — no issues found in this agent's domain
- `CONDITIONAL PASS` — minor issues that don't block (raised as advisory flags)
- `REJECTED` — blocking issue found (route feedback, max 3 loops)

### Final Verdict
- **ALL APPROVED** → Commit with conventional commit message
- **ANY REJECTED** → Route feedback to Code Architect, fix, re-submit to rejecting agent(s) only (max 3 loops)
- **3 loops exhausted** → Escalate to user with all findings

### Challenger Checklist (applies to all reviewers)

| # | Check | Criterion |
|---|-------|-----------|
| 1 | Build | `idf.py build` exits 0, no warnings |
| 2 | Doxygen | Every new public function/struct/enum has `/** @brief */` |
| 3 | Typed enums | No raw integers in public API — all fields use `enum class` |
| 4 | Datasheet fidelity | Field names match datasheet; no invented values |
| 5 | HAL decoupling | Library headers include only `<cstdint>`, `<cstring>`, own headers |
| 6 | No magic numbers | `@code` examples and docs use library vocabulary |
| 7 | Reserved bits | Accounted for in `to_byte()`/`from_byte()` |
| 8 | AGENTS.md compliance | All rules in AGENTS.md followed |
| 9 | Commit message | Conventional Commits format |
| 10 | Learning docs | Non-trivial learnings captured in `docs/learning/` |

### Verdict
- **APPROVED** → Commit with conventional commit message
- **REJECTED** → Route feedback, fix, re-validate (max 3 loops)

---

## No-Assumption Protocol

When encountering ambiguity, **HALT** and signal:

```
STATUS: BLOCKED
CONTEXT: <What was being analyzed>
QUESTION: <Specific question — one only>
OPTIONS: <Suggested answers if applicable>
IMPACT: <What downstream work depends on this answer>
```

Pipeline pauses until the user answers. Never guess hardware behaviour.

---

## Flag Protocol

When an issue is found that doesn't block the current task:

```markdown
## Flag: [type] — [short title]

| Field      | Value |
|-----------|-------|
| Type       | `task` / `clarification` / `advisory` |
| Priority   | `critical` / `high` / `medium` / `low` |
| Raised by  | Agent role |
| Blocking   | `yes` / `no` |
| Reference  | Current task |

## Description
What was found.

## Evidence
Code snippet or datasheet reference.

## Suggested action
Recommendation.
```

---

## Incremental Execution Rules

1. Identify logical units within the task
2. Implement one unit → `idf.py build` → confirm pass
3. Report progress between units
4. Only after ALL units pass → proceed to verification
5. Never commit code that doesn't build

---

## Datasheet Verification (Replaces Context7)

Before coding any hardware register or protocol detail:

1. **Check `docs/datasheets/`** for the relevant datasheet
2. **Read the specific register/section** from the datasheet
3. **If unclear, search the web** (application notes, errata, community reports)
4. **If still unclear, HALT** — do not invent values

Every register field in a review must cite the datasheet page/table where the value comes from.

---

## Review Persistence

Every review round is saved to `docs/pipeline/reviews/`:
```
docs/pipeline/reviews/
  <task-name>/
    round-1.md
    round-2.md
    ...
```

Never overwrite previous rounds. This creates an audit trail.
