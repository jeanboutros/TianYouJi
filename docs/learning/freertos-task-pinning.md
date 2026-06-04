# FreeRTOS Task Pinning to a Core (ESP32)

## What was learned

ESP-IDF runs FreeRTOS in SMP (Symmetric Multiprocessing) mode on the ESP32's two cores:

- **Core 0** (`PRO_CPU`) — used by the Wi-Fi/Bluetooth stack
- **Core 1** (`APP_CPU`) — free for application tasks

To pin a task to a specific core, use `xTaskCreatePinnedToCore()` instead of the standard `xTaskCreate()`.

> **Note:** In IDF FreeRTOS, stack sizes are specified in **bytes**, not words (unlike vanilla FreeRTOS).

## Code examples

```c
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void my_task(void *pvParameters)
{
    while (1) {
        printf("Running on core %d\n", xPortGetCoreID());
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    vTaskDelete(NULL); // never return; delete self if loop exits
}

void app_main(void)
{
    xTaskCreatePinnedToCore(
        my_task,    // task function
        "my_task",  // name (debug only, max 16 chars by default)
        2048,       // stack size in BYTES (IDF-specific)
        NULL,       // parameter passed to task
        5,          // priority (1–24 typical; higher = more urgent)
        NULL,       // task handle (NULL if not needed)
        1           // core: 0 = PRO_CPU, 1 = APP_CPU
    );

    vTaskDelete(NULL); // app_main must not return
}
```

### Verify the core at runtime
```c
printf("core: %d\n", xPortGetCoreID()); // prints 0 or 1
```

### Use `tskNO_AFFINITY` to let the scheduler choose
```c
xTaskCreatePinnedToCore(my_task, "t", 2048, NULL, 5, NULL, tskNO_AFFINITY);
```

## Pitfalls

- **Never return from a task function** — always loop or call `vTaskDelete(NULL)`.
- **Stack overflow**: if the task crashes with a `LoadProhibited` or watchdog reset, increase the stack size (try 4096 or 8192).
- **Watchdog resets**: always yield with `vTaskDelay()` or a blocking call inside the loop; never busy-spin indefinitely.
- **Stack size is in bytes in IDF**, not words — this differs from vanilla FreeRTOS documentation.
- Avoid calling `vTaskDelete()` on a task running on the other core; prefer self-deletion (`vTaskDelete(NULL)`).

## References

- [ESP-IDF FreeRTOS SMP overview and xTaskCreatePinnedToCore API](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/freertos_idf.html) *(verified 2026-06-04)*
- [FreeRTOS xTaskCreate API reference](https://www.freertos.org/Documentation/02-Kernel/04-API-references/01-Task-creation/01-xTaskCreate) *(verified 2026-06-04)*
