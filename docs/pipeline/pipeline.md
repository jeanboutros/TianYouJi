# Agent Validation Pipeline — ESP32 nRF24L01+ Project

Multi-agent validation pipeline with tiered compliance gates, adapted for embedded C++ development on ESP32 with ESP-IDF.

> **AI agents performing technical tasks:** Always verify hardware register layouts, API signatures, and protocol details against datasheets and official documentation before producing code or reviews. Never rely on training data for hardware specifics.

> **Never hallucinate URLs.** Do not suggest, reference, or embed any URL without first verifying it exists.

---

## Pipeline Flow Diagram

```
Phase A: REQUIREMENTS & DESIGN ──▶ A-GATE (T3+T-ARCH) ──▶ Phase B: BUILD ──▶ B-UNIT-GATE (T1+T-ARCH) × N units ──▶ B-FINAL-GATE (T1+T2+T-ARCH) ──▶ Phase C: MULTI-AGENT VERIFY ──▶ C-GATE (T1+T3+T-ARCH) ──▶ COMMIT

         ┌─────────────┐                          ┌──────────────────┐                       ┌──────────────────┐                    ┌──────────────────┐
         │  Retry loop  │                          │  Retry per unit  │                       │  Retry up to 3×  │                    │  Retry up to 3×  │
         │  (max 3× T3) │◄───────────────────────│  (max 3× T1)    │◄─────────────────────│  (T1+T2+T-ARCH   │◄───────────────────│  (T1+T3+T-ARCH   │
         │  + 3× T-ARCH)│                          │  + 3× T-ARCH)   │                       │   indep per tier)│                    │   indep per tier)│
         └─────────────┘                           └──────────────────┘                       └──────────────────┘                    └──────────────────┘
              A-GATE                                    B-UNIT-GATE                              B-FINAL-GATE                            C-GATE
         T3: 6 specialists                         T1: Mechanical                           T1: Mechanical                          T1: Mechanical re-run
         T-ARCH: Architecture review               T-ARCH: Architecture review              T2: Architectural                       T3: 6 specialists
                                                    (build, Doxygen,                      T-ARCH: Architecture review             T-ARCH: Architecture review
                                                     patterns, enums)
```

Each gate has an independent retry budget per tier (see Retry Protocol below). Worst case per gate: 3×T1 + 3×T2 + 3×T3 + 3×T-ARCH = 12 loops (each tier has independent counters). A T1 failure does not consume the T2, T3, or T-ARCH budget.

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

## Compliance Tiers

Compliance checks are organised into four tiers with independent retry budgets. Each gate applies a specific combination of tiers. Tier checks are independent — each tier has its own retry budget. See Gate Definitions for which tiers each gate checks.

### T1 — Mechanical (Automated)

Checks that can be verified by automated tooling (build, grep scripts). No human judgement required.

| # | Check | Method | Fail condition |
|---|-------|--------|----------------|
| T1.1 | Build passes | `idf.py build` | Exit code ≠ 0 or any warning |
| T1.2 | Doxygen on public symbols | Grep every new/changed function, struct, enum, method for `/**` block with `@brief` | Missing `@brief` on any public symbol |
| T1.3 | No decision references | Grep for `D-\d`, `F-\d`, `(decision` patterns in .cpp/.h | Any match |
| T1.4 | No changelog-style comments | Grep for `replaces the`, `was previously`, `formerly`, `old`, `refactored from` in .cpp/.h | Any match |
| T1.5 | No raw uint8_t where typed vocabulary exists | Every `uint8_t` parameter in public API must either have a typed overload or be `private`/`protected` | Public `uint8_t` with no typed alternative |
| T1.6 | No magic numbers in @code examples | Grep for hex/decimal literals >1 in `@code` blocks within library headers | Raw literal where named constant/enum exists |
| T1.7 | Constants in correct module | Grep for chip-level constants outside their library namespace (e.g. `NRF24_MAX_PAYLOAD` in main/) | Match in wrong module |
| T1.8 | Reserved bits handled | Grep `to_byte()`/`from_byte()` implementations for reserved bit masking | Reserved bits not masked |
| T1.9 | No hardcoded secrets | Grep for common secret patterns (password, api_key, secret, token, credential, Bearer) in .cpp/.h files | Any match that isn't a test fixture or explanatory comment |

