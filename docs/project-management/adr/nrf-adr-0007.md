# nrf-adr-0007: Agent outputs stored in docs/pipeline/logs/ sequentially

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

Agent output logs are stored in `docs/pipeline/logs/` with sequential numeric prefixes (e.g., `005-memory-safety-review.md`, `006-ubertooth-dewhiten-test-plan.md`).

## Context

User requirement #5 specified that agent outputs should be stored sequentially. Sequential numbering makes it easy to see the progression of pipeline activity and locate specific log entries.

## Decision

Store logs as `NNN-agent-description.md` in `docs/pipeline/logs/`.

## Consequences

- Clear chronological ordering of pipeline activity
- Easy to find specific review logs
- Sequential numbering requires coordination to avoid conflicts