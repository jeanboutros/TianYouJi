# ESP32 nRF24L01+ Project — Agent Configuration

## Tech Stack

| Component | Skill to Load |
|-----------|---------------|
| nRF24L01+ radio | `nrf24l01plus` |
| ESP-IDF / ESP32 | `esp-idf` |
| BLE protocol | `ble-protocol` |
| C++ embedded patterns | `cpp-embedded` |
| Memory safety | `memory-safety` |
| Ubertooth testing | `ubertooth` |
| nRF52840 sniffer | `nrf52840-sniffer` |

## Skill Loading Protocol

All agents follow this initialisation sequence:

1. **Assumption trap** — load `assumption-trap` FIRST before any analysis
2. **Domain skills** — load skills from the Tech Stack table relevant to the task
3. **Pipeline skills** — load `pipeline` when working with gates or phase transitions; load `pipeline-passport` for task tracking
4. **Process skills** — load `pau-loop`, `incremental-execution`, `test-driven-development`, or `systematic-debugging` as the task requires
5. **Review skills** — load `self-audit-checklist` before any Phase C verdict; load `datasheet-verification` before confirming any register value

## Skill Registry

| Skill | Trigger Condition |
|-------|-------------------|
| `assumption-trap` | First skill loaded in any session — halt on ambiguity |
| `pau-loop` | Plan-Apply-Validate cycle for implementation tasks |
| `incremental-execution` | Build-validate-next unit protocol |
| `test-driven-development` | Red-green-refactor with static_assert |
| `systematic-debugging` | Root-cause before fix — no random edits |
| `verification-before-completion` | No claims without fresh build evidence |
| `brainstorming` | Structured design dialogue before code — hard gate |
| `grill-me` | Adversarial review (Dual-Model Challenge) |
| `self-audit-checklist` | 10-point quality checklist before verdicts |
| `flag-protocol` | Raise issues for PM attention |
| `datasheet-verification` | Verify hardware claims against datasheets |
| `pipeline` | Pipeline phases, gates, compliance tiers, agent roles |
| `compliance-gate` | T1/T2/T3/T-ARCH gate checks and retry budgets |
| `pipeline-passport` | Pipeline passport tracking — every task carries a passport with stamped steps |
| `nrf24l01plus` | nRF24L01+ chip traps, diagnostics, register order |
| `esp-idf` | ESP-IDF build, FreeRTOS, SPI, GPIO, monitor, VS Code |
| `cpp-embedded` | Typed enums, register structs, Doxygen, HAL, platform independence |
| `ble-protocol` | BLE advertising, PDU types, whitening, bit order, CRC-24 |
| `memory-safety` | RAII, buffer safety, ASAN, heap/stack analysis |
| `ubertooth` | Ubertooth One BLE TX/RX, firmware, dewhitening |
| `nrf52840-sniffer` | Nordic dongle BLE capture, Wireshark extcap |
| `review-confidence` | Confidence scoring for design reviews |
| `silent-failure` | Detecting silent hardware/software failures |
| `type-design-review` | Reviewing enum/struct API design quality |

## Pipeline Summary

```
Phase A ──▶ A-GATE (T3+T-ARCH) ──▶ Phase B ──▶ B-UNIT-GATE (T1+T-ARCH)×N ──▶ B-FINAL-GATE (T1+T2+T-ARCH) ──▶ Phase C ──▶ C-GATE (T1+T3+T-ARCH) ──▶ COMMIT
```

T1 = Mechanical (build, Doxygen, banned patterns) · T2 = Architectural (library boundary, namespaces, typed API) · T3 = Semantic (datasheet, protocol, security) · T-ARCH = Architecture + Principles (cross-cutting consistency, structure, principle alignment)

See `pipeline` skill for full definitions, agent roles, and gate retry rules.

## Agent Principles

1. **No assumptions** — Halt on ambiguity; never guess register values, bit layouts, or protocol details. Load `assumption-trap`.
2. **Datasheet is truth** — Verify all hardware claims against `docs/datasheets/`. If unclear, check the web. If still unclear, halt.
3. **PAU loop** — Plan units → Apply one at a time → Validate with `idf.py build`. Never implement everything at once.
4. **Quality gates** — Build must pass with zero warnings (`-Werror` active). Run `t1-check.sh` between units and at gates.
5. **Typed API only** — No raw integers in the public API. Every finite-value field is an `enum class`. See `cpp-embedded` skill.
6. **Platform independence** — Library public headers include only `<cstdint>`, `<cstring>`, and own headers. All hardware access through the `Hal` interface. See `cpp-embedded` skill.
7. **Self-reflection** — After any bug fix, ask: why was it missed? What safeguard would catch it? Update the skill or learning doc.
8. **Incremental with evidence** — One logical unit → build → pass → next unit. Never skip validation. Load `verification-before-completion`.

## Project Details

| Item | Value |
|------|-------|
| ESP-IDF path | `~/.espressif/v6.0.1/esp-idf/` |
| Build / verify | `source ~/.espressif/tools/activate_idf_v6.0.1.sh && idf.py build` |
| T1 check | `bash docs/pipeline/scripts/t1-check.sh` |
| Flash | `idf.py -p /dev/ttyUSB0 flash monitor` |
| Serial port | `/dev/ttyUSB0` (verify with `ls /dev/ttyUSB*`) |
| CE pin | GPIO4 (was GPIO5 — migrated to avoid SPI3 IO_MUX overlap; see `nrf24l01plus` skill §1.3) |
| Pipeline spec | `docs/pipeline/pipeline.md` |
| Agent roles | `.opencode/skills/pipeline/SKILL.md` |
| Task tracker | `docs/pipeline/TODO.md` |

## Git Commit Rules

### Commit after every completed task

After completing any task — code change, doc update, config edit — always commit before moving on.

### Use Conventional Commits format

```
<type>(<scope>): <short summary>

<body — what changed and why, bullet points if multiple items>
```

| Type | Use for |
|------|---------|
| `feat` | New feature or capability |
| `fix` | Bug fix |
| `docs` | Documentation only (`docs/learning/`, `README.md`, `AGENTS.md`) |
| `chore` | Tooling, config, build system |
| `refactor` | Code restructured without behaviour change |
| `style` | Formatting only (no logic change) |

### Group commits by concern — never bundle unrelated changes

Each commit must contain **only related changes**. Split into separate commits when changes span unrelated areas.

### Commit body guidelines

- Explain **what** changed and **why**, not just how.
- Use bullet points for multiple related changes within the same scope.
- Reference the source of truth when relevant (e.g. datasheet page, ESP-IDF docs URL).
- Verify URLs before linking — never rely on training data for factual claims.

### Example

```
docs(learning): add FreeRTOS task pinning guide

- Add docs/learning/freertos-task-pinning.md covering xTaskCreatePinnedToCore
- Add docs/learning/esp32-app-main-basics.md covering firmware structure
- Update docs/learning/INDEX.md with new entries under FreeRTOS and ESP-IDF Basics
```
