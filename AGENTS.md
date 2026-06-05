# ESP32 Development Agent Notes

> **Important:** After managing a new challenge in a conversation, update this AGENTS.md file with the solution.

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
