# ESP32 Development Agent Notes

> **Important:** After managing a new challenge in a conversation, update this AGENTS.md file with the solution.

## Multi-Agent Validation Pipeline (MANDATORY)

This project uses a 3-phase validation pipeline for all non-trivial changes.

### Documentation & Configuration

| Resource | Location |
|----------|----------|
| Pipeline spec | `docs/pipeline/pipeline.md` |
| Agent roles (detailed) | `docs/pipeline/agents.md` |
| OpenCode agents | `.opencode/agents/` |
| OpenCode skills | `.opencode/skills/` |
| Task tracker | `docs/pipeline/TODO.md` |

### OpenCode Agents

| Agent | Role | Phases |
|-------|------|--------|
| `agency-director` | Orchestrator — dispatch only, never executes | All |
| `software-engineer` | Architecture, API, component boundaries | A, C |
| `hardware-engineer` | Register models, datasheet fidelity, timing | A, C |
| `wireless-expert` | RF protocol, BLE spec, channel/frequency mapping | A, C |
| `security-reviewer` | Buffer safety, stack depth, secrets, DMA bounds | A, C |
| `test-engineer` | Test strategy, static_assert, host-side unit tests | A, B, C |
| `docs-writer` | Doxygen, learning docs, reference verification | A, C |
| `code-architect` | Implementation via PAU loop | B |
| `memory-safety` | C++ memory leak detection, RAII, heap/stack safety | A, C |
| `pm` | Task creation, flag processing (sole authority) | All |

### OpenCode Skills

| Skill | Purpose | Used by |
|-------|---------|---------|
| `assumption-trap` | HALT on ambiguity — never guess | All agents |
| `pau-loop` | Plan-Apply-Validate cycle | Code Architect, Test Engineer |
| `incremental-execution` | Unit-by-unit with build validation | Code Architect, Test Engineer |
| `datasheet-verification` | Verify hardware against datasheets | HW Engineer, Wireless Expert, Code Architect |
| `self-audit-checklist` | 10-point quality checklist for reviews | All reviewing agents |
| `flag-protocol` | Raise issues for PM attention | All non-PM agents |
| `systematic-debugging` | Root-cause before fix — no random edits | Code Architect |
| `test-driven-development` | Red-green-refactor with static_assert | Test Engineer |
| `verification-before-completion` | No claims without fresh build evidence | All agents |
| `brainstorming` | Structured design before code — hard gate | All (Phase A) |
| `grill-me` | Adversarial review (Dual-Model Challenge) | Phase A & C |
| `nrf24l01plus` | nRF24L01+ chip-specific traps and diagnostics | All agents touching nRF24 code |
| `memory-safety` | C++ memory leak, RAII, ASAN, heap/stack safety | All agents |

### Pipeline Summary

```
Phase A: REQUIREMENTS & DESIGN  →  Phase B: BUILD (PAU Loop)  →  Phase C: MULTI-AGENT VERIFY
   (All specialists review)          (Code Architect)              (All specialists approve)
   (Dual-Model Challenge)                                          (Dual-Model Challenge)
```

### Key Rules

1. **No-Assumption Protocol** — When encountering ambiguity about hardware, protocol, or design, HALT with `STATUS: BLOCKED` and ask. Never guess register values or bit layouts.
2. **PAU Loop** — Plan the units → Apply one unit at a time → Validate (`idf.py build`) after each. Never implement everything at once.
3. **Datasheet is source of truth** — Verify all hardware details against `docs/datasheets/` before coding. If unclear, check the web. If still unclear, HALT.
4. **Quality Gate** — Before committing: build passes, Doxygen present, typed enums used, no magic numbers, AGENTS.md rules followed.
5. **Incremental execution** — One logical unit → build → pass → next unit. Never skip validation.
6. **Flag Protocol** — Non-blocking issues raised as structured flags for the PM to process.

### Validation Command

```bash
source ~/.espressif/tools/activate_idf_v6.0.1.sh && idf.py build
```

Must exit 0 with zero warnings (`-Werror` is active) before any commit.

## Code Documentation Rules (MANDATORY)

### Doxygen-style comments for all new symbols

Every new **function**, **struct**, **class**, **enum**, or **macro group** must be preceded by a Doxygen `/** ... */` block. Use the following structure:

```c
/**
 * @brief One-sentence summary of what this does.
 *
 * Longer explanation if needed — describe the protocol, bit layout,
 * algorithm, or non-obvious design decision here.
 *
 * Use @code / @endcode blocks for:
 *   - Bit/byte layout diagrams
 *   - Arithmetic walkthroughs
 *   - Usage examples
 *
 * @code
 *   // Example: show inputs, intermediate steps, and result
 *   foo(REG_STATUS);   // 0x07 & 0x1F = 0x07 → cmd = 0x07
 * @endcode
 *
 * @param name   Description (include units, valid range, or bit meaning).
 * @return       What is returned and under what conditions.
 */
```

### Rules

- **`@brief`** — always present; one sentence, no period at end.
- **`@code`** — use for any non-obvious bit manipulation, protocol encoding, or illustrative call site. Include an edge-case example (e.g. `0xFF` input) when the masking/clamping behaviour matters.
- **`@param`** — one line per parameter; note which bits are used if the full width is not consumed.
- **`@return`** — always present for non-void functions.
- Comments inside the function body remain as `/* short inline notes */` — do **not** duplicate the Doxygen block content there.
- This style applies to C and C++ equally.

## Hardware Register Library Design Principles (MANDATORY)

These rules apply whenever modelling hardware registers, peripherals, or protocol fields in C/C++ libraries.

### Datasheet is the source of truth — no assumptions, no invention

- **Read the datasheet first.** Every field name, bit position, encoding, and default value must come from the datasheet, not from intuition, training data, or prior experience with similar chips.
- **Never invent a value or field.** If a bit's meaning is not described in the datasheet, leave it unnamed (e.g. `reserved`) — do not guess.
- **If something is unclear in the datasheet, check the web** (application notes, errata, community reports) before writing any code. Use `fetch_webpage` on official sources.
- **If it is not in the datasheet and not on the web, do not make it up.** Stop, ask the user for clarification, or mark the field explicitly as `/* unknown — not documented */`.

### Typed enums for every field — no raw integers in the API

- Every register field that has a finite set of legal values **must be an `enum class`**, not an `int` or `uint8_t` literal.
- Every **method parameter** that accepts one of a finite set of values **must use a typed enum or a struct with `ADDRESS` constant**, not `uint8_t`. If a named-constant vocabulary exists for a parameter (e.g. `nrf24::reg::CONFIG` for register addresses), the parameter type must enforce it at compile time. Convenience `uint8_t` overloads **must be `private` or `protected`** — never `public`.
- Enum enumerator names must come **verbatim from the datasheet symbol names** wherever possible (e.g. `RF_DR_LOW`, `RF_PWR`). Use CamelCase only when the datasheet uses all-caps abbreviations that would be unreadable as-is.
- **No "catch-all" or "raw value" enumerators** (e.g. `Raw = 0xFF`). If the user needs a raw escape hatch, provide a separate `from_byte()` / `to_byte()` path on the struct, not inside the enum.
- **Naming conventions (`nrf24::reg::CONFIG`) are NOT sufficient where type enforcement is possible.** A `uint8_t` parameter that accepts `nrf24::reg::CONFIG` also accepts `0xFF` (an invalid register address). The type system must reject invalid values at compile time.

### Library API design — give users a vocabulary, not a raw byte

The goal is that a user calling the library can **read and understand the intent** of their code without looking at the datasheet:

```cpp
// BAD — opaque, error-prone, requires the user to memorise bit positions
nrf24_write_reg(REG_RF_SETUP, 0x26);

// GOOD — self-documenting, impossible to put a field in the wrong position
nrf24::RfSetup rf;
rf.data_rate = nrf24::DataRate::Mbps1;
rf.tx_power  = nrf24::TxPower::dBm0;
nrf24_write_reg(REG_RF_SETUP, rf.to_byte());
```

Rules:
- Expose **one `enum class` per field**, with an enumerator for every value the datasheet defines.
- Expose **one aggregate struct per register** with typed fields, `to_byte()`, `from_byte()`, and `format()`.
- Expose **typed overloads on the driver class** that deduce the register address from the struct type (e.g. `radio.read_reg(Config{})` returns `Config`, `radio.write_reg(cfg)` deduces `Config::ADDRESS`). Raw `uint8_t` overloads must be `private` — accessible only via the typed wrapper or internally.
- Expose **`to_reg(FieldType)` free functions** (overloaded per field type) so users can compose individual fields without constructing a full struct.
- Keep internal helpers (bit-splitting, encoding tables) in a `detail` sub-namespace so they do not pollute the public API.
- Headers are a library contract — **no learning notes, no TODO comments, no design rationale** in headers. That material belongs in `docs/learning/`.
- **The typed API must be the ONLY public API** — if both raw and typed overloads exist, the raw overloads must be private or protected. The presence of typed overloads does not excuse making raw overloads public.

