# nrf-adr-0014: EspIdfHal::init() has no RAII destructor

| Field        | Value                                            |
| ------------ | ------------------------------------------------ |
| Category     | `adr`                                            |
| Status       | `open`                                           |
| Assignee     | Memory Safety                                     |
| Priority     | `medium`                                         |
| Created      | 2026-06-06                                       |
| Completed    | —                                                 |
| Dependencies | —                                                 |

## Description

Finding F-4 (MEDIUM severity, pre-existing): `EspIdfHal::init()` uses `ESP_ERROR_CHECK` for initialization (appropriate for setup) but has no RAII destructor for cleanup. If initialization succeeds partially (e.g., SPI device added but bus not fully configured), cleanup on destruction is not guaranteed.

## Context

The memory-safety review found that `EspIdfHal` lacks a destructor that calls `spi_bus_remove_device()` and `spi_bus_free()`. While `ESP_ERROR_CHECK` is appropriate for init (fail-fast on setup), the absence of RAII cleanup means resources could leak if the HAL object is destroyed unexpectedly.

## Decision

Add RAII destructor to `EspIdfHal` that removes the SPI device and frees the bus. (Now implemented.)

## Consequences

- `EspIdfHal` now has a destructor that properly cleans up SPI resources
- No resource leaks on destruction
- Consistent with RAII best practices for embedded systems