**Who runs T1:** Code Architect runs the checks; Supreme Leader orchestrates retry loops.

### T2 — Architectural (Agent Delta Review)

Checks that require design-level judgement about structure, boundaries, and API surface.

| # | Check | Method | Fail condition |
|---|-------|--------|----------------|
| T2.1 | Library/platform boundary | Grep for platform includes in library public headers | Any `#include <esp_*>` or `#include "freertos/"` in `components/nrf24l01plus/include/` |
| T2.2 | Namespace structure | All public symbols in `nrf24::`, `nrf24::ble::`, `nrf24::diag::`, `nrf24::reg::` | Symbol in global namespace or wrong namespace |
| T2.3 | File placement | Chip constants in library, task functions in separate .cpp, HAL impl in platform adapter | Constant in main/, function in wrong component |
| T2.4 | API surface audit | Every public method parameter is maximally restrictive type | Public `uint8_t` where enum/struct vocabulary exists |
| T2.5 | No mutable globals in library | Grep for file-scope mutable globals in library `src/` | Global mutable state found |

**Who runs T2:** Software Engineer spot-checks with delta review; Code Architect implements fixes.

### T3 — Semantic (Full Specialist Review)

Checks that require deep domain expertise — datasheet fidelity, protocol correctness, security, test coverage, documentation completeness.

| # | Check | Method | Fail condition |
|---|-------|--------|----------------|
| T3.1 | Datasheet fidelity | Field names, bit positions, encodings compared to datasheet tables | Any deviation from datasheet |
| T3.2 | Protocol correctness | BLE channel mapping, whitening, CRC, PDU format verified | Protocol violation |
| T3.3 | Security review | Buffer bounds, stack depth, secrets, input validation | Any vulnerability |
| T3.4 | Test coverage | static_assert round-trips, edge cases, host-side unit tests | Uncovered public function or missing edge case |
| T3.5 | Documentation completeness | Learning docs, INDEX.md, verified references | Missing or unverified doc |
| T3.6 | Full architecture review | SOLID, HAL sufficiency, namespace hygiene, component CMake | Architecture violation |

**Who runs T3:** All six specialist agents (Software Engineer, Hardware Engineer, Wireless Expert, Security Reviewer, Test Engineer, Docs Writer).

### T-ARCH — Architecture + Principles (Cross-cutting)

Checks that apply to every agent output, not just at formal gate transitions. T-ARCH runs alongside whichever tier checks are active at a gate.

| # | Check | Method | Fail condition |
|---|-------|--------|----------------|
| T-ARCH.1 | Logical consistency | No contradictions between agent outputs, datasheet, and design | Self-contradictory design |
| T-ARCH.2 | Structural soundness | No circular dependencies, clean layering, no god objects | Circular dependency or layer violation |
| T-ARCH.3 | Principle alignment | Follows project principles (typed API, RAII, HAL, no globals) | Principle violation |
| T-ARCH.4 | Completeness | All requirements covered, no orphaned code, no TODO/FIXME | Missing requirement or orphaned code |
| T-ARCH.5 | Correct agent routing | Output went to the right agent, no dispatch errors | Misrouted dispatch |

**Who runs T-ARCH:** Software Engineer (code and design review); Supreme Leader (routing validation).

---

## Gate Definitions

Gates are mandatory checkpoints. Work cannot proceed past a gate until all required tiers pass. Each gate applies a specific combination of tiers.

### A-GATE (Exits Phase A)

| Property | Value |
|----------|-------|
| Location | Between Phase A and Phase B |
| Tiers | T3 + T-ARCH |
| Who runs | All 6 specialists independently (T3), Software Engineer + Supreme Leader (T-ARCH) |
| Pass | All 6 issue APPROVED or CONDITIONAL PASS, and T-ARCH checks pass |
| Fail | Any REJECTED → loop back to Phase A with critique; T-ARCH fail → architectural fix |
| Retry budget | 3 loops at T3, 3 loops at T-ARCH (independent) |
| Escalation | After 3 T3 or 3 T-ARCH loops → escalate to user with all findings |

A-GATE is unchanged from the previous A3 gate — it ensures all specialists have approved the architecture before any code is written.

### B-UNIT-GATE (After Each PAU Unit in Phase B)

