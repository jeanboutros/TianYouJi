# nrf-adr-0001: Agent model field removed from all 8 agents

| Field        | Value                                            |
| ------------ | ------------------------------------------------ |
| Category     | `adr`                                            |
| Status       | `closed`                                         |
| Assignee     | PM                                               |
| Priority     | `medium`                                         |
| Created      | 2026-06-06                                       |
| Completed    | 2026-06-06                                       |
| Dependencies | —                                                 |

## Description

The `model` field was removed from all 8 agent configuration files. Agents now inherit the session default model rather than specifying their own.

## Context

Model diversity is achieved through session-level configuration instead of per-agent specification. This simplifies agent configuration and makes it easier to switch models for experimentation (e.g., using a different model for challenger passes).

## Decision

Remove the `model` field from all agent files. Agents inherit the session default model.

## Consequences

- Simplified agent configuration
- Model selection centralized at session level
- Easier to run challenger passes with different models