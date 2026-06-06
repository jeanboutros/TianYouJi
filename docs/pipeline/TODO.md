# Pipeline TODO

Task tracking for the ESP32 nRF24L01+ project.
Status: `[ ]` pending, `[~]` active, `[x]` done, `[!]` blocked

---

## Active

- [~] **Review Pipeline** — 12-session comprehensive review (Sessions 1-6 primary pass done + dewhiten fix; challenger pass pending)

---

## Backlog

- [ ] Challenger pass reviews (all 6 specialists) — nemotron-3-ultra:cloud configured
- [ ] Session 7: HAL adapter review (`components/nrf24_espidf/`)
- [ ] Session 8: Driver implementation review (missing SPI commands)
- [ ] Session 9: Application firmware review (`main/main.cpp`)
- [ ] Session 10: Build system review
- [ ] Session 11: Pipeline governance review
- [ ] Session 12: Adversarial challenge synthesis (Dual-Model Challenge)
- [ ] Wire test_registers.cpp into build system (currently orphaned)
- [ ] Add RfSetup::to_byte() constexpr
- [ ] Add HAL IRQ pin abstraction
- [ ] Add missing Driver SPI commands (W_TX_PAYLOAD, W_TX_PAYLOAD_NOACK, REUSE_TX_PL, R_RX_PL_WID, W_ACK_PAYLOAD, NOP)
- [ ] Create learning docs for: channel mapping, HAL porting, non-contiguous encoding
- [ ] Fix uint8_t overflow in RxConfig::total_channels() when extra_channel_count >= 253
- [ ] Address memory-safety findings F1 (EspIdfHal destructor), F3 (null guard channel_at), F2 (ASAN flags)

---

## Done

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
- [x] Pipeline logs directory created and populated (005-008)
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

---

## Pipeline Logs Index

| Log # | Agent | Date | File |
|-------|-------|------|------|
| 005 | memory-safety | 2026-06-06 | `docs/pipeline/logs/005-memory-safety-review.md` |
| 006 | wireless-expert | 2026-06-06 | `docs/pipeline/logs/006-ubertooth-dewhiten-test-plan.md` |
| 007 | test-engineer | 2026-06-06 | `docs/pipeline/logs/007-dewhiten-unit-tests.md` |
| 008 | docs-writer | 2026-06-06 | `docs/pipeline/logs/008-ble-whitening-doc.md` |