| Property | Value |
|----------|-------|
| Location | After each logical unit in the PAU loop |
| Tiers | T1 + T-ARCH |
| Who runs | Code Architect (T1), Software Engineer (T-ARCH) |
| Pass | All 9 T1 checks pass AND T-ARCH checks pass |
| Fail | Any T1 check fails → fix and re-check the unit |
| Retry budget | 3 loops per unit at T1, 3 loops at T-ARCH (independent) |
| Escalation | After 3 T1 loops on same unit → escalate to user |

Rationale: Phase B had NO quality gate. Mechanical violations (missing Doxygen, raw integers, magic numbers, banned comment patterns) leaked to Phase C where they wasted specialist time. B-UNIT-GATE catches these early, per unit, at negligible cost. T-ARCH catches architectural drift at the unit level before it compounds.

### B-FINAL-GATE (Exits Phase B)

| Property | Value |
|----------|-------|
| Location | Between Phase B and Phase C |
| Tiers | T1 + T2 + T-ARCH |
| Who runs | Code Architect (T1), Software Engineer spot-check (T2), Software Engineer (T-ARCH) |
| Pass | All T1 checks pass AND all T2 checks pass AND T-ARCH checks pass |
| Fail | T1 fail → Code Architect fixes; T2 fail → Code Architect fixes with SW Engineer input; T-ARCH fail → architectural fix |
| Retry budget | 3 loops at T1, 3 loops at T2, 3 loops at T-ARCH (independent) |
| Escalation | After 3 loops at same tier → escalate to user with violation log |

B-FINAL-GATE ensures the Code Architect's output meets both mechanical and architectural standards before it reaches specialist review. T1, T2, and T-ARCH have independent retry counters — a T1 failure does not consume the T2 or T-ARCH budget.

### C-GATE (Enters Commit)

| Property | Value |
|----------|-------|
| Location | After Phase C specialist review, before git commit |
| Tiers | T1 re-run + T3 + T-ARCH |
| Who runs | Code Architect (T1 re-run), All 6 specialists (T3), Software Engineer + Supreme Leader (T-ARCH) |
| Pass | T1 re-run passes AND all 6 specialists issue APPROVED or CONDITIONAL PASS AND T-ARCH checks pass |
| Fail | T1 fail → Code Architect fixes; T3 fail → route to relevant specialist(s); T-ARCH fail → architectural fix |
| Retry budget | 3 loops at T1, 3 loops at T3, 3 loops at T-ARCH (independent) |
| Escalation | After 3 loops at same tier → escalate to user with all findings |

C-GATE adds a T1 re-run before final specialist review. This catches any regressions introduced during B→C fixes and ensures mechanical compliance is verified on the final artifact, not just at B-FINAL-GATE. T-ARCH ensures architectural principles are validated on the final artifact before commit.

### Pipeline Passport

Every task carries a **passport** that tracks which pipeline steps have been completed. No step may be skipped without written justification. An agent receiving a task with a missing previous step must reject it and return STATUS: BLOCKED to the Supreme Leader.

Passports are stored in `docs/project-management/passports/<ticket-id>-passport.md` and are created by the PM when a ticket is opened. For the full passport format and rules, see `.opencode/skills/pipeline-passport/SKILL.md`.

Key passport rules:

1. **No step without a stamp** — every step must be checked off before the next step begins
2. **No skip without justification** — a written justification and Supreme Leader authorisation are required
3. **Loops are tracked** — every A→B→A loop is recorded in the passport's Loop History section
4. **Passport travels with dispatch** — the passport file path is included in every dispatch envelope

---

## Pipeline Phases

The pipeline has 3 phases with tiered compliance gates at each transition. Each phase must complete before the next begins.

```
┌──────────────────────────┐     ┌──────────────────┐     ┌──────────────────────────┐
│  Phase A:                │     │  Phase B:        │     │  Phase C:                │
│  REQUIREMENTS & DESIGN   │────▶│  BUILD (PAU)     │────▶│  MULTI-AGENT VERIFY      │
│                          │     │                  │     │                          │
│  All specialists review  │     │  - Plan          │     │  All specialists approve │
│  in parallel + challenge │     │  - Apply         │     │  + Dual-Model Challenge  │
│  - SW Engineer           │     │  - Validate      │     │  - SW Engineer           │
│  - HW Engineer           │     │                  │     │  - HW Engineer           │
│  - Wireless Expert       │     │  Each unit:      │     │  - Wireless Expert       │
│  - Security Reviewer     │     │  B-UNIT-GATE     │     │  - Security Reviewer     │
│  - Test Engineer         │     │  T1+T-ARCH       │     │  - Test Engineer         │
│  - Docs Writer           │     │  All units done: │     │  - Docs Writer           │
│                          │     │  B-FINAL-GATE    │     │                          │
│  A-GATE: T3+T-ARCH      │     │  (T1+T2+T-ARCH)  │     │  C-GATE: T1+T3+T-ARCH   │
└──────────────────────────┘     └──────────────────┘     └──────────────────────────┘
```

