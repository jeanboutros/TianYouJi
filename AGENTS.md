# ESP32 Development Agent Notes

> **Important:** After managing a new challenge in a conversation, update this AGENTS.md file with the solution.

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
- Enum enumerator names must come **verbatim from the datasheet symbol names** wherever possible (e.g. `RF_DR_LOW`, `RF_PWR`). Use CamelCase only when the datasheet uses all-caps abbreviations that would be unreadable as-is.
- **No "catch-all" or "raw value" enumerators** (e.g. `Raw = 0xFF`). If the user needs a raw escape hatch, provide a separate `from_byte()` / `to_byte()` path on the struct, not inside the enum.

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
- Expose **one aggregate struct per register** with typed fields, `to_byte()`, and `from_byte()`.
- Expose **`to_reg(FieldType)` free functions** (overloaded per field type) so users can compose individual fields without constructing a full struct.
- Keep internal helpers (bit-splitting, encoding tables) in a `detail` sub-namespace so they do not pollute the public API.
- Headers are a library contract — **no learning notes, no TODO comments, no design rationale** in headers. That material belongs in `docs/learning/`.

### Completeness — model the full register, nothing less

- Every bit in a register must be accounted for, even reserved bits.
- Reserved bits are represented as unnamed padding or a note in the Doxygen block — **never silently ignored** in `to_byte()` or `from_byte()`.
- If a multi-bit field has a non-contiguous encoding (e.g. nRF24 `DataRate` spans bits 5 and 3), the encoding logic **must follow the datasheet's table exactly** — use a helper in `detail::` rather than inventing a compact representation.

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
