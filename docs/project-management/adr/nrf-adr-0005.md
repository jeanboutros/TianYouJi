# nrf-adr-0005: dewhiten() encapsulates bit-swap internally

| Field        | Value                                            |
| ------------ | ------------------------------------------------ |
| Category     | `adr`                                            |
| Status       | `closed`                                         |
| Assignee     | Code Architect                                    |
| Priority     | `high`                                            |
| Created      | 2026-06-06                                       |
| Completed    | 2026-06-06                                       |
| Dependencies | nrf-adr-0003, nrf-adr-0004                        |

## Description

The `dewhiten()` function encapsulates the bit-swap operation internally, so callers do not need to remember to call `swapbits()` before dewhitening.

## Context

After Bug #1 (missing bit-swap), the design choice was to embed the bit-swap inside `dewhiten()` itself. This follows the principle of making correct usage easy and incorrect usage difficult — callers cannot forget the swap if it's built into the function.

## Decision

`dewhiten()` performs: bit-swap → LFSR dewhitening → bit-swap. Callers pass raw nRF24 SPI bytes and receive dewhitened BLE bytes.

## Consequences

- Eliminates the class of bugs where callers forget bit-swap
- Function signature unchanged — drop-in fix
- Callers must be aware that input is expected in nRF24 SPI byte order (MSB-first)