---

## Phase A — Requirements & Architecture (Max 3 Loops at T3, 3 at T-ARCH)

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

The Supreme Leader synthesizes the strongest elements. If contradictions remain, the user decides.

### A3: A-GATE (T3 + T-ARCH — All 6 Specialists)

All six specialists must issue `APPROVED` or `CONDITIONAL PASS`.
- Any `REJECTED` → loop back with specific critique (max 3 loops at T3)
- T-ARCH failures → architectural fix (max 3 loops at T-ARCH)
- After 3 T3 loops or 3 T-ARCH loops → escalate to user with all critiques

**Why T3 + T-ARCH:** Phase A is a design phase. Mechanical checks (T1) are not meaningful until code exists. Architectural checks (T2) are implicitly performed during the specialist review because architecture is the subject matter. T-ARCH is included because logical consistency, structural soundness, and principle alignment can be validated on design documents — no code is needed. No code has been written yet, so T1 is not applicable.

---

## Phase B — Build (PAU Loop with Unit Gates)

**Goal:** Implement incrementally with self-validation, enforced by T1 mechanical checks after each unit and T1+T2 checks before exiting Phase B.

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

### B2a: B-UNIT-GATE (T1 + T-ARCH — After Each Unit)

**After completing each logical unit**, the Code Architect runs the full T1 mechanical check suite, and the Software Engineer reviews T-ARCH checks:

| # | Check | Fail action |
|---|-------|-------------|
| T1.1 | Build passes | Fix build errors/warnings |
| T1.2 | Doxygen on new/changed public symbols | Add missing `/** @brief */` blocks |
| T1.3 | No decision references | Remove `D-\d`, `F-\d`, `(decision` patterns |
| T1.4 | No changelog-style comments | Remove `replaces the`, `was previously`, etc. |
| T1.5 | No raw uint8_t where typed vocabulary exists | Add typed overload or make raw overload private |
| T1.6 | No magic numbers in @code examples | Replace with named constants/enums |
| T1.7 | Constants in correct module | Move constant to correct namespace/file |
| T1.8 | Reserved bits handled | Add masking in `to_byte()`/`from_byte()` |

**If any T1 check fails:**
1. Fix the violation
2. Re-run the full T1 suite
3. Retry budget: 3 loops per unit
4. After 3 loops → escalate to user with violation log

**If any T-ARCH check fails:**
1. Route to Software Engineer for architectural fix
2. Re-run T-ARCH checks
3. Retry budget: 3 loops per unit (independent of T1)
4. After 3 loops → escalate to user

**Only after all T1 AND T-ARCH checks pass**, proceed to the next unit.

