# Pipeline Workflow — Plain-English Guide

> This document explains how the multi-agent validation pipeline works. It's written for humans who want to understand the process without reading the formal specification or the agent-facing skill files.

---

## 1. Overview

### What Is the Pipeline?

The pipeline is a **multi-agent quality assurance system** for the ESP32 nRF24L01+ project. Before any code reaches the git repository, it must pass through three phases and four gates, each enforced by specialized AI agents with independent verification.

### Why Does It Exist?

When developing embedded firmware that talks directly to hardware, mistakes are expensive:

| Problem | What Happens Without the Pipeline |
|---------|-----------------------------------|
| Wrong register value | The radio silently corrupts data — no exception, no error message |
| Platform coupling in library | Can't port the nRF24L01+ driver to a different MCU |
| Missing documentation | Future developers (or future-you) can't understand design decisions |
| Security vulnerability | Buffer overflow or stack corruption in a constrained embedded environment |
| Magic numbers in API | Callers pass `0x03` when they mean "1 Mbps data rate" — and the compiler can't help |

The pipeline exists to catch these problems **before** they're committed, through a structured process with defined checks, defined retries, and defined escalation paths.

### What Problems Does It Solve?

1. **No unreviewed code** — Every change must pass both automated checks and specialist review
2. **No skipped quality steps** — Gates are mandatory; you can't proceed until every check passes
3. **No infinite loops** — Each check tier has a 3-retry budget; if exhausted, the work escalates to the human user
4. **No silent regression** — A T1 re-run at C-GATE catches any fixes introduced during Phase C that broke earlier checks
5. **No guessing** — The No-Assumption Protocol halts work on ambiguity rather than proceeding on wrong information

---

## 2. The Three Phases

The pipeline has three sequential phases. Each must complete before the next begins.

### Phase A — Requirements & Design

**Purpose:** Figure out *what* to build and *how* to build it, before writing any code.

**What happens:**

1. **A0 — Task Definition:** All agents collaborate to produce a detailed task specification with acceptance criteria, files to create/modify, constraints, test strategy, and documentation plan.

2. **A1 — Parallel Specialist Review:** Six specialist agents review the proposal independently and in parallel. Each focuses on their domain:
   - Software Engineer → architecture, API design, component boundaries
   - Hardware Engineer → register models, bit layouts, timing constraints
   - Wireless Expert → RF protocol, BLE spec, channel mapping
   - Security Reviewer → buffer safety, stack depth, input validation
   - Test Engineer → testability, edge cases, static_assert feasibility
   - Docs Writer → Doxygen coverage, learning docs, references

3. **A2 — Dual-Model Challenge:** Two independent AI model passes review the architecture. The "primary" produces the design; the "challenger" tries to break it — finding contradictions, missed edge cases, and unsupported assumptions. The Supreme Leader synthesizes the best from both passes.

4. **A3 — A-GATE:** All six specialists must issue APPROVED or CONDITIONAL PASS. Any REJECTED triggers a loop back to A1 with specific critique (up to 3 retries).

**Key rule:** No code is written in Phase A. This is purely design and review.

**Analogy:** Like an architecture review board for a building — you don't start pouring concrete until the structural engineers, fire safety, and accessibility experts have all signed off.

---

### Phase B — Build (PAU Loop)

**Purpose:** Implement the design incrementally, one unit at a time, with validation after each unit.

**What happens:**

1. **B1 — PLAN:** The Code Architect reads the task, identifies files, lists acceptance criteria, and declares logical units of work.

2. **B2 — APPLY (per unit):** The Code Architect implements one logical unit, then runs `idf.py build` to verify it compiles cleanly (zero warnings, `-Werror` is active).

3. **B2a — B-UNIT-GATE:** After each unit, automated T1 mechanical checks run. If any fail, the unit must be fixed before proceeding to the next one.

4. **B3 — VALIDATE:** After all units pass their unit gates, a full build verification runs. If hardware is connected, the firmware can be flashed.

5. **B3a — B-FINAL-GATE:** A comprehensive T1 + T2 + T-ARCH check on the entire codebase (not just the last unit). Only after this passes can work proceed to Phase C.

