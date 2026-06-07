# nrf-epic-003: Structured Diagnostics Module

| Field        | Value                                            |
| ------------ | ------------------------------------------------ |
| Category     | `epic`                                            |
| Status       | `closed`                                          |
| Assignee     | Code Architect                                    |
| Priority     | `high`                                            |
| Created      | 2026-06-06                                        |
| Completed    | 2026-06-06                                        |
| Dependencies | —                                                  |

## Description

Implement a structured diagnostics module for the nRF24L01+ driver that replaces the old boolean `spi_comm_test()` + `verify_ble_rx()` functions with phased, typed diagnostic results.

## Background

The original diagnostic verification used simple boolean pass/fail functions that gave no insight into which phase failed or why. This made debugging hardware issues extremely difficult, as the only signal was "SPI test failed" with no way to know if it was a wiring issue, a power problem, a register write order bug, or a clone chip.

## Scope

- Design phased diagnostic architecture (6 phases)
- Implement typed result structures (`DiagResult`, `DiagFullResult`)
- Implement `DiagOpts` for configurable retries and verbose output
- Implement `full_boot_diagnostic()` main entry point
- Maintain backward compatibility via deprecated wrappers
- All 7 hardware verification criteria must PASS

## Hardware Verification Criteria

1. **Power-on delay**: Sufficient time after power-on before SPI is valid
2. **Independent address verification**: Read-back of written addresses matches expected values
3. **EN_AA/EN_CRC write order**: EN_AA must be written before CONFIG to avoid silent CRC forcing
4. **CE GPIO readback**: CE pin state can be verified by reading it back
5. **Warm boot handling**: Diagnostics work correctly on warm reset without cold boot
6. **Clone detection**: Si24R1/BK2425 clone chips are detected
7. **Self-consistent error detection**: Diagnostic module detects its own errors

## Related Decisions

- nrf-adr-0012: Structured diagnostics module COMPLETE
- nrf-adr-0013: ESP_ERROR_CHECK in spi_xfer() — motivated the bool return type

## Related Tickets

- nrf-adr-0009: MOSI direction bug (found during diagnostics development)
- nrf-adr-0010: Access address byte order confirmed correct
- nrf-adr-0011: Dewhiten implementation approved

## Milestones

1. ~~Phase A: Design diagnostic phases and result types~~ DONE
2. ~~Phase B: Implement full_boot_diagnostic() and all 6 phase checks~~ DONE
3. ~~Phase C: HW-engineer APPROVED, Security-reviewer CONDITIONAL (F-1 tracked separately)~~ DONE

## Acceptance Criteria

- [x] Build passes with zero warnings (`-Werror` active)
- [x] All 7 hardware verification criteria PASS
- [x] Backward-compatible deprecated wrappers for old API
- [x] Configurable retry count and verbose output
- [x] Phase C HW-engineer review APPROVED

## Lessons Learned

1. **EN_AA/EN_CRC write order** is a critical trap — writing CONFIG before EN_AA forces CRC on silently
2. **CE GPIO mode** must be `GPIO_MODE_INPUT_OUTPUT` to allow readback
3. **Self-consistent error detection** is essential — diagnostic code can have bugs too
4. **Boolean diagnostics are insufficient** for embedded hardware debugging