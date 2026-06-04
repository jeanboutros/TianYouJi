# ESP32 Firmware Entry Point and Mandatory Building Blocks

## What was learned

ESP-IDF does not use the standard C `main()` function. Instead, the framework calls `app_main()` after the RTOS scheduler starts.

### Mandatory elements in every ESP-IDF firmware

| Element | Why it cannot be removed |
|---|---|
| `void app_main(void)` | The only function ESP-IDF calls at boot. Without it, the firmware does not run. |
| `#include "freertos/FreeRTOS.h"` + `#include "freertos/task.h"` | Required for any FreeRTOS call, including `vTaskDelay`. |
| `vTaskDelay(...)` (or equivalent blocking call) | Must yield the CPU periodically. A tight infinite loop triggers the watchdog timer and causes a reboot. |
| No bare `return` from `app_main` | Returning from `app_main` is undefined behaviour. Use an infinite loop or `esp_restart()`. |

### Minimal skeleton

```c
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void app_main(void)
{
    while (1) {
        // your code here
        vTaskDelay(pdMS_TO_TICKS(1000)); // yield every 1 s
    }
}
```

## Code examples

### Original hello_world structure (annotated)

```c
#include <stdio.h>
#include "freertos/FreeRTOS.h"  // MANDATORY
#include "freertos/task.h"      // MANDATORY (for vTaskDelay)
#include "esp_system.h"         // optional (for esp_restart)

void app_main(void)             // MANDATORY entry point
{
    // ... optional demo code ...

    for (int i = 10; i >= 0; i--) {
        vTaskDelay(1000 / portTICK_PERIOD_MS); // MANDATORY: yield the CPU
    }
    esp_restart(); // MANDATORY: do not return from app_main
}
```

## Pitfalls

- `app_main` **must not return** — it must loop forever or call `esp_restart()`.
- `Ctrl+C` in `idf.py monitor` does **not** exit the monitor; use `Ctrl+]` instead.
- `portTICK_PERIOD_MS` and `pdMS_TO_TICKS(ms)` are interchangeable for delay calculations; prefer `pdMS_TO_TICKS` for clarity.

## References

- [ESP-IDF Application Startup Flow](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/startup.html) *(verify before citing)*
- [FreeRTOS (IDF) overview](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/freertos_idf.html) *(verified 2026-06-04)*
