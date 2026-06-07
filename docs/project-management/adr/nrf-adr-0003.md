# nrf-adr-0003: BLE whitening Bug #1 — missing bit-swap before dewhiten()

| Field        | Value                                            |
| ------------ | ------------------------------------------------ |
| Category     | `adr`                                            |
| Status       | `closed`                                         |
| Assignee     | Wireless Expert                                   |
| Priority     | `critical`                                        |
| Created      | 2026-06-06                                       |
| Completed    | 2026-06-06                                       |
| Dependencies | —                                                 |

## Description

BLE whitening Bug #1 confirmed: the `dewhiten()` function was missing a bit-swap operation before dewhitening. The nRF24L01+ sends data MSB-first over SPI, but BLE uses LSB-first, so the bit order must be swapped before applying the whitening LFSR.

## Context

Wireless-expert analysis identified that the nRF24 MSB-first SPI byte order differs from BLE's LSB-first on-air byte order. Without the bit-swap, dewhitened data would be corrupted.

## Decision

Confirmed as a critical bug. The bit-swap must be applied before the dewhitening LFSR operation.

## Consequences

- Without fix: all BLE advertising payloads are decoded incorrectly
- With fix: bit-swap is now encapsulated inside `dewhiten()` to prevent callers from forgetting it