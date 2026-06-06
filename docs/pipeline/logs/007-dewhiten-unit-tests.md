# 007 — Dewhiten Unit Tests — Host-Side GoogleTest Results

**Date:** 2026-06-06  
**Author:** Test Engineer  
**Status:** PASS  
**Test binary:** `tests/build/test_ble_dewhiten`  
**Framework:** GoogleTest v1.14.0  
**Compiler:** GCC 14.2.0  
**ASAN:** Enabled (`-fsanitize=address -fno-omit-frame-pointer -g`)

---

## 1. Summary

| Metric | Value |
|--------|-------|
| Total tests | 23 |
| Passed | 23 |
| Failed | 0 |
| Test suites | 2 (Swapbits, Dewhiten) |
| Build | Clean (0 warnings) |
| ASAN violations | 0 |
| ESP-IDF build | Unaffected (passes) |

---

## 2. Test Coverage by Acceptance Criterion

| AC | Criterion | Tests | Status |
|----|-----------|-------|--------|
| AC-1 | swapbits() correctly reverses bit order | `Swapbits.IdentityValues`, `Swapbits.PalindromicBytes`, `Swapbits.KnownReversals` | ✅ PASS |
| AC-2 | swapbits() is an involution | `Swapbits.Involution_EdgeCases`, `Swapbits.Involution_All256` | ✅ PASS |
| AC-3 | Known whitened vectors match Grinberg reference | `Dewhiten.KnownVector_Channel37`, `Dewhiten.KnownVector_Channel38`, `Dewhiten.KnownVector_Channel39_AllZero`, `Dewhiten.CrossVerification_Grinberg` | ✅ PASS |
| AC-4 | Round-trip: whiten→swapbits→dewhiten = original | `Dewhiten.RoundTrip_AdvChannels`, `Dewhiten.RoundTrip_DataChannels`, `Dewhiten.SingleByteRoundTrip`, `Dewhiten.MaxLength32Bytes`, `Dewhiten.RoundTrip_AllPduTypes`, `Dewhiten.RoundTrip_VariousLengths` | ✅ PASS |
| AC-5 | Channel-dependent whitening | `Dewhiten.CrossChannelIndependence` | ✅ PASS |
| AC-6 | Edge cases: zero-len, single, all-zero, all-0xFF, max 32 | `Dewhiten.ZeroLength`, `Dewhiten.SingleByteRoundTrip`, `Dewhiten.AllZeroInput_YieldsWhiteningMasks`, `Dewhiten.AllFFInput`, `Dewhiten.MaxLength32Bytes` | ✅ PASS |
| AC-7 | All-zero input produces raw whitening masks | `Dewhiten.AllZeroInput_YieldsWhiteningMasks` | ✅ PASS |
| AC-8 | Full RX pipeline (swap + dewhiten) | `Dewhiten.FullRxPipelineSimulation` | ✅ PASS |
| AC-9 | PDU header validity after dewhiten | `Dewhiten.PduHeaderParsingAfterDewhiten`, `Dewhiten.PduHeader_AdvScanInd` | ✅ PASS |

---

## 3. Detailed Test Results

```
[==========] Running 23 tests from 2 test suites.
[----------] 5 tests from Swapbits
[ RUN      ] Swapbits.IdentityValues              [  OK  ]
[ RUN      ] Swapbits.PalindromicBytes             [  OK  ]
[ RUN      ] Swapbits.KnownReversals              [  OK  ]
[ RUN      ] Swapbits.Involution_EdgeCases        [  OK  ]
[ RUN      ] Swapbits.Involution_All256           [  OK  ]
[----------] 18 tests from Dewhiten
[ RUN      ] Dewhiten.RoundTrip_AdvChannels       [  OK  ]
[ RUN      ] Dewhiten.RoundTrip_DataChannels      [  OK  ]
[ RUN      ] Dewhiten.KnownVector_Channel37       [  OK  ]
[ RUN      ] Dewhiten.KnownVector_Channel38       [  OK  ]
[ RUN      ] Dewhiten.KnownVector_Channel39_AllZero [  OK  ]
[ RUN      ] Dewhiten.ZeroLength                  [  OK  ]
[ RUN      ] Dewhiten.SingleByteRoundTrip         [  OK  ]
[ RUN      ] Dewhiten.AllZeroInput_YieldsWhiteningMasks [  OK  ]
[ RUN      ] Dewhiten.AllFFInput                  [  OK  ]
[ RUN      ] Dewhiten.MaxLength32Bytes            [  OK  ]
[ RUN      ] Dewhiten.CrossChannelIndependence    [  OK  ]
[ RUN      ] Dewhiten.CrossVerification_Grinberg  [  OK  ]
[ RUN      ] Dewhiten.FullRxPipelineSimulation    [  OK  ]
[ RUN      ] Dewhiten.PduHeaderParsingAfterDewhiten [  OK  ]
[ RUN      ] Dewhiten.PduHeader_AdvScanInd        [  OK  ]
[ RUN      ] Dewhiten.RoundTrip_AllPduTypes       [  OK  ]
[ RUN      ] Dewhiten.RoundTrip_VariousLengths    [  OK  ]
[ RUN      ] Dewhiten.SwapbitsAndWhiteningAreOrthogonal [  OK  ]
[==========] 23 tests ran.
[  PASSED  ] 23 tests.
```

---

## 4. Test Design Notes

### 4.1 Round-trip Strategy

The `dewhiten()` function combines two steps internally:
1. **Bit-swap** each byte (nRF24 MSbit-first → BLE LSbit-first)
2. **Galois LFSR XOR** whitening with polynomial x^7+x^4+1

Because XOR whitening is symmetric and swapbits is an involution, the
correct round-trip test simulates the full TX→RX pipeline:

```
BLE plaintext → LFSR whiten → swapbits (TX path, nRF24 format)
                ↓ dewhiten() called on nRF24-format bytes ↓
              swapbits (step 1) → LFSR XOR (step 2) → original BLE plaintext
```

**Note:** Calling `dewhiten()` twice on the same buffer does NOT produce
the original because swapbits(A ^ B) ≠ swapbits(A) ^ swapbits(B) in
general — the bit-swap step is NOT in the XOR sub-space. The round-trip
must go through the TX pipeline (whiten then swap) before calling
dewhiten.

### 4.2 Known Vectors

Test vectors were computed using `docs/pipeline/scripts/ble_whiten_reference.py`
which implements Dmitry Grinberg's `btLeWhiten` algorithm exactly. The
vectors are fully independent of our `simulate_nrf24_tx` helper, providing
true cross-verification.

### 4.3 All-Zero Input Test

When `dewhiten` receives all-zero bytes:
- Step 1: swapbits(0x00) = 0x00
- Step 2: 0x00 ^ mask = mask

This directly exposes the raw Galois LFSR whitening sequence, which we
verify against the masks computed by the Python reference.

---

## 5. Files Modified

| File | Change |
|------|--------|
| `tests/test_ble_dewhiten.cpp` | **Created** — 23 test cases for swapbits() and dewhiten() |
| `tests/CMakeLists.txt` | Added `test_ble_dewhiten` target with ASAN flags and ble.cpp linkage |

---

## 6. Build Commands

```bash
# Configure and build (host-side)
cd tests
cmake -B build -S .
cmake --build build --target test_ble_dewhiten

# Run tests
./build/test_ble_dewhiten

# Verify ESP-IDF build not broken
source ~/.espressif/tools/activate_idf_v6.0.1.sh
idf.py build
```

---

## 7. Self-Audit Checklist

| # | Check | Criterion | Status |
|---|-------|-----------|--------|
| 1 | Round-trip coverage | Every channel (37/38/39 + data) has round-trip test | ✅ 6 round-trip tests |
| 2 | Edge cases | 0x00, 0xFF, zero-length, single-byte, 32-byte max tested | ✅ |
| 3 | Boundary values | Max valid payload (32), min (1), ch0, ch39 tested | ✅ |
| 4 | Protocol logic | Whitening masks verified against Python reference, cross-channel independence tested | ✅ |
| 5 | AC coverage | All 9 acceptance criteria have test evidence | ✅ 9/9 |
| 6 | Build passes | cmake build exits 0, ESP-IDF build exits 0 | ✅ |

---

## References

- Dmitry Grinberg, "Bit-Banging Bluetooth Low Energy": https://dmitry.gr/?r=05.Projects&proj=11.%20Bluetooth%20LE%20fakery
- Bluetooth Core Spec Vol 6 Part B §3.2 (data whitening)
- `docs/pipeline/scripts/ble_whiten_reference.py` — Python reference implementation
- `docs/pipeline/logs/006-ubertooth-dewhiten-test-plan.md` — hardware test plan