**Key rules:**
- **Never implement everything at once** — one unit at a time
- **Never skip validation between units** — build after every unit
- **Never skip B-UNIT-GATE** — T1 + T-ARCH must pass after each unit
- **All new public symbols must have Doxygen documentation**

**Analogy:** Like building a house room by room and having the inspector check each room before moving to the next, rather than building the entire house and then discovering the plumbing is wrong.

---

### Phase C — Multi-Agent Verification

**Purpose:** Final check before commit. All six specialist agents must approve independently.

**What happens:**

1. **C0 — T1 Re-run:** The T1 mechanical check suite runs again on the final codebase. This catches any regressions introduced during fixes between B-FINAL-GATE and now.

2. **C1 — Dual-Model Challenge (Verification):** Two independent model passes review the implemented code. Primary verifier checks against acceptance criteria; challenger tries adversarial review to find what was missed.

3. **C2 — Parallel Specialist Approval:** All six specialists review the final implementation independently, each from their domain perspective.

4. **C3 — C-GATE:** T1 must pass (from C0) AND all six specialists must issue APPROVED or CONDITIONAL PASS AND T-ARCH must pass.

**Final verdict:**
- **All APPROVED** → Commit with conventional commit message
- **Any REJECTED** → Route feedback, fix, re-submit (max 3 loops at T3)
- **3 loops exhausted** → Escalate to human user with all findings

**Analogy:** Like a final quality inspection where six independent inspectors each focus on their specialty (structural, electrical, plumbing, fire safety, accessibility, code compliance) and all must sign off before the building is approved for occupancy.

---

## 3. The Four Gates

Gates are **mandatory checkpoints**. Work cannot proceed past a gate until all required checks pass. Think of them as security checkpoints at an airport — you can't skip any of them, and each one checks different things.

### A-GATE — Exiting Phase A

| Property | Value |
|----------|-------|
| **When** | Between Phase A (design) and Phase B (build) |
| **What it checks** | T3 (semantic) + T-ARCH (principles) |
| **Who runs it** | All 6 specialists (T3), Software Engineer (T-ARCH) |
| **Pass criteria** | All 6 APPROVED or CONDITIONAL PASS + T-ARCH passes |
| **Fail action** | Loop back to A1 with specific critique |
| **Retry budget** | 3 loops at T3, 3 loops at T-ARCH (independent) |

**Why T3 + T-ARCH (no T1/T2)?** Phase A is a design phase. There's no code yet, so mechanical checks (T1) aren't meaningful. Architecture is the subject matter being reviewed, so T2 is implicitly covered by the T3 specialist review. T-ARCH is included because logical consistency, structural soundness, and principle alignment can be validated on design documents — no code is needed.

---

### B-UNIT-GATE — After Each Implementation Unit

| Property | Value |
|----------|-------|
| **When** | After each logical unit in the PAU loop |
| **What it checks** | T1 (mechanical) + T-ARCH (principles) |
| **Who runs it** | Code Architect (T1), Software Engineer (T-ARCH) |
| **Pass criteria** | All 9 T1 checks pass + T-ARCH passes |
| **Fail action** | Fix the unit, re-check |
| **Retry budget** | 3 loops per unit at T1, 3 loops at T-ARCH (independent) |

**Why this gate exists:** Previously, Phase B had no quality gate. Mechanical violations (missing Doxygen, raw integers, magic numbers) leaked to Phase C where they wasted specialist time. B-UNIT-GATE catches these early, per unit, at negligible cost.

---

### B-FINAL-GATE — Exiting Phase B

| Property | Value |
|----------|-------|
| **When** | Between Phase B and Phase C |
| **What it checks** | T1 (mechanical) + T2 (architectural) + T-ARCH (principles) |
| **Who runs it** | Code Architect (T1), Software Engineer (T2 + T-ARCH) |
| **Pass criteria** | T1 passes AND T2 passes AND T-ARCH passes |
| **Fail action** | Fix violations, re-run the failing tier |
| **Retry budget** | 3 loops per tier (independent counters) |

**Why T1 is re-run:** The full T1 suite runs on the complete codebase, not just the last unit. This acts as a final mechanical sweep before specialist review.

---