### Completeness — model the full register, nothing less

- Every bit in a register must be accounted for, even reserved bits.
- Reserved bits are represented as unnamed padding or a note in the Doxygen block — **never silently ignored** in `to_byte()` or `from_byte()`.
- If a multi-bit field has a non-contiguous encoding (e.g. nRF24 `DataRate` spans bits 5 and 3), the encoding logic **must follow the datasheet's table exactly** — use a helper in `detail::` rather than inventing a compact representation.

### Platform independence — libraries must not couple to a specific SDK

- **No platform headers in library public API.** A reusable library (e.g. a peripheral driver) must not `#include` platform-specific headers (ESP-IDF, STM32 HAL, Arduino, etc.) in any public header under `include/`.
- **Abstract hardware access through a HAL interface.** Define a pure-virtual `Hal` class in the library. Platform-specific implementations live **outside** the library (e.g. in `main/` or a platform adapter component).
- **Library source files must only include library headers and standard C/C++ headers.**

### Dogfood your own vocabulary — no raw integers in examples or docs

When the library defines named constants or typed enums, **all documentation, `@code` examples, and internal usage must use them**:

```cpp
// BAD — raw hex in an @code block, even though nrf24::reg::CONFIG exists
radio.write_reg(0x00, 0x03);

// GOOD — uses the library's own vocabulary
radio.write_reg(nrf24::reg::CONFIG, nrf24::Config().to_byte());
```

Rules:
- **`@param` docs** must reference the header that defines the legal values (e.g. "Use constants from `nrf24l01plus/registers/addresses.h`").
- **`@code` examples** must use the library's typed constants and struct forms — never magic numbers that the user would need to look up. Examples must show the typed overload first (`radio.write_reg(cfg)`) and only show the raw overload in a comment marked `// internal use`.
- **If a function accepts `uint8_t` but a named-constant vocabulary exists**, this must be treated as a design deficiency, not merely documented. Either: (a) add a typed overload that deduces the address from a struct type, or (b) make the raw overload `private`. Documentation alone is not sufficient.
- **Learning docs and code examples** in `docs/learning/` must use the library's typed vocabulary in all `@code` blocks and inline code. If a doc needs to show the low-level raw API (for educational purposes), it must prominently note: *"This shows the low-level SPI API for educational purposes. Production code should use the typed struct API — see [cpp-enum-class-and-struct.md]."*

## Knowledge Management Rules

### Learning Summaries

After every conversation where something non-trivial is learned or solved:

1. **Create or update a learning document** in `docs/learning/` as a Markdown file.
   - File name: kebab-case reflecting the topic, e.g. `freertos-task-pinning.md`
   - Sections: **What was learned**, **Code examples**, **Pitfalls**, **References**
2. **Update `docs/learning/INDEX.md`** — add or update the entry for the relevant theme. Group entries by theme (e.g. *FreeRTOS*, *ESP-IDF Setup*, *C/C++ Basics*).
3. All learning files must be **Markdown only**.

### Follow-up Questions and Clarifications (MANDATORY)

Whenever a follow-up question or clarification is asked about a topic that already has a learning document:

1. **Add the answer directly into the relevant existing doc** — do not just reply in chat and move on.
2. Place the new content in the most logical section, or add a new section if needed.
3. Then commit the updated doc.

> Rationale: the docs are the source of truth. A clarification that lives only in chat history is lost.

### Reference Policy (MANDATORY)

