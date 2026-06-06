# Pipeline TODO

Task tracking for the ESP32 nRF24L01+ project.
Status: `[ ]` pending, `[~]` active, `[x]` done, `[!]` blocked

---

## Active

- [~] **Review Pipeline** — 12-session comprehensive review (Sessions 1-6 primary pass done; challenger pass pending)
- [~] **BLE Whitening Bug Fix** — CRITICAL: missing bit-swap before dewhiten() + LFSR form ambiguity (blocked on BLE spec Vol 6 Part B §3.2)

---

## Backlog

- [ ] Challenger pass reviews (all 6 specialists) — requires model diversity config
- [ ] Session 7: HAL adapter review (`components/nrf24_espidf/`)
- [ ] Session 8: Driver implementation review (missing SPI commands)
- [ ] Session 9: Application firmware review (`main/main.cpp`)
- [ ] Session 10: Build system review
- [ ] Session 11: Pipeline governance review
- [ ] Session 12: Adversarial challenge synthesis (Dual-Model Challenge)
- [ ] Configure additional models for model diversity (qwen3.5:397b-cloud, etc.)
- [ ] Add test coverage for dewhiten(), channel_to_rf_ch(), configure_rx(), Driver methods, RxConfig methods
- [ ] Wire test_registers.cpp into build system (currently orphaned)
- [ ] Add RfSetup::to_byte() constexpr
- [ ] Add HAL IRQ pin abstraction
- [ ] Add missing Driver SPI commands (W_TX_PAYLOAD, W_TX_PAYLOAD_NOACK, REUSE_TX_PL, R_RX_PL_WID, W_ACK_PAYLOAD, NOP)
- [ ] Create learning docs for: BLE whitening, channel mapping, HAL porting, non-contiguous encoding
- [ ] Fix uint8_t overflow in RxConfig::total_channels() when extra_channel_count >= 253

---

## Done

- [x] Memory-safety skill created (`.opencode/skills/memory-safety/SKILL.md`)
- [x] Memory-safety agent created (`.opencode/agents/memory-safety.md`)
- [x] Primary pass reviews completed (6/6 specialists: SW-engineer, HW-engineer, wireless-expert, security-reviewer, test-engineer, docs-writer)
- [x] BLE whitening XOR order analysis completed

---

## Decisions

| ID | Date | Decision | Context |
|----|------|----------|---------|
| D1 | 2026-06-06 | Agent model field removed from all 8 agents (inherit session default) | Model diversity via session config |
| D2 | 2026-06-06 | Memory-safety skill created from scratch (no existing skill found on skills.sh or GitHub) | User requested memory leak/C++ best practices coverage |
| D3 | 2026-06-06 | BLE whitening Bug #1 confirmed: missing bit-swap before dewhiten() | Wireless-expert analysis; nRF24 MSB-first vs BLE LSB-first |
| D4 | 2026-06-06 | BLE whitening Bug #2 (LFSR form): BLOCKED on BLE Core Spec Vol 6 Part B §3.2 | Galois vs Fibonacci produces different whitening sequences |