### C-GATE — Entering Commit

| Property | Value |
|----------|-------|
| **When** | After Phase C specialist review, before git commit |
| **What it checks** | T1 re-run + T3 (semantic) + T-ARCH (principles) |
| **Who runs it** | Code Architect (T1), All 6 specialists (T3), Software Engineer (T-ARCH) |
| **Pass criteria** | T1 passes AND all 6 APPROVED AND T-ARCH passes |
| **Fail action** | Route to appropriate fixer per tier |
| **Retry budget** | 3 loops per tier (independent counters) |

**Why T1 is re-run again:** Fixes requested by specialists during C2 can introduce new mechanical violations. The T1 re-run at C-GATE catches these regressions.

---

## 4. The Compliance Tiers

Compliance checks are organized into four tiers, each with increasing scope and specialist involvement. Tiers are run sequentially at each gate — a lower tier must pass before the next tier runs.

### T1 — Mechanical (Automated)

**What:** Checks that can be verified by automated tooling. No human judgement required.

**Who runs it:** Code Architect (via `t1-check.sh` script)

**Checks:**

| # | Check | What It Catches |
|---|-------|-----------------|
| T1.1 | Build passes | Compilation errors or warnings (`-Werror` active) |
| T1.2 | Doxygen on public symbols | Missing `/** @brief */` on any public function, struct, enum, or macro |
| T1.3 | No decision references | Leftover `D-1`, `F-1`, `(decision` markers that should have been resolved |
| T1.4 | No changelog-style comments | `replaces the`, `was previously`, `formerly` — these belong in git history, not code |
| T1.5 | No raw uint8_t where typed vocabulary exists | Public API uses `uint8_t` where an `enum class` or struct type exists |
| T1.6 | No magic numbers in @code examples | Hex literals like `0x03` in Doxygen examples where named constants exist |
| T1.7 | Constants in correct module | Chip-level constants outside their library namespace |
| T1.8 | Reserved bits handled | `to_byte()`/`from_byte()` must mask reserved bits |
| T1.9 | No hardcoded secrets | No common secret patterns (password, api_key, secret, token, credential, Bearer) in .cpp/.h that aren't test fixtures or comments |

**Analogy:** Like a linter or CI check — mechanical, repeatable, binary pass/fail.

---

### T2 — Architectural (Design Judgement)

**What:** Checks that require design-level judgement about structure, boundaries, and API surface.

**Who runs it:** Software Engineer (spot-check); Code Architect implements fixes

**Checks:**

| # | Check | What It Catches |
|---|-------|-----------------|
| T2.1 | Library/platform boundary | Platform headers (ESP-IDF, FreeRTOS) leaking into the portable library |
| T2.2 | Namespace structure | Symbols in wrong namespace or global namespace |
| T2.3 | File placement | Constants in main/ instead of library, HAL in wrong component |
| T2.4 | API surface audit | `uint8_t` parameters where typed enums or structs should be used |
| T2.5 | No mutable globals in library | Global mutable state in library code (should be instance-scoped) |

**Analogy:** Like an architecture review board — these need human judgement about design intent.

---

### T3 — Semantic (Domain Expertise)

**What:** Checks that require deep domain expertise — datasheet fidelity, protocol correctness, security, test coverage, documentation completeness.

**Who runs it:** All six specialist agents, each from their domain

**Checks:**

| # | Check | Who Is Responsible |
|---|-------|-------------------|
| T3.1 | Datasheet fidelity — register fields match datasheet exactly | Hardware Engineer |
| T3.2 | Protocol correctness — BLE channel mapping, whitening, CRC | Wireless Expert |
| T3.3 | Security review — buffer bounds, stack depth, secrets, input validation | Security Reviewer |
| T3.4 | Test coverage — static_assert round-trips, edge cases | Test Engineer |
| T3.5 | Documentation completeness — Doxygen, learning docs, verified references | Docs Writer |
| T3.6 | Architectural integrity — SOLID, HAL decoupling, CMake deps | Software Engineer |

**Analogy:** Like a building inspector with a structural engineering degree — these require specialist knowledge that can't be automated.

---

### T-ARCH — Architecture + Principles (Cross-Cutting)

