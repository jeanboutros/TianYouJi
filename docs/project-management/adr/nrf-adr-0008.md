# nrf-adr-0008: nemotron-3-ultra:cloud configured as challenger model

| Field        | Value                                            |
| ------------ | ------------------------------------------------ |
| Category     | `adr`                                            |
| Status       | `closed`                                         |
| Assignee     | PM                                               |
| Priority     | `low`                                             |
| Created      | 2026-06-06                                       |
| Completed    | 2026-06-06                                       |
| Dependencies | —                                                 |

## Description

The `nemotron-3-ultra:cloud` model was configured in OpenCode as the challenger model for Dual-Model Challenge pipeline phases.

## Context

The user confirmed that additional LLM models are working in their OpenCode configuration. Nemotron-3-ultra was selected as a challenger model for its different reasoning patterns compared to the primary model, providing adversarial review diversity.

## Decision

Use `nemotron-3-ultra:cloud` as the challenger model in Dual-Model Challenge phases.

## Consequences

- Challenger passes use a different model than primary passes
- Increases likelihood of catching errors that the primary model might miss