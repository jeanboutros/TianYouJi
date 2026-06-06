# Pipeline TODO

Task tracking for the ESP32 nRF24L01+ project.
Status: `[ ]` pending, `[~]` active, `[x]` done, `[!]` blocked

---

## Active

- [~] **Review Pipeline** — Sessions 1-6 primary + dewhiten fix done; challenger pass 3/6 done (SW, HW, wireless); Phase C structured diagnostics review in progress

---

## Backlog

- [ ] **HIGH: Replace ESP_ERROR_CHECK in Hal::spi_xfer() with error propagation** — Phase C finding F-1. `ESP_ERROR_CHECK` causes `abort()` on any SPI transfer failure, defeating the diagnostic retry architecture. Requires changing `spi_xfer()` from `void` to return `bool`, propagating through all Driver methods, and updating all callers. (Security reviewer, severity HIGH)
- [ ] *See also F-4:* `EspIdfHal::init()` uses `ESP_ERROR_CHECK` for initialization (appropriate) but has no RAII destructor for cleanup — medium severity, pre-existing.
- [ ] **BUG: Remove MOSI GPIO_MODE_INPUT from main.cpp:133** — Challenger confirmed this breaks all SPI writes after init (switch_channel, clear_irq). Must remove lines 132-134.
- [ ] Challenger pass reviews: security, test, docs, memory-safety (4 remaining)
- [ ] Session 7: HAL adapter review (`components/nrf24_espidf/`)
- [ ] Session 8: Driver implementation review (missing SPI commands)
- [ ] Session 9: Application firmware review (`main/main.cpp`)
- [ ] Session 10: Build system review
- [ ] Session 11: Pipeline governance review
- [ ] Session 12: Adversarial challenge synthesis (Dual-Model Challenge)
- [ ] ESP32 live dewhitening validation (requires dialout group for serial port)
- [ ] Wire test_registers.cpp into build system (currently orphaned)
- [ ] Add RfSetup::to_byte() constexpr
- [ ] Add HAL IRQ pin abstraction
- [ ] Add missing Driver SPI commands (W_TX_PAYLOAD, W_TX_PAYLOAD_NOACK, REUSE_TX_PL, R_RX_PL_WID, W_ACK_PAYLOAD, NOP)
- [ ] Create learning docs for: channel mapping, HAL porting, non-contiguous encoding
- [ ] Fix uint8_t overflow in RxConfig::total_channels() when extra_channel_count >= 253
- [ ] Address memory-safety findings F1 (EspIdfHal destructor), F3 (null guard channel_at)

---

## Done

- [x] **Structured diagnostics module** — `diag_boot.h` with `DiagPhase`, `DiagResult`, `DiagFullResult`, `DiagOpts`, and `full_boot_diagnostic()`. 7 hardware verification criteria PASS: power-on delay, independent address verification, EN_AA/EN_CRC write order, CE GPIO readback, warm boot handling, clone detection, self-consistent error detection. Build passes with zero warnings. (Phase C: HW engineer APPROVED, security reviewer CONDITIONAL — F-1 `ESP_ERROR_CHECK` in `spi_xfer` is HIGH, tracked in Backlog)
- [x] BLE dewhiten() fix: bit-swap + Galois LFSR + correct seed (Dmitry Grinberg reference)
- [x] swapbits() utility function added to ble.h
- [x] Host-side unit tests for swapbits() and dewhiten() (23 tests, all passing, ASAN enabled)
- [x] Learning doc: ble-data-whitening-nrf24.md (8 sections, full bug analysis, code breakdown)
- [x] Ubertooth hardware validation test plan (006-ubertooth-dewhiten-test-plan.md)
- [x] Python reference implementation (ble_whiten_reference.py, 35 round-trip tests pass)
- [x] Memory-safety skill created (`.opencode/skills/memory-safety/SKILL.md`)
- [x] Memory-safety agent created (`.opencode/agents/memory-safety.md`)
- [x] Memory-safety review completed (CONDITIONAL PASS, severity 6, 7 findings)
- [x] Primary pass reviews completed (6/6 specialists)
- [x] BLE whitening reference implementation research (6 projects, all use Galois LFSR)
- [x] nemotron-3-ultra:cloud model configured in opencode provider
- [x] Challenger pass: SW-engineer (011), HW-engineer (012), wireless-expert (013) — dewhiten APPROVED by wireless challenger
- [x] Ubertooth BLE testing learning doc (11 sections, Python+C++ examples, 6 firmware testing techniques)
- [x] Ubertooth TX capability fixed and verified (100 packets on ch37)
- [x] Dewhiten validation: 23 unit tests PASS + 35 Python round-trips PASS
- [x] Learning doc nrf24-ble-packet-crafting.md corrected (bit order claim fixed, LFSR updated)