**What:** Structural and logical correctness of **every agent output** — not just code, but also documentation, reviews, and routing decisions.

**Who runs it:** Software Engineer (for code and design); Supreme Leader (for routing)

**Checks:**

| # | Check | What It Catches |
|---|-------|-----------------|
| T-ARCH.1 | Logical consistency | Internal contradictions, claims that conflict with evidence |
| T-ARCH.2 | Structural soundness | Missing sections, incomplete analysis, skip in reasoning |
| T-ARCH.3 | Principle alignment | Output violates established project principles (typed API, HAL decoupling, datasheet fidelity) |
| T-ARCH.4 | Completeness | Required sections or checks omitted without justification |
| T-ARCH.5 | Correct agent routing | Task routed to wrong agent type |

**Key difference from other tiers:** T-ARCH runs on **every agent output**, not just at formal gate transitions. This prevents an agent from producing well-formatted but logically flawed output that slips through because it's not at a gate boundary.

**Analogy:** Like a constitutional review — does this output uphold our founding principles, regardless of whether it's code, a design document, or a review?

---

## 5. The Retry Protocol

### How Retries Work

Each compliance tier has an **independent** 3-retry budget at each gate. This means:

- T1, T2, T3, and T-ARCH retry counters are **independent**
- A T1 failure does **not** consume the T2, T3, or T-ARCH budget
- Worst case per gate: 3×T1 + 3×T2 + 3×T3 + 3×T-ARCH = 12 loops (not 3 total)

### Which Tier Goes Where?

| Failure Tier | Who Fixes It | What Happens |
|-------------|-------------|--------------|
| T1 (Mechanical) | Code Architect | Fix build errors, add Doxygen, remove banned patterns, add typed overloads |
| T2 (Architectural) | Code Architect + Software Engineer input | Fix boundary violations, namespace issues, API surface, globals |
| T3 (Semantic) | Relevant specialist(s) | Fix datasheet errors, protocol bugs, security holes, test gaps, doc gaps |
| T-ARCH (Principles) | Software Engineer or Supreme Leader | Fix logical contradictions, structural gaps, principle violations |

### What Happens When Retries Are Exhausted

After 3 loops at the same tier at the same gate, the Supreme Leader **escalates to the human user** with:

1. **Full violation log** — what failed, which check, which tier
2. **Number of retry attempts** — how many times it was tried
3. **Specific files and lines** that failed
4. **Suggested remediation** — if known

The user then decides: relax the requirement, fix it manually, or restructure the approach.

### Gate-by-Gate Summary

| Gate | Tiers Checked | Retry Budget | Escalation Trigger |
|------|--------------|-------------|-------------------|
| A-GATE | T3 + T-ARCH | 3×T3, 3×T-ARCH | 3 loops at any tier |
| B-UNIT-GATE | T1 + T-ARCH | 3×T1, 3×T-ARCH | 3 loops at any tier |
| B-FINAL-GATE | T1 + T2 + T-ARCH | 3×T1, 3×T2, 3×T-ARCH | 3 loops at any tier |
| C-GATE | T1 + T3 + T-ARCH | 3×T1, 3×T3, 3×T-ARCH | 3 loops at any tier |

---

## 6. The Self-Reflection Clause

### Why It Exists

When a compliance check catches a violation, it's not enough to just fix the immediate problem. The pipeline needs to learn from the failure to prevent the same class of bug from recurring.

The self-reflection clause forces agents to ask: **"How did this get past our safeguards in the first place?"**

### The Three Questions

After any compliance violation is caught, the responsible agent must answer:

1. **Why was this violation not caught earlier?** — What review, test, or protocol gap allowed it through?
2. **What procedural safeguard would have caught it?** — What specific check, test, or verification step would have prevented it?
3. **Update the knowledge base** — Add the lesson to the relevant skill or learning document.

### Example

Suppose T1.5 catches a `uint8_t` parameter in the public API that should be a typed enum:

```
SELF-REFLECTION:
  Violation: T1.5 — raw uint8_t in public API
  Why missed: No grep check in editor; typed overload was planned but forgotten during implementation
  Safeguard: Add a pre-commit hook that greps for public uint8_t params
  Knowledge update: Added to docs/learning/typed-api-enforcement.md
```

