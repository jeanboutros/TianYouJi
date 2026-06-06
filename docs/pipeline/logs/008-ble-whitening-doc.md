# 008 — BLE Data Whitening Learning Document

**Date:** 2026-06-06  
**Author:** Docs Writer  
**Status:** COMPLETE  
**Task:** Write comprehensive learning document about BLE data whitening, bugs, and corrections

---

## What was done

1. **Created** `docs/learning/ble-data-whitening-nrf24.md` — comprehensive self-contained
   learning document covering:
   - BLE whitening overview (purpose, polynomial, 7-bit LFSR mechanism)
   - Correct Galois LFSR implementation with state diagram
   - Seed derivation walkthrough (`swapbits(channel) | 2` formula)
   - Complete seed table for all 40 BLE channels
   - Full TX encoding and RX decoding pipelines
   - Line-by-line code walkthrough with annotations
   - Bug #1 analysis (missing bit-swap) with nRF24L01+ datasheet evidence
   - Bug #2 analysis (Fibonacci vs Galois LFSR + wrong seed) with concrete examples
   - Step-by-step LFSR state trace for first 8 bits of ch37
   - Cross-reference against 5 known-good nRF24 BLE implementations
   - Ubertooth hardware validation plan reference
   - 5 pitfalls and lessons learned
   - 6 verified external references

2. **Updated** `docs/learning/INDEX.md` — added "BLE Protocol" section with
   new entry for the whitening document

## Sources consulted

- `components/nrf24l01plus/include/nrf24l01plus/ble.h` — current (corrected) implementation
- `components/nrf24l01plus/src/ble.cpp` — current (corrected) implementation
- `docs/pipeline/logs/006-ubertooth-dewhiten-test-plan.md` — test plan with bug analysis
- `docs/pipeline/scripts/ble_whiten_reference.py` — Python reference implementation
- `docs/learning/nrf24-ble-packet-crafting.md` — existing BLE packet doc
- Git commit `82bbc45` diff — the actual fix that was applied

## Verification

- All external URLs verified with `fetch_webpage`:
  - Dmitry Grinberg page: ✅ loads, full source code present
  - omriiluz/NRF24-BTLE-Decoder: ✅ 332 stars confirmed
  - sandeepmistry/arduino-nRF24L01-BLE: ✅ 71 stars confirmed
  - MarcelMG/STM8_nRF24L01_BLE_Beacon: ✅ exists
  - Bluetooth Core Spec page: ✅ loads
  - Nordic nRF24 datasheet: ❌ 403 (using local copy reference)
- Python reference script run successfully (all tests pass)
- LFSR state walkthrough manually computed and cross-checked
- Whitening vectors for ch37/38/39 verified against script output

## Self-audit checklist (Phase C)

| Category | Checked? | Finding |
|----------|----------|---------|
| Doxygen on new public symbols | yes | No new code symbols — documentation only |
| @code examples use library vocabulary | yes | All examples use `nrf24::ble::dewhiten`, `nrf24::ble::swapbits` |
| Learning docs updated | yes | New doc created, INDEX.md updated |
| References verified | yes | 6 URLs checked, 5 confirmed live, 1 using local copy |
| No magic numbers in examples | yes | No raw hex register writes in @code blocks |
| Self-contained document | yes | Reader can understand full algorithm from this doc alone |
| No TODO/design rationale in doc | yes | Reference documentation only |
| AGENTS.md compliance | yes | Reference policy followed, knowledge management rules met |