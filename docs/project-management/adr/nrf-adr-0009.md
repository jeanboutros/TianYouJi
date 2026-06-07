# nrf-adr-0009: MOSI direction bug found by challenger HW-engineer

| Field        | Value                                            |
| ------------ | ------------------------------------------------ |
| Category     | `adr`                                            |
| Status       | `closed`                                         |
| Assignee     | Hardware Engineer                                 |
| Priority     | `critical`                                        |
| Created      | 2026-06-06                                       |
| Completed    | 2026-06-06                                       |
| Dependencies | —                                                 |

## Description

Challenger HW-engineer found a MOSI direction bug (finding C-2): `main.cpp:133` sets the MOSI GPIO to `GPIO_MODE_INPUT` after `spi_bus_initialize()`, which silently breaks all post-init SPI writes while reads appear to work.

## Context

During the challenger pass review of the hardware configuration, the HW-engineer identified that after SPI bus initialization, the MOSI pin was being set to `GPIO_MODE_INPUT`. This breaks SPI write operations (register writes, channel switching, IRQ clearing) while SPI read operations continue to work — making the bug invisible to read-based diagnostics.

## Decision

Confirmed as a critical bug. Lines 132-134 in `main.cpp` that set MOSI to `GPIO_MODE_INPUT` must be removed.

## Consequences

- Without fix: all SPI write operations silently fail after initialization
- See nRF24L01+ skill §1.4 for the full diagnostic and fix