### Where Lessons Are Logged

| Violation type | Log location |
|---------------|-------------|
| nRF24 hardware-specific | `.opencode/skills/nrf24l01plus/SKILL.md` |
| General compliance process | `.opencode/skills/compliance-gate/SKILL.md` |
| Learning about C++/ESP-IDF | `docs/learning/` |
| Pipeline process improvement | `docs/pipeline/pipeline.md` |

---

## 7. The OWASP Expansion Mechanism

### What Is It?

The OWASP (Open Web Application Security Project) Expansion Mechanism is how the pipeline handles **new security and compliance concerns** that emerge as the project grows. It's named after the OWASP Top 10 — the industry-standard list of web application security risks — but the mechanism applies to any new concern category.

### The Problem It Solves

When you start a project, you define your compliance checks (T1, T2, T3, T-ARCH). But as features are added, new types of concerns appear that weren't in the original checklist:

| Feature Added | New Concern Category | Required Additional Checks |
|--------------|---------------------|----------------------------|
| WiFi connectivity | Network Communication | OWASP A02 (Cryptographic Failures) + A03 (Injection) |
| Device authentication | Secrets/Credentials | Full Security Reviewer checklist + OWASP A07 (Auth Failures) |
| User data collection | PII/User Data | OWASP A01 (Broken Access Control) + privacy review |
| OTA firmware updates | Firmware Updates | OWASP A08 (Software/Data Integrity) |
| BLE advertising | Input from External Sources | OWASP A03 (Injection) — treat all RF input as untrusted |
| Debug logging | Logging/Debug Output | OWASP A09 (Security Logging) |

### How It Works

The expansion follows a 5-step protocol:

1. **Identify** — When a task is dispatched, check whether it introduces new concern categories
2. **Load** — Load the relevant compliance checks (OWASP category or project-specific skill)
3. **Add** — Add the expanded checks to the appropriate gate tier (T3 for semantic, T-ARCH for structural)
4. **Document** — Record the expansion in the gate results: `OWASP Expansion: [category] → [added checks]`
5. **Re-run** — Re-run the gate with the expanded check set

The dispatch envelope includes an `OWASP_expansion` field that tracks what categories have been added for the current task.

### Example

If a task adds WiFi connectivity to the firmware:

```yaml
OWASP_expansion: "Network Communication → OWASP A02 (Cryptographic Failures) + A03 (Injection)"
```

This means the T3 check at the next gate will include additional scrutiny of:
- Transport-layer encryption (TLS configuration)
- Certificate pinning
- Injection attack vectors via network input

---

## 8. Project Management

### Ticket Categories

The project uses 7 types of tracked items, each with a unique ID format:

| Category | ID Format | Purpose | Example |
|----------|-----------|---------|---------|
| **Ticket** | `nrf-0001` | A unit of work — a feature, bug fix, or task | `nrf-0016: Add RfSetup::to_byte() constexpr` |
| **Epic** | `nrf-epic-001` | A large body of work that spans multiple tickets | `nrf-epic-001: BLE Sniffer Implementation` |
| **Clarification** | `nrf-clar-0001` | A question that needs a decision or answer | `nrf-clar-0001: Should HAL return error or abort on SPI failure?` |
| **ADR** | `nrf-adr-0001` | Architecture Decision Record — a documented design choice | `nrf-adr-0001: Use Galois LFSR for BLE whitening` |
| **Advisory** | `nrf-adv-0001` | A non-blocking observation or recommendation | `nrf-adv-0006: ESP_ERROR_CHECK in spi_xfer defeats retry architecture` |
| **Design** | `nrf-design-0001` | A design document for a feature or component | `nrf-design-0001: Structured diagnostics module` |
| **Chore** | `nrf-chore-0001` | Housekeeping — refactoring, dependency updates, cleanup | `nrf-chore-0001: Wire test_registers.cpp into build system` |

### Ticket Lifecycle

Every ticket follows this lifecycle:

```
open → backlog → in-progress → review → closed
```