### B3: VALIDATE
After all units complete:
```bash
source ~/.espressif/tools/activate_idf_v6.0.1.sh && idf.py build
```
Must exit 0. If hardware is connected:
```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

### B3a: B-FINAL-GATE (T1 + T2 + T-ARCH — Exits Phase B)

**After all units pass their B-UNIT-GATEs**, the Code Architect runs T1 again (full compliance, not just deltas), the Software Engineer spot-checks T2, and the Software Engineer reviews T-ARCH:

**T1 re-run:** All 8 mechanical checks on the complete codebase (not just the last unit).

**T2 Architectural checks:**

| # | Check | Fail action |
|---|-------|-------------|
| T2.1 | Library/platform boundary | Remove platform includes from library headers |
| T2.2 | Namespace structure | Move symbols to correct namespace |
| T2.3 | File placement | Move constants/functions to correct files |
| T2.4 | API surface audit | Replace public `uint8_t` params with typed alternatives |
| T2.5 | No mutable globals in library | Encapsulate or remove global mutable state |

**If T1 fails:** Code Architect fixes, re-runs T1. Retry budget: 3 loops.
**If T2 fails:** Code Architect fixes with Software Engineer input. Retry budget: 3 loops (independent of T1).
**If T-ARCH fails:** Software Engineer fixes architectural violations. Retry budget: 3 loops (independent of T1 and T2).
**After 3 loops at same tier → escalate to user** with full violation log.

**Only after T1, T2, AND T-ARCH all pass**, proceed to Phase C.

### Rules
- NEVER implement everything at once — one unit at a time
- NEVER skip validation between units
- NEVER skip B-UNIT-GATE after a unit
- If a unit fails to build, fix before proceeding
- All new public symbols MUST have Doxygen documentation
- T1 failures route to Code Architect (mechanical fixes)
- T2 failures route to Code Architect with Software Engineer input
- T-ARCH failures route to Software Engineer (architectural principle fixes)

---

## Phase C — Verify (Multi-Agent Approval Gate)

**Goal:** Final check before commit. ALL specialist agents must approve. C-GATE adds a T1 re-run before specialist review.

### C0: T1 Re-run (Mechanical Compliance Verification)

Before specialist review begins, re-run the full T1 suite on the final codebase:
- This catches any regressions introduced during B-FINAL-GATE fixes
- If T1 fails → Code Architect fixes, re-run T1 (3 retry budget, independent of T3)
- Only proceed to C1 when T1 passes

### C1: Dual-Model Challenge (Verification)

Two model passes review the implemented code independently:
1. **Primary verifier** — checks against acceptance criteria and architecture
2. **Challenger verifier** — adversarial review, tries to find what the primary missed

### C2: Parallel Specialist Approval (T3)

All six specialists review the final implementation **independently**:

| Agent | Verification Focus |
|-------|-------------------|
| Software Engineer | Clean architecture, SOLID, typed enums, HAL decoupling, no raw integers |
| Hardware Engineer | Register implementations match datasheet exactly, reserved bits handled |
| Wireless Expert | RF parameters correct, BLE channel mapping verified, protocol timing valid |
| Security Reviewer | No buffer overflows, stack safe, no secrets in code, input validated |
| Test Engineer | Test coverage sufficient, edge cases covered, static_asserts pass |
| Docs Writer | Doxygen complete on all new symbols, learning docs updated, refs valid |

### C3: C-GATE (T1 Re-run + T3 + T-ARCH — Full Specialist Review)

**ALL six specialists must approve.** Verdicts:
- `APPROVED` — no issues found in this agent's domain
- `CONDITIONAL PASS` — minor issues that don't block (raised as advisory flags)
- `REJECTED` — blocking issue found (route feedback, max 3 loops at T3)

**T1 re-run** (from C0) must also pass. If T1 passes at C0 but fails at C3 due to specialist-requested fixes, Code Architect fixes and T1 re-runs with its own 3-retry budget.

**T3 failures route to relevant specialists** (current behavior). T1 failures route to Code Architect. Budgets are independent.

### Final Verdict
- **T1 passes AND ALL 6 APPROVED AND T-ARCH passes** → Commit with conventional commit message
- **T1 fails** → Code Architect fixes, re-run T1 (max 3 loops)
- **ANY T3 REJECTED** → Route feedback to Code Architect, fix, re-submit to rejecting agent(s) only (max 3 loops at T3)
- **T-ARCH fails** → Software Engineer fixes architectural violation (max 3 loops at T-ARCH)
- **3 loops exhausted at any tier** → Escalate to user with all findings

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

> **Note:** Checks 1–3 and 5–7 overlap with T1 mechanical checks. The specialist review provides redundancy — T1 catches them early and cheaply; T3 catches anything T1 missed.

### Verdict
- **APPROVED** → Commit with conventional commit message
- **REJECTED** → Route feedback, fix, re-validate (max 3 loops)

---

## Retry Protocol

Each compliance tier has an **independent** 3-retry budget at each gate.

### Independent Counters

| Tier | Retry budget per gate | Who handles failures |
|------|----------------------|---------------------|
| T1 (Mechanical) | 3 retries per gate | Code Architect (automated fixes) |
| T2 (Architectural) | 3 retries per gate | Code Architect with Software Engineer input |
| T3 (Semantic) | 3 retries per gate | Relevant specialist(s) |
| T-ARCH (Architecture + Principles) | 3 retries per gate | Software Engineer (code/design); Supreme Leader (routing) |

**Key rule:** T1, T2, T3, and T-ARCH retry counters are independent. A T1 failure does not consume the T2, T3, or T-ARCH budget. Worst case per gate is 12 loops (3×T1 + 3×T2 + 3×T3 + 3×T-ARCH), not 3 total.

### Retry Routing

| Failure tier | Routes to | What happens |
|-------------|-----------|--------------|
| T1 | Code Architect | Fix mechanical violations (build, Doxygen, patterns, enums, magic numbers, reserved bits) |
| T2 | Code Architect + Software Engineer | Fix architectural violations (boundary, namespace, placement, API surface, globals) |
| T3 | Relevant specialist(s) | Fix semantic violations (datasheet, protocol, security, test, docs) |
| T-ARCH | Software Engineer (code/design); Supreme Leader (routing) | Fix architectural principle violations (consistency, structure, alignment, completeness, routing) |

### Escalation

After 3 loops at the same tier → **ESCALATE to user** with:
- Full violation log (what failed, which check, which tier)
- Number of retry attempts
- Specific files and lines that failed
- Suggested remediation if known

The user decides: relax the requirement, fix manually, or restructure.

### Gate-by-Gate Summary

| Gate | Tiers | Retry budget | Escalation trigger |
|------|-------|-------------|-------------------|
| A-GATE | T3 + T-ARCH | 3×T3, 3×T-ARCH (indep) | 3 loops at same tier |
| B-UNIT-GATE | T1 + T-ARCH | 3×T1, 3×T-ARCH (indep) | 3 T1 or 3 T-ARCH loops on same unit |
| B-FINAL-GATE | T1 + T2 + T-ARCH | 3×T1, 3×T2, 3×T-ARCH (indep) | 3 loops at same tier |
| C-GATE | T1 + T3 + T-ARCH | 3×T1, 3×T3, 3×T-ARCH (indep) | 3 loops at same tier |

---

## Self-Reflection on Compliance Violations

When a T1 or T2 check catches a violation, the Code Architect **must** ask:

1. **Why was this violation not caught during implementation?** — What review, test, or protocol gap allowed it through?
2. **What procedural safeguard would prevent recurrence?** — What specific check, test, or verification step would have prevented it?
3. **Update the knowledge base** — Add the lesson to:
   - This pipeline doc (if the T1/T2 check itself needs improvement)
   - The relevant skill (`/home/huyang/projects/esp32/.opencode/skills/nrf24l01plus/SKILL.md` for nRF24 hardware bugs)
   - `docs/learning/` (for generalisable lessons)

This mirrors the existing nRF24L01+ self-reflection clause but applies to **all** compliance violations, not just hardware bugs.

**Implementation:** After fixing a T1 or T2 violation, the Code Architect must add a brief note to the PAU unit report:

```
SELF-REFLECTION:
  Violation: [T1.5 — raw uint8_t in public API]
  Why missed: [No grep check in editor; typed overload was planned but forgotten]
  Safeguard: [Add pre-commit hook that greps for public uint8_t params]
  Knowledge update: [Added to docs/learning/typed-api-enforcement.md]
```

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
| Type       | `task` / `clarification` / `decision` / `advisory` |
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
3. **Run B-UNIT-GATE (T1 + T-ARCH) after each unit** → all 9 T1 + 5 T-ARCH checks must pass
4. Report progress between units
5. Only after ALL units pass B-UNIT-GATE → run B-FINAL-GATE (T1+T2+T-ARCH)
6. Only after B-FINAL-GATE passes → proceed to Phase C
7. Never commit code that doesn't build AND pass all T1 checks

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

### Review Round Content (Compliance Violations)

When a review round includes compliance violations, the round file must include:

```markdown
## Compliance Check Results

### T1 Mechanical
| # | Check | Pass/Fail | Detail |
|---|-------|-----------|--------|
| T1.1 | Build passes | ✅/❌ | [error/warning detail] |
| T1.2 | Doxygen | ✅/❌ | [missing symbols] |
| ... | ... | ... | ... |

### T2 Architectural (B-FINAL-GATE and C-GATE only)
| # | Check | Pass/Fail | Detail |
|---|-------|-----------|--------|
| T2.1 | Library/platform boundary | ✅/❌ | [violations] |
| ... | ... | ... | ... |

### Self-Reflection (if violations found)
[Self-reflection notes per the Self-Reflection section above]
```
