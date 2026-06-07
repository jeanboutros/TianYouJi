# nrf-adr-0006: Seed formula: swapbits(channel) | 2

| Field        | Value                                            |
| ------------ | ------------------------------------------------ |
| Category     | `adr`                                            |
| Status       | `closed`                                         |
| Assignee     | Wireless Expert                                   |
| Priority     | `critical`                                        |
| Created      | 2026-06-06                                       |
| Completed    | 2026-06-06                                       |
| Dependencies | nrf-adr-0004                                      |

## Description

The BLE data whitening LFSR seed formula is `swapbits(channel) | 2`. This originates from Dmitry Grinberg's `btLeWhitenStart` function and is confirmed by all 6 reference implementations.

## Context

The BLE Core Spec describes the whitening LFSR initial value as the channel index with 2 bits set. The exact formula — bit-swap the channel number then OR with 2 (setting bit 1) — comes from btLeWhiten and is universally used in BLE implementations.

## Decision

Use `swapbits(channel) | 2` as the LFSR seed for whitening/dewhitening.

## Consequences

- Correct seed for all BLE advertising channels (37, 38, 39) and data channels (0-36)
- Consistent with all known reference implementations
- Verified by 23 unit tests and 35 Python round-trip tests