| Status | Meaning |
|--------|---------|
| **open** | Created but not yet prioritized |
| **backlog** | Prioritized and queued for work |
| **in-progress** | Actively being worked on by an agent |
| **review** | Under specialist review (Phase C) |
| **closed** | Completed and committed |

### Generating IDs with next-id.mjs

The `docs/project-management/next-id.mjs` script generates the next available ID for each category. It reads from `docs/project-management/counters.json` and atomically increments the counter.

**Usage:**

```bash
# Next ticket ID
node docs/project-management/next-id.mjs ticket
# Output: { "kind": "ticket", "ids": ["nrf-0016"], "dryRun": false }

# Next 5 ticket IDs
node docs/project-management/next-id.mjs ticket 5
# Output: { "kind": "ticket", "ids": ["nrf-0016", "nrf-0017", "nrf-0018", "nrf-0019", "nrf-0020"], "dryRun": false }

# Preview without updating counters
node docs/project-management/next-id.mjs ticket --dry-run
# Output: { "kind": "ticket", "ids": ["nrf-0016"], "dryRun": true }

# Other categories
node docs/project-management/next-id.mjs epic
node docs/project-management/next-id.mjs clarification
node docs/project-management/next-id.mjs adr
node docs/project-management/next-id.mjs advisory
node docs/project-management/next-id.mjs design
node docs/project-management/next-id.mjs chore
```

### Ticket Template

Every ticket uses the template at `docs/project-management/TEMPLATE.md`, which includes:

- **No-Assumption Check** — Verify datasheet details, protocol details, design decisions, and scope boundaries before starting
- **Acceptance Criteria** — Binary pass/fail criteria, each independently verifiable
- **Compliance Requirements** — Which compliance skills must be loaded
- **Files Affected** — List of files to create or modify

### Flag Protocol

When an agent identifies work that needs attention but doesn't have the authority to create a ticket, it raises a **flag** instead. Only the PM creates actual tickets.

```markdown
## Flag: advisory — MOSI GPIO direction bug

| Field      | Value |
|-----------|-------|
| Type       | advisory |
| Priority   | high |
| Raised by  | Hardware Engineer |
| Blocking   | no |
| Reference  | nrf-0012 |

## Description
main.cpp:133 sets MOSI to INPUT mode after SPI init, breaking all subsequent SPI writes.

## Evidence
SPI register readback shows MOSI direction = INPUT after init sequence.

## Suggested action
Remove lines 132-134 from main.cpp to prevent MOSI direction override.
```

**Routing:**

| Blocking? | Route | Effect |
|-----------|-------|--------|
| Yes | Supreme Leader pauses pipeline | Presented to user immediately |
| No | Queued for PM | PM creates task in next planning cycle |
| Advisory | Logged | Persisted for future reference |

---

## 9. Agent Dispatch and State Machine

### How Agents Are Dispatched

The Supreme Leader dispatches agents using a **structured envelope** that preserves context across handoffs. Every dispatch carries:

```yaml
ticket: "nrf-0016"                    # Which task this is for
phase: "B"                             # Current pipeline phase (A, B, or C)
step: "B2a"                            # Current step within the phase
trigger: "B-UNIT-GATE failed: T1.5"   # Why this dispatch occurred
agent: "code-architect"                # Which agent is being dispatched
passport: "docs/project-management/passports/nrf-0016-passport.md"  # Pipeline passport
skills_loaded:
  - "assumption-trap"
  - "compliance-gate"
  - "pipeline"
  - "pau-loop"
  - "nrf24l01plus"
expected_outcomes:
  - "Fix T1.5 violation in driver.h:142"
  - "Add typed overload for write_reg()"
next_agent: "code-architect"           # Who receives the output next
retry_count:
  T1: 2                                # Current T1 retries at this gate
  T2: 0                                # Current T2 retries at this gate
  T3: 0                                # Current T3 retries at this gate
  T-ARCH: 0                             # Current T-ARCH retries at this gate
OWASP_expansion: "none"                # Any OWASP categories added
```

### State Machine

The pipeline is a state machine with defined transitions. Here's how work flows through it:

```
A0 (Task Definition)
  ↓
A1 (Parallel Specialist Review)
  ↓
A2 (Dual-Model Challenge)
  ↓
A3 (A-GATE: T3 + T-ARCH)
  │ FAIL → loop back to A1 (max 3× per tier)
  ↓ PASS
B1 (PLAN)
  ↓
B2 (APPLY — one unit)
  ↓
B2a (B-UNIT-GATE: T1 + T-ARCH)
  │ FAIL → fix unit, retry (max 3× per tier)
  ↓ PASS
  → More units? → YES: back to B2
                   NO: continue to B3a
  ↓
B3a (B-FINAL-GATE: T1 + T2 + T-ARCH)
  │ FAIL → fix, retry (max 3× per tier)
  ↓ PASS
C0 (T1 Re-run)
  │ FAIL → Code Architect fixes (max 3×)
  ↓ PASS
C1 (Dual-Model Challenge — verification)
  ↓
C2 (Parallel Specialist Approval — T3)
  ↓
C3 (C-GATE: T1 + T3 + T-ARCH)
  │ FAIL → route to appropriate fixer (max 3× per tier)
  ↓ PASS
COMMIT (git commit with conventional commit message)
```

**At any point:** If any tier exhausts its 3-retry budget, the Supreme Leader escalates to the user with a full violation report.

### Agent Routing Table

| Intent | Agent | Skills Loaded |
|--------|-------|--------------|
| Architecture design | Software Engineer | assumption-trap, compliance-gate, type-design-review |
| Register model design | Hardware Engineer | assumption-trap, datasheet-verification, nrf24l01plus |
| RF protocol design | Wireless Expert | assumption-trap, datasheet-verification, nrf24l01plus, ble-protocol |
| Security analysis | Security Reviewer | assumption-trap, silent-failure, memory-safety |
| Test strategy | Test Engineer | assumption-trap, test-driven-development |
| Documentation plan | Docs Writer | assumption-trap, verification-before-completion |
| Implementation | Code Architect | pau-loop, incremental-execution, nrf24l01plus, compliance-gate |
| T1 compliance check | Code Architect | compliance-gate, verification-before-completion |
| T2 architectural review | Software Engineer | compliance-gate, type-design-review |
| T3 semantic review | All 6 specialists | compliance-gate, domain-specific skills |
| T-ARCH review | Software Engineer | compliance-gate, type-design-review |
| Memory safety review | Memory Safety | assumption-trap, memory-safety |
| Gate orchestration | Supreme Leader | pipeline, compliance-gate, flag-protocol |
| Task creation | PM | pipeline, flag-protocol |
| Debugging | Code Architect | systematic-debugging, nrf24l01plus |

---

## 10. Skill Loading

### How Agents Decide Which Skills to Load

Skills are pre-packaged knowledge modules that agents load before starting work. The loading order matters — `assumption-trap` must always be loaded first because it establishes the "halt on ambiguity" protocol that prevents agents from guessing.

### Mandatory Loading Order

When a task is dispatched, skills are loaded in this sequence:

1. **`assumption-trap`** — Always first. Halts on ambiguity; never guesses register values or hardware behavior.
2. **Domain-specific skills** — Based on the task and tech stack (see table below)
3. **Phase-specific skills** — Based on which pipeline phase we're in
4. **Process skills** — Based on the type of work (implementation, debugging, review)

### Core Skills (Always Loaded)

These are loaded for every task, regardless of phase or domain:

| Skill | Purpose |
|-------|---------|
| `assumption-trap` | Halt on ambiguity — never guess hardware values |
| `compliance-gate` | Tiered checks (T1/T2/T3/T-ARCH) and OWASP expansion |
| `pipeline` | State machine, phases, gates, agent dispatch |
| `pau-loop` | Plan-Apply-Validate cycle for implementation |
| `verification-before-completion` | No claims without fresh build evidence |
| `self-audit-checklist` | 10-point quality checklist before verdicts |

### Domain Skills (Loaded Based on Task)

| Task Involves | Skills to Load |
|---------------|---------------|
| nRF24L01+ radio configuration | `nrf24l01plus`, `datasheet-verification` |
| ESP-IDF / ESP32 platform | `esp-idf` |
| BLE protocol | `ble-protocol` |
| C++ embedded patterns | `cpp-embedded` |
| Memory safety | `memory-safety` |
| Ubertooth testing | `ubertooth` |
| nRF52840 sniffer | `nrf52840-sniffer` |