---

## Decisions

| ID | Date | Decision | Context |
|----|------|----------|---------|
| D1 | 2026-06-06 | Agent model field removed from all 8 agents (inherit session default) | Model diversity via session config |
| D2 | 2026-06-06 | Memory-safety skill created from scratch (no existing skill found on skills.sh or GitHub) | User requested memory leak/C++ best practices coverage |
| D3 | 2026-06-06 | BLE whitening Bug #1 confirmed: missing bit-swap before dewhiten() | Wireless-expert analysis; nRF24 MSB-first vs BLE LSB-first |
| D4 | 2026-06-06 | BLE whitening Bug #2 resolved: Galois LFSR is correct form | All 6 reference projects use Galois (Dmitry Grinberg origin) |
| D5 | 2026-06-06 | dewhiten() encapsulates bit-swap internally | Prevents callers from forgetting the swap |
| D6 | 2026-06-06 | Seed formula: swapbits(channel) \| 2 (Dmitry Grinberg btLeWhitenStart) | Confirmed by all reference implementations |
| D7 | 2026-06-06 | Agent outputs stored in docs/pipeline/logs/ sequentially | User requirement #5 |
| D8 | 2026-06-06 | nemotron-3-ultra:cloud configured as challenger model | User confirmed additional models working |
| D9 | 2026-06-06 | Challenger HW-engineer found MOSI direction bug (C-2) | main.cpp:133 sets MOSI to INPUT, breaks all post-init SPI writes |
| D10 | 2026-06-06 | Challenger HW-engineer confirmed access address byte order CORRECT | ADV_ACCESS_ADDR already in LSByte-first nRF24 SPI order |
| D11 | 2026-06-06 | Challenger wireless-expert APPROVED dewhiten (no critical defects) | Galois LFSR, seed, bit-swap pipeline all verified correct |
| D12 | 2026-06-06 | Structured diagnostics module COMPLETE — 7 HW verification criteria PASS | Phase C: HW APPROVED, Security CONDITIONAL (F-1 spi_xfer abort) |
| D13 | 2026-06-06 | F-1 (HIGH): `ESP_ERROR_CHECK` in `spi_xfer()` causes `abort()` on SPI failure | Defeats diagnostic retry architecture; needs `bool` return propagation |
| D14 | 2026-06-06 | F-4 (MEDIUM, pre-existing): `EspIdfHal::init()` has no RAII destructor | Cleanup on init failure not guaranteed |
| D15 | 2026-06-06 | F-2..F-6 (LOW): Silent truncation, uint8_t retry limit, retry w/o re-init (correct), no secrets | Non-blocking advisory flags — no code changes required |

---

## Pipeline Logs Index

| Log # | Agent | Date | File |
|-------|-------|------|------|
| 005 | memory-safety | 2026-06-06 | `docs/pipeline/logs/005-memory-safety-review.md` |
| 006 | wireless-expert | 2026-06-06 | `docs/pipeline/logs/006-ubertooth-dewhiten-test-plan.md` |
| 007 | test-engineer | 2026-06-06 | `docs/pipeline/logs/007-dewhiten-unit-tests.md` |
| 008 | docs-writer | 2026-06-06 | `docs/pipeline/logs/008-ble-whitening-doc.md` |
| 014 | hardware-engineer | 2026-06-06 | Phase C: Structured diagnostics HW verification (APPROVED) |
| 015 | security-reviewer | 2026-06-06 | Phase C: Structured diagnostics security review (CONDITIONAL PASS) |