# BLE Advertising with ESP32 (ESP-IDF)

## Overview

This document covers using the ESP32's built-in BLE radio to broadcast non-connectable advertising packets. This is the "proper" BLE way — using the full Bluetooth stack — before we later attempt the same thing from the NRF24L01+ using raw packet crafting.

---

## 1. BLE Advertising Basics

### What is BLE advertising?

BLE devices announce their presence by broadcasting short packets called **advertisements**. Any nearby BLE scanner (phone, laptop) can detect these without establishing a connection.

### Advertising types

| Type | Enum | Connectable | Scannable | Use case |
|---|---|---|---|---|
| ADV_IND | `ADV_TYPE_IND` | Yes | Yes | General discoverable (default) |
| ADV_DIRECT_IND | `ADV_TYPE_DIRECT_IND_HIGH` | Yes | No | Directed to specific device |
| ADV_SCAN_IND | `ADV_TYPE_SCAN_IND` | No | Yes | Discoverable, no connections |
| **ADV_NONCONN_IND** | `ADV_TYPE_NONCONN_IND` | **No** | **No** | **Broadcast-only beacon** |

**We use `ADV_TYPE_NONCONN_IND`** — this is a pure broadcast: no connections, no scan responses. Simplest and safest for learning.

### Advertising channels

BLE advertising happens on 3 fixed channels:
- Channel 37: 2402 MHz
- Channel 38: 2426 MHz  
- Channel 39: 2480 MHz

The advertiser cycles through all three channels each advertising interval.

### Advertising data format

Max 31 bytes, structured as Length-Type-Value (LTV) entries:
```
[Len1][Type1][Value1...][Len2][Type2][Value2...]...
```

Common types:
| Type code | Name | Example |
|---|---|---|
| 0x01 | Flags | `0x06` (General Discoverable + BR/EDR Not Supported) |
| 0x09 | Complete Local Name | "ESP32_BLE" |
| 0xFF | Manufacturer Specific | Custom data |

---

## 2. ESP-IDF BLE Stack Architecture

```
┌──────────────────────────────────┐
│         Your Application         │
│    (GAP callbacks, adv config)   │
├──────────────────────────────────┤
│        Bluedroid Host Stack      │
│  esp_gap_ble_api.h               │
├──────────────────────────────────┤
│      BT Controller (firmware)    │
│  esp_bt.h                        │
├──────────────────────────────────┤
│         BLE Radio Hardware       │
│  (2.4 GHz transceiver)           │
└──────────────────────────────────┘
```

### Required headers

```c
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
```

### CMakeLists.txt dependency

```cmake
idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES bt
)
```

### sdkconfig requirements

Enable Bluetooth in menuconfig:
```
CONFIG_BT_ENABLED=y
CONFIG_BT_BLUEDROID_ENABLED=y
CONFIG_BT_BLE_ENABLED=y
```

---

## 3. Initialization Sequence

The BLE stack must be initialized in a specific order:

```c
#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_log.h"

static const char *TAG = "BLE_ADV";

void ble_init(void) {
    // Step 1: Initialize NVS (required by BT stack)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Step 2: Release classic BT memory (we only use BLE)
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

    // Step 3: Initialize BT controller with default config
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));

    // Step 4: Enable BT controller in BLE mode
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));

    // Step 5: Initialize Bluedroid stack
    ESP_ERROR_CHECK(esp_bluedroid_init());

    // Step 6: Enable Bluedroid
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    // Step 7: Register GAP callback
    ESP_ERROR_CHECK(esp_ble_gap_register_callback(gap_event_handler));

    ESP_LOGI(TAG, "BLE initialized");
}
```

---

## 4. GAP Event Handler

The BLE stack is event-driven. After configuring advertising data, the stack fires events to confirm each step:

```c
static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
    switch (event) {
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        ESP_LOGI(TAG, "Advertising data set, starting advertising...");
        esp_ble_gap_start_advertising(&adv_params);
        break;

    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        if (param->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS) {
            ESP_LOGI(TAG, "Advertising started successfully");
        } else {
            ESP_LOGE(TAG, "Advertising start failed: %d", param->adv_start_cmpl.status);
        }
        break;

    case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
        ESP_LOGI(TAG, "Advertising stopped");
        break;

    default:
        break;
    }
}
```

---

## 5. Advertising Parameters

```c
static esp_ble_adv_params_t adv_params = {
    .adv_int_min = 0x0040,        // Min interval: 0x0040 * 0.625ms = 40ms
    .adv_int_max = 0x0080,        // Max interval: 0x0080 * 0.625ms = 80ms
    .adv_type = ADV_TYPE_NONCONN_IND,  // Non-connectable, non-scannable
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .channel_map = ADV_CHNL_ALL,  // Advertise on all 3 channels
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};
```

### Interval constraints

| Parameter | Range | Unit | Notes |
|---|---|---|---|
| `adv_int_min` | 0x0020–0x4000 | × 0.625 ms | Min 20 ms |
| `adv_int_max` | 0x0020–0x4000 | × 0.625 ms | Max 10.24 s |

For non-connectable advertising, minimum interval is 0x00A0 (100 ms) per BLE spec. However, ESP-IDF may allow lower values in practice.

