# nrf-adr-0002: Memory-safety skill created from scratch

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

A new `memory-safety` skill was created from scratch to cover C++ memory leak detection, RAII best practices, and heap/stack safety for embedded systems. No existing skill was found on skills.sh or GitHub that matched the project's needs.

## Context

The user requested memory leak and C++ best practices coverage for the embedded ESP32 project. After searching skills.sh and GitHub, no suitable existing skill was found. The skill was written from scratch covering ASAN/Valgrind patterns, RAII, FreeRTOS heap analysis, and common embedded C++ memory safety issues.

## Decision

Create the memory-safety skill from scratch rather than adapting an existing one.

## Consequences

- Custom skill covering embedded-specific memory safety patterns
- Includes RAII, smart pointers, buffer overflow detection, FreeRTOS heap analysis