### Phase Skills

| Phase | Skills |
|-------|--------|
| Phase A (Design) | `brainstorming` (structured design dialogue) |
| Phase B (Build) | `incremental-execution` (build-validate-next unit), `test-driven-development` |
| Phase C (Verify) | `grill-me` (adversarial review) |

### Process Skills

| Activity | Skills |
|----------|--------|
| Debugging | `systematic-debugging` (hypothesis-driven, root-cause first) |
| Review (Phase C) | `self-audit-checklist`, `review-confidence` |
| Design decisions | `datasheet-verification` (verify against datasheets, not training data) |
| Type/API design | `type-design-review` |

### The Tech Stack Reference Table

This is documented in `AGENTS.md` and should be consulted before any task:

| Component | Skill to Load |
|-----------|--------------|
| nRF24L01+ radio | `nrf24l01plus` |
| ESP-IDF / ESP32 | `esp-idf` |
| BLE protocol | `ble-protocol` |
| C++ embedded patterns | `cpp-embedded` |
| Memory safety | `memory-safety` |
| Ubertooth testing | `ubertooth` |
| nRF52840 sniffer | `nrf52840-sniffer` |

---

## Quick Reference: End-to-End Flow

Here's the complete pipeline in one view:

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           PHASE A: DESIGN                                   │
│  A0: Define task (all agents)                                               │
│  A1: 6 specialists review independently                                     │
│  A2: Dual-Model Challenge (primary vs challenger)                           │
│  A3: A-GATE ── T3 + T-ARCH ── all 6 must APPROVE                           │
│      FAIL → loop to A1 (max 3× per tier)                                   │
└───────────────────────────────────┬─────────────────────────────────────────┘
                                    │ PASS
                                    ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                           PHASE B: BUILD                                    │
│  B1: Plan units (Code Architect)                                            │
│  B2: Apply one unit → build                                                 │
│  B2a: B-UNIT-GATE ── T1 + T-ARCH ── after each unit                        │
│       FAIL → fix unit (max 3× per tier)                                    │
│  ... repeat for each unit ...                                               │
│  B3a: B-FINAL-GATE ── T1 + T2 + T-ARCH ── on complete codebase             │
│       FAIL → fix (max 3× per tier)                                         │
└───────────────────────────────────┬─────────────────────────────────────────┘
                                    │ PASS
                                    ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                           PHASE C: VERIFY                                   │
│  C0: T1 re-run on final codebase                                            │
│  C1: Dual-Model Challenge (verification)                                    │
│  C2: 6 specialists approve independently (T3)                               │
│  C3: C-GATE ── T1 + T3 + T-ARCH ── all 6 must APPROVE                      │
│      FAIL → route to appropriate fixer (max 3× per tier)                  │
└───────────────────────────────────┬─────────────────────────────────────────┘
                                    │ PASS
                                    ▼
                              ┌──────────┐
                              │  COMMIT  │
                              └──────────┘
```

**At any tier exhaustion (3 retries):** Supreme Leader escalates to the human user.

**Self-reflection after any violation:** Why was it missed? What safeguard would catch it? Update the knowledge base.

---

*This document is a companion to `docs/pipeline/pipeline.md` (the formal specification) and `.opencode/skills/pipeline/SKILL.md` (the agent-facing state machine). For the complete tier definitions, checklists, and violation report formats, see those documents.*

---

## 11. Pipeline Passport

Every task carries a **passport** that tracks which pipeline steps have been completed. No step may be skipped without written justification.

**Key rules:**

1. **No step without a stamp** — every step must be checked off before the next step begins
2. **No skip without justification** — a written justification and Supreme Leader authorisation are required
3. **Loops are tracked** — every A→B→A loop is recorded in the passport's Loop History section
4. **Passport travels with dispatch** — the passport file path is included in every dispatch envelope

Passports are stored in `docs/project-management/passports/<ticket-id>-passport.md` and are created by the PM when a ticket is opened. For the full passport format and rules, see `.opencode/skills/pipeline-passport/SKILL.md`.