---

## 6. Advertising Data Configuration

### Using the structured API (recommended)

```c
static esp_ble_adv_data_t adv_data = {
    .set_scan_rsp = false,          // This is advertising data, not scan response
    .include_name = true,           // Include device name
    .include_txpower = false,
    .min_interval = 0x0006,         // Slave connection interval (ignored for non-conn)
    .max_interval = 0x0010,
    .appearance = 0x0000,           // Unknown appearance
    .manufacturer_len = 0,
    .p_manufacturer_data = NULL,
    .service_data_len = 0,
    .p_service_data = NULL,
    .service_uuid_len = 0,
    .p_service_uuid = NULL,
    .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};
```

### Using raw data (for custom packets)

```c
// Raw advertising data: Flags + Complete Local Name "Hello"
static uint8_t raw_adv_data[] = {
    // Flags
    0x02,       // Length
    0x01,       // Type: Flags
    0x06,       // Value: General Disc + BR/EDR Not Supported

    // Complete Local Name
    0x06,       // Length (5 chars + 1 type byte)
    0x09,       // Type: Complete Local Name
    'H', 'e', 'l', 'l', 'o',
};

// Use raw API:
esp_ble_gap_config_adv_data_raw(raw_adv_data, sizeof(raw_adv_data));
```

---

## 7. Complete Example: Non-Connectable Beacon

```c
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_log.h"

static const char *TAG = "BLE_BEACON";

// Advertising parameters: non-connectable, 100ms interval
static esp_ble_adv_params_t adv_params = {
    .adv_int_min = 0x00A0,   // 100 ms
    .adv_int_max = 0x00A0,   // 100 ms
    .adv_type = ADV_TYPE_NONCONN_IND,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

// Advertising data: flags + custom name
static esp_ble_adv_data_t adv_data = {
    .set_scan_rsp = false,
    .include_name = true,
    .include_txpower = false,
    .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
    switch (event) {
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        esp_ble_gap_start_advertising(&adv_params);
        break;
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        if (param->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS) {
            ESP_LOGI(TAG, "Advertising started — scan with your phone!");
        }
        break;
    default:
        break;
    }
}

void app_main(void) {
    // NVS init
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    // Release unused classic BT memory
    esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);

    // BT controller
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_bt_controller_init(&bt_cfg);
    esp_bt_controller_enable(ESP_BT_MODE_BLE);

    // Bluedroid
    esp_bluedroid_init();
    esp_bluedroid_enable();

    // Set device name (will appear in scans)
    esp_ble_gap_set_device_name("ESP32_LEARN");

    // Register GAP callback
    esp_ble_gap_register_callback(gap_event_handler);

    // Configure advertising data (triggers ADV_DATA_SET_COMPLETE_EVT)
    esp_ble_gap_config_adv_data(&adv_data);

    ESP_LOGI(TAG, "BLE beacon initialized");
}
```

---

## 8. Changing the Device Name Dynamically

```c
// Change name and restart advertising
void set_beacon_name(const char *name) {
    esp_ble_gap_stop_advertising();
    esp_ble_gap_set_device_name(name);
    esp_ble_gap_config_adv_data(&adv_data);  // Re-triggers the event chain
}
```

---

## 9. Memory Considerations

The BLE stack uses significant RAM:

| Component | RAM usage |
|---|---|
| BT controller | ~60 KB |
| Bluedroid host | ~40 KB |
| Total | ~100 KB |

For advertising-only applications, you can reduce this by:
- Releasing classic BT memory: `esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT)`
- Using NimBLE instead of Bluedroid (lighter stack, ~30 KB less)

---

## 10. Verification

**Success criteria:**
1. Open a BLE scanner app on your phone (nRF Connect, LightBlue, or similar)
2. The device "ESP32_LEARN" appears in the scan list
3. It shows as non-connectable (no "Connect" button)
4. Signal strength (RSSI) is reasonable (-30 to -80 dBm depending on distance)

**Debug tips:**
- If device doesn't appear: check that BT is enabled in sdkconfig
- If name is truncated: BLE names in advertising are limited by the 31-byte payload
- Monitor output should show "Advertising started successfully"

---

## 11. Legal Notes

BLE advertising is **legal everywhere** — it's the designed purpose of the BLE specification. Every BLE device (phones, smartwatches, fitness trackers) advertises constantly.

What matters for legality:
- ✅ Non-connectable advertising with your own device name — always legal
- ✅ Changing your device name — legal
- ⚠️ Impersonating another device's name — legal grey area (no radio law violation, but may violate trademark/fraud laws)
- ❌ Flooding hundreds of fake advertisements to cause DoS — illegal (intentional interference)

---

## References

- [ESP-IDF BLE GAP API](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/bluetooth/esp_gap_ble.html) *(verified 2026-06-04)*
- [Bluetooth Core Specification v5.x](https://www.bluetooth.com/specifications/specs/core-specification/) — Vol 3, Part C (GAP), Vol 6, Part B (Link Layer)
- [ESP-IDF BLE advertising example](https://github.com/espressif/esp-idf/tree/master/examples/bluetooth/bluedroid/ble/gatt_security_server)
