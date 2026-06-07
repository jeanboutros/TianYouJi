# nrf-adr-0004: Galois LFSR is correct form for BLE dewhitening

| Field        | Value                                            |
| ------------ | ------------------------------------------------ |
| Category     | `adr`                                            |
| Status       | `closed`                                         |
| Assignee     | Wireless Expert                                   |
| Priority     | `critical`                                        |
| Created      | 2026-06-06                                       |
| Completed    | 2026-06-06                                       |
| Dependencies | nrf-adr-0003                                      |

## Description

BLE whitening Bug #2 resolved: the Galois LFSR is the correct form for BLE data dewhitening, not the Fibonacci LFSR. All 6 reference implementations use Galois form, originating from Dmitry Grinberg's implementation.

## Context

The BLE specification describes the whitening LFSR but does not explicitly name the form. Analysis of 6 reference projects (including Dmitry Grinberg's btLeWhiten) confirmed that all use Galois form with XOR feedback.

## Decision

Use Galois LFSR for dewhitening. The Fibonacci form was an incorrect alternative that would produce wrong results.

## Consequences

- Correct dewhitening of BLE payloads
- Consistency with all known reference implementations