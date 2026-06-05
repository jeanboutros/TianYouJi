---
name: self-audit-checklist
description: "Mandatory self-audit protocol for all reviewing agents. Before issuing any verdict in Phase C, agents MUST complete this checklist explicitly. If any row is missing, the review is INVALID."
---

# Self-Audit Checklist — ESP32 nRF24L01+ Project

## Purpose

Prevent reviewers from focusing on one dimension and missing others. Every Phase C review MUST cover ALL dimensions explicitly.

## When to Use

- **All specialist agents** — Before issuing APPROVED/REJECTED in Phase C
- **Code Architect** — Before declaring a task complete

## Mandatory Checklist

Before writing your verdict, complete this checklist **explicitly in your output**. Every row must show a concrete finding or "PASS — checked [evidence]".

```markdown
### Self-Audit Checklist

| Category | Checked? | Finding or PASS |
|----------|----------|-----------------|
| Build passes (`idf.py build` exit 0) | yes/no | [output evidence] |
| Typed enums (no raw integers in API) | yes/no | [files checked] |
| Doxygen on new public symbols | yes/no | [symbols checked] |
| Datasheet fidelity (fields match) | yes/no | [register + page ref] |
| HAL decoupling (no platform headers in library) | yes/no | [includes checked] |
| Reserved bits handled | yes/no | [to_byte/from_byte checked] |
| No magic numbers in @code examples | yes/no | [examples checked] |
| Buffer safety (bounded copies) | yes/no | [buffers checked] |
| AGENTS.md compliance | yes/no | [rules verified] |
| Conventional commit ready | yes/no | [message format] |
```

## How to Complete Each Row

### Build passes
- Evidence: actual `idf.py build` output showing "Project build complete"
- If not run in this session, state "NOT VERIFIED — requires build"

### Typed enums
- Grep for `uint8_t` parameters that should be enum class
- Check that no public function accepts raw integers for typed fields

### Doxygen
- List every new public symbol (function, struct, enum, macro)
- Verify each has `/** @brief ... */` with @param and @return

### Datasheet fidelity
- For each register struct: cite the datasheet page where the bit layout is defined
- For protocol values: cite the Bluetooth Core Spec section

### HAL decoupling
- Check `#include` lines in all library public headers
- Only allowed: `<cstdint>`, `<cstring>`, `<cstdio>`, own library headers
- Forbidden: `driver/spi_master.h`, `driver/gpio.h`, any ESP-IDF header

### Reserved bits
- Check `to_byte()`: does it preserve/mask reserved bits?
- Check `from_byte()`: does it ignore reserved bits?

### No magic numbers
- Scan `@code` blocks for hex literals like `0x03`, `0x26`
- All values should use library vocabulary: `nrf24::DataRate::Mbps1`

### Buffer safety
- All `memcpy`, array access, SPI transfers have bounded size
- No unbounded reads from external data

### AGENTS.md compliance
- Cross-check against the rules in AGENTS.md
- Verify commit message format, learning docs policy, etc.
