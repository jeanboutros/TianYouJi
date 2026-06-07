# nrf-adr-0013: ESP_ERROR_CHECK in spi_xfer() causes abort() on SPI failure

| Field        | Value                                            |
| ------------ | ------------------------------------------------ |
| Category     | `adr`                                            |
| Status       | `open`                                           |
| Assignee     | Code Architect                                    |
| Priority     | `high`                                            |
| Created      | 2026-06-06                                       |
| Completed    | —                                                 |
| Dependencies | —                                                 |

## Description

Finding F-1 (HIGH severity): `ESP_ERROR_CHECK` in `Hal::spi_xfer()` calls `abort()` on any SPI transfer failure. This defeats the diagnostic retry architecture, which expects to detect and recover from transient SPI errors.

## Context

During Phase C security review, the reviewer identified that `ESP_ERROR_CHECK` is inappropriate for `spi_xfer()` because:
1. The diagnostic module has a retry mechanism that cannot function if `abort()` is called
2. SPI failures can be transient (loose wire, EMI) and should be recoverable
3. The `Driver` methods need to propagate errors, not crash

## Decision

Change `spi_xfer()` from `void` to return `bool`. Propagate through all `Driver` methods. All callers updated. (Now resolved — see nrf-adr-0012 for context on the diagnostics module that motivated this change.)

## Consequences

- `EspIdfHal::spi_xfer()` returns `false` on SPI errors instead of calling `abort()`
- All `Driver` methods propagate the error
- `read_reg(uint8_t)` returns `0xFF` on failure
- `write_reg()`, `write_reg_multi()`, `read_reg_multi()`, `read_payload()`, `flush_rx()`, `flush_tx()` return `bool`
- `EspIdfHal` has RAII destructor that removes SPI device and frees bus