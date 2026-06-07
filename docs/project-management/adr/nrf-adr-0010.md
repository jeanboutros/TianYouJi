# nrf-adr-0010: Access address byte order confirmed CORRECT

| Field        | Value                                            |
| ------------ | ------------------------------------------------ |
| Category     | `adr`                                            |
| Status       | `closed`                                         |
| Assignee     | Hardware Engineer                                 |
| Priority     | `high`                                            |
| Created      | 2026-06-06                                       |
| Completed    | 2026-06-06                                       |
| Dependencies | —                                                 |

## Description

Challenger HW-engineer confirmed that the access address byte order in the code is correct. `ADV_ACCESS_ADDR` is already in LSByte-first nRF24 SPI order, which matches the BLE on-air order.

## Context

During review of BLE packet construction, a question was raised about whether `ADV_ACCESS_ADDR` needed byte-order reversal. The HW-engineer verified that the access address 0x8E89BED6 is already stored in the correct LSByte-first order for the nRF24 SPI interface, matching the BLE specification's on-air byte order.

## Decision

No change needed. The access address byte order is correct as-is.

## Consequences

- No code change required for access address handling
- Confirms that the nRF24 SPI byte order (LSByte-first for multi-byte registers) is being used correctly