- **Never rely solely on training data** for factual claims about APIs, versions, behaviour, or hardware specs.
- **Always verify with online references** before including them:
  - Use the `fetch_webpage` tool to check that a URL returns valid content before linking it.
  - If the page is unreachable or returns an error, do not include the link.
  - Prefer official documentation: [Espressif docs](https://docs.espressif.com), [FreeRTOS docs](https://www.freertos.org/Documentation/RTOS_book.html), [cppreference](https://en.cppreference.com).
- Each learning file should include a **References** section with at least one verified external link.

### `docs/learning/INDEX.md` Format

```markdown
# Learning Index

## FreeRTOS
- [Task pinning to a core](freertos-task-pinning.md)

## ESP-IDF Setup
- [Environment variables and IDF_PYTHON_ENV_PATH](idf-env-setup.md)

## C/C++ Basics
- [Entry point: app_main vs main](esp32-entry-point.md)
```

## Git Commit Rules (MANDATORY)

### Commit after every completed task

After completing any task — code change, doc update, config edit — always commit the result before moving on.

### Use Conventional Commits format

```
<type>(<scope>): <short summary>

<body — what changed and why, bullet points if multiple items>
```

**Common types:**

| Type | Use for |
|------|---------|
| `feat` | New feature or capability added to code |
| `fix` | Bug fix |
| `docs` | Documentation changes only (`docs/learning/`, `README.md`, `AGENTS.md`) |
| `chore` | Tooling, config, build system changes |
| `refactor` | Code restructured without behaviour change |
| `style` | Formatting only (no logic change) |

**Scope** is the area affected, e.g. `main`, `freertos`, `idf-setup`, `agents`.

### Group commits by concern — never bundle unrelated changes

Each commit must contain **only related changes**. Split into separate commits when changes span unrelated areas:

```
# WRONG — one big commit mixing code and docs
git commit -m "add task pinning and update docs and fix monitor note"

# CORRECT — separate commits
git commit -m "feat(main): add xTaskCreatePinnedToCore example on core 1"
git commit -m "docs(learning): add freertos-task-pinning learning file"
git commit -m "docs(agents): add knowledge management and commit rules"
```

### Commit body guidelines

- Use the body to explain **what** changed and **why**, not just how.
- Use bullet points for multiple related changes within the same scope.
- Reference the source of truth when relevant (e.g. ESP-IDF docs URL).

### Example commit

```
docs(learning): add FreeRTOS task pinning guide

- Add docs/learning/freertos-task-pinning.md covering xTaskCreatePinnedToCore
- Add docs/learning/esp32-app-main-basics.md covering mandatory firmware structure
- Update docs/learning/INDEX.md with new entries under FreeRTOS and ESP-IDF Basics

References verified against:
https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/freertos_idf.html
```

## ESP-IDF Monitor Control

### Exiting Monitor Mode

When running `idf.py monitor`, use these shortcuts:

| Shortcut | Action |
|----------|--------|
| `Ctrl`+`]` | Exit monitor (standard) |
| `Ctrl`+`T` then `X` | Alternative exit |
| `Ctrl`+`A` then `K` | If using `screen` backend |

> **Note:** `Ctrl+C` does NOT exit ESP-IDF monitor - it only interrupts the running app.

## Finding ESP32 Serial Port

When flashing ESP32, first detect the correct `/dev/ttyUSB*` or `/dev/ttyACM*` device path:

### Quick Detection
```bash
# List USB serial devices
ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null

# Check USB devices (look for ESP32 bridge chips)
lsusb
# Common chips: "Silicon Labs CP210x", "QinHeng CH340", "FTDI"

# Watch kernel messages when plugging/unplugging
sudo dmesg -w
```

### Before/After Comparison
```bash
# Run before plugging in ESP32
ls /dev/tty* > /tmp/before.txt

# Plug in ESP32, then run
ls /dev/tty* > /tmp/after.txt

# Compare to find the new device
diff /tmp/before.txt /tmp/after.txt
```

### Fixing Permission Issues

If you get "not readable" error:

```bash
# Option 1: Add user to dialout group (permanent, requires logout)
sudo usermod -a -G dialout $USER

# Option 2: Quick fix (temporary, resets on unplug)
sudo chmod 666 /dev/ttyUSB0
```

### Setting up ESP-IDF Environment

```bash
# Step 1: Activate ESP-IDF Python environment (ALWAYS do this first in any session)
source ~/.espressif/tools/activate_idf_v6.0.1.sh

# Step 2: Then flash
idf.py -p /dev/ttyUSB0 flash monitor
```

## Project Structure

- This project uses ESP-IDF framework
- ESP-IDF located at: `~/.espressif/v6.0.1/esp-idf/`

## VS Code Diagnostics

If VS Code shows errors such as `cannot open source file "freertos/FreeRTOS.h"`, verify workspace settings in `.vscode/settings.json` include:

- `C_Cpp.default.configurationProvider`: `espressif.esp-idf-extension`
- `C_Cpp.default.compileCommands`: `${workspaceFolder}/build/compile_commands.json`
- `C_Cpp.default.compilerPath`: Xtensa toolchain GCC path (for ESP32 target)
- `idf.port`: serial device path such as `/dev/ttyUSB0`
- `idf.flashType`: `UART`

Notes:

- `build/compile_commands.json` contains ESP-IDF include paths (including FreeRTOS headers).
- If paths are still unresolved, run `C/C++: Reset IntelliSense Database` and reload the VS Code window.

### "Setup from environment variables is not valid: IDF_PYTHON_ENV_PATH is not set"

Cause: the EIM-installed activate script (`~/.espressif/tools/activate_idf_v<ver>.sh`) does NOT export `IDF_PYTHON_ENV_PATH` or `IDF_TOOLS_PATH`, and the ESP-IDF extension's `idf.currentSetup` must match a key under `idfInstalled` in `~/.espressif/idf-env.json` (e.g. `/home/huyang/.espressif/v6.0.1/esp-idf-v6.0`, NOT just `.../esp-idf`). When the key does not match, the extension falls back to env-var setup, which then fails.

Fix in `.vscode/settings.json`:

```jsonc
"idf.currentSetup": "/home/huyang/.espressif/v6.0.1/esp-idf-v6.0", // exact key from idf-env.json
"idf.customExtraVars": {
  "IDF_PATH": "/home/huyang/.espressif/v6.0.1/esp-idf",
  "IDF_TOOLS_PATH": "/home/huyang/.espressif",
  "IDF_PYTHON_ENV_PATH": "/home/huyang/.espressif/tools/python/v<ver>/venv",
  "ESP_IDF_VERSION": "6.0.1"
}
```

Verify the exact `idfInstalled` key:

```bash
python3 -c "import json; print(list(json.load(open('$HOME/.espressif/idf-env.json'))['idfInstalled']))"
```

For a CLI shell (when launching `idf.py` outside the activate script), also export:

```bash
export IDF_TOOLS_PATH=$HOME/.espressif
export IDF_PYTHON_ENV_PATH=$HOME/.espressif/tools/python/v6.0.1/venv
```

## nRF24L01+ Register Write Order — CRITICAL

### The EN_AA / EN_CRC Override Trap

When configuring the nRF24L01+ for BLE passive reception, **EN_AA MUST be written to 0x00 BEFORE CONFIG is written with EN_CRC=0**.

The nRF24L01+ datasheet states: *"If the EN_AA is set for any pipe, the EN_CRC bit in the CONFIG register is forced high."*

The power-on reset value of EN_AA is **0x3F** (all pipes auto-ACK enabled). If CONFIG is written with EN_CRC=0 while EN_AA is still 0x3F:
1. The hardware overrides EN_CRC to 1 internally
2. After clearing EN_AA=0x00, the stored CONFIG value **still has EN_CRC=1**
3. The nRF24 now expects CRC in every received packet
4. BLE packets use CRC-24 (3 bytes), not nRF24's CRC-1/CRC-2 (1–2 bytes)
5. **Every BLE packet fails CRC check and is silently discarded**
6. The RX FIFO stays empty; `read_payload()` returns 0xFE (power-on default)

### Symptom Pattern

| Symptom | Cause |
|---------|-------|
| RX FIFO always 0xFE | CRC rejection → no valid packets reach FIFO |
| RX_DR set but no real data | FIFO read from empty buffer returns 0xFE |
| RPD signal very rare (0.1%) | nRF24 can detect RF energy but can't demodulate with wrong CRC |
| Channel-dependent "MACs" after dewhitening | 0xFE × 32 dewhittened produces fake but deterministic PDUs |

### Prevention

```cpp
// BROKEN — CONFIG written before EN_AA
radio.write_reg(cfg);       // EN_CRC=0 → OVERRIDDEN to 1 because EN_AA=0x3F
radio.write_reg(en_aa);     // EN_AA=0x00 → too late, CONFIG already stored with EN_CRC=1

// CORRECT — EN_AA zeroed first
radio.write_reg(en_aa);     // EN_AA=0x00 → no forcing condition exists
radio.write_reg(cfg);       // EN_CRC=0 → accepted as-is
```

### Clone Chip Detection

On genuine nRF24L01+, writing EN_AA=0x00 then CONFIG with EN_CRC=0 works correctly.
On Si24R1 clone chips, the CRC forcing may persist even after EN_AA=0x00.
Always read back CONFIG after writing and verify EN_CRC matches the intended value.

## nRF24L01+ SPI Address Register Byte Order — CRITICAL

### The LSByte-First Trap

The nRF24L01+ datasheet §8.3.1 states: **"LSByte is written first"** for multi-byte address registers (RX_ADDR_P0, RX_ADDR_P1, TX_ADDR). This means the **first SPI data byte** maps to the **LSByte** of the address register, and the **last byte** maps to the **MSByte**.

On-air, the nRF24 transmits the **MSByte first** (datasheet §7.3: "MSB to the left"). This is the OPPOSITE of the SPI write order, creating a common trap:

```cpp
// BROKEN — bytes in MSByte-first SPI order (looks "natural" but is wrong)
//   On-air: MSByte=0x71 first, LSByte=0x6B last → matches AA 0x71919D6B ≠ 0x8E89BED6
inline constexpr uint8_t ADV_ACCESS_ADDR[4] = {0x6B, 0x7D, 0x91, 0x71};

// CORRECT — bytes in LSByte-first SPI order (per datasheet §8.3.1)
//   On-air: MSByte=0x6B first, LSByte=0x71 last → matches AA 0x8E89BED6
inline constexpr uint8_t ADV_ACCESS_ADDR[4] = {0x71, 0x91, 0x7D, 0x6B};
```

### Why This Is Easy to Get Wrong

1. **Two independent bit/byte inversions**: BLE transmits LSBit-first per byte, nRF24 expects MSBit-first → requires `swapbits()`. BLE transmits LSByte-first on air, nRF24 SPI reads LSByte-first. These two inversions compose, and getting them wrong is nearly impossible to diagnose from the output alone.

2. **Diagnostic readback passes**: If you write the wrong byte sequence and then read back the same register, you get the same wrong sequence — the comparison always passes. A self-consistent error is invisible to readback verification.

3. **RPD detects RF ≠ packets received**: The nRF24 still detects RF energy (RPD=1) because that's a physical-layer measurement. But address matching fails silently, so the FIFO stays empty.

### Transformation Chain (BLE AA → nRF24 SPI)

```
BLE Access Address: 0x8E89BED6

Step 1: BLE on-air (LSByte-first): D6 BE 89 8E
Step 2: Per-byte bit reversal (swapbits): 6B 7D 91 71  (MSByte-first on air)
Step 3: nRF24 SPI write (LSByte-first): 71 91 7D 6B  (FIRST byte = LSByte)
```

Cross-verified against Dmitry Grinberg's reference implementation (`dmitry.gr`).

### Symptom Pattern

| Symptom | Cause |
|---------|-------|
| RPD=1 occasionally | RF energy detected (physical layer works) |
| FIFO always empty | Address never matches → no packets enter FIFO |
| 0xFE FIFO reads | Power-on default, not real data |
| Channel-dependent "MACs" | 0x7F × 32 dewhittened produces deterministic but fake PDUs |

### Prevention

- Always verify multi-byte register SPI byte order against the datasheet AND an independent reference implementation
- Never trust readback verification alone for self-consistent errors (write wrong → read same wrong → compare passes)
- The challenger review that flagged this was **CORRECT** — it was dismissed based on an incomplete analysis of byte ordering conventions

## nRF24L01+ CE GPIO Configuration — CRITICAL

### The GPIO_MODE_OUTPUT Readback Trap

On ESP32, `GPIO_MODE_OUTPUT` disables the input buffer. `gpio_get_level()` on an output-only pin **always returns 0** regardless of the actual output level. This makes CE readback diagnostics show `gpio_get_level()=0` even when the pin is physically HIGH.

### Prevention

```cpp
// BROKEN — output-only, gpio_get_level() always returns 0
gpio_config_t gpio_cfg = {
    .mode = GPIO_MODE_OUTPUT,  // ← input buffer disabled
};

// CORRECT — input-output, gpio_get_level() returns actual pad level
gpio_config_t gpio_cfg = {
    .mode = GPIO_MODE_INPUT_OUTPUT,  // ← input buffer enabled
};
```

Also note: GPIO5 is SPI3_HOST's native IO_MUX CS0 pin. If using SPI3_HOST and CE=GPIO5, the SPI driver internally enables IO_MUX CS0 (cs0_dis=0). While `PIN_FUNC_GPIO` disconnects the IO_MUX signal from the pad, consider moving CE to a non-IOMUX pin (e.g., GPIO4) for maximum safety.
