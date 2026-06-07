# [ID]: [Title]

| Field        | Value                                            |
| ------------ | ------------------------------------------------ |
| Category     | `ticket` / `epic` / `clarification` / `adr` / `advisory` / `design` / `chore` |
| Status       | `open` / `backlog` / `in-progress` / `review` / `closed` |
| Assignee     | [Agent role or person]                           |
| Priority     | `critical` / `high` / `medium` / `low`           |
| Created      | YYYY-MM-DD                                       |
| Completed    | —                                                |
| Dependencies | [IDs of dependent tickets, or —]                 |

## Description

[What needs to be done, from the user's perspective. No assumptions.]

## No-Assumption Check

Before starting work on this ticket:
- [ ] All hardware details verified against datasheet
- [ ] All protocol details verified against specification
- [ ] All design decisions confirmed (no BLOCKED questions)
- [ ] Scope boundaries clearly defined

If any check fails, STATUS: BLOCKED and raise a clarification.

## Acceptance Criteria

- [ ] [Binary pass/fail criteria]
- [ ] [Each criterion must be independently verifiable]
- [ ] [Include compliance gate requirements if applicable]
- [ ] [Include verification command if applicable]

## Expected Output

[Description of what this ticket produces: code files, documentation, test results, etc.]

## Compliance Requirements

[Which compliance skills must be loaded for this ticket? Check all that apply]
- [ ] compliance-gate (always required for implementation)
- [ ] type-design-review (if adding/modifying types or APIs)
- [ ] silent-failure (if adding error handling or error paths)
- [ ] review-confidence (for all review phases)
- [ ] [Domain-specific skills from tech stack]

## Files Affected

- [List of files to create or modify]

## Comments

[Additional context, links to external references, etc.]