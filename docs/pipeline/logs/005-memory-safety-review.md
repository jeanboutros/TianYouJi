# Memory Safety Review — ESP32 nRF24L01+ Project

**Reviewer:** Memory Safety Reviewer  
**Date:** 2026-06-06  
**Scope:** All C++ source files in `components/nrf24l01plus/`, `components/nrf24_espidf/`, `main/main.cpp`, and `tests/test_rf_setup.cpp`

---

## Self-Audit Checklist

| Category | Checked? | Finding or PASS |
|----------|----------|-----------------|
| Build passes (`idf.py build` exit 0) | no | NOT VERIFIED — build not run in this session |
| Typed enums (no raw integers in API) | yes | PASS — all register fields use `enum class`; checked config.h, rf_setup.h, setup_retr.h, feature.h, setup_aw.h |
| Doxygen on new public symbols | yes | PASS — all public structs, enums, functions have `/** @brief */` blocks with @param/@return |
| Datasheet fidelity (fields match) | yes | PASS — register bit layouts match nRF24L01+ Product Spec v1.0 Tables 8–28; checked CONFIG, RF_SETUP, STATUS, EN_AA, etc. |
| HAL decoupling (no platform headers in library) | yes | PASS — nrf24l01plus/include/ only includes `<cstdint>`, `<cstdio>`, own headers; no ESP-IDF headers |
| Reserved bits handled | yes | PASS — `to_byte()` masks/truncates reserved bits; `from_byte()` ignores upper bits (e.g. EnAa from_byte(0xFF).to_byte() == 0x3F) |
| No magic numbers in @code examples | yes | PASS — all examples use typed enums/constants |
| Buffer safety (bounded copies) | yes | FINDING — see F3, F4 below: Driver API lacks bounds validation; RxConfig::channel_at has null-ptr risk |
| AGENTS.md compliance | yes | PASS — conventional commits, Doxygen, typed enums, HAL decoupling all followed |
| Conventional commit ready | yes | PASS — format matches `<type>(<scope>): <summary>` |

---

## 10-Point Memory Safety Verification Protocol

### 1. RAII Compliance

**Status: FINDING (F1)**

| Area | Assessment |
|------|-----------|
| `nrf24::Hal` base class | `virtual ~Hal() = default;` — CORRECT |
| `nrf24::Driver` | No resources acquired; holds `Hal&` reference; no destructor needed — CORRECT |
| `nrf24::EspIdfHal` | **FINDING F1**: Acquires SPI bus, SPI device, and GPIO in `init()` but has no destructor or `deinit()` to release them. RAII violation. |
| Library classes (Config, RfSetup, etc.) | Value types, no resources; stack-allocated — CORRECT |
| Bare new/delete/malloc/free | None found anywhere in codebase — PASS |

### 2. Buffer Bounds

**Status: FINDINGS (F3, F4)**

| Location | Code | Assessment |
|----------|------|-----------|
| `driver.cpp:18` | `write_reg_multi(reg, data, len)` | **F4**: No validation that `len` ≤ 5 (nRF24 max address width) or that `data` is non-null when `len > 0` |
| `driver.cpp:23` | `read_reg_multi(reg, buf, len)` | **F4**: No validation that `buf` is non-null when `len > 0` |
| `driver.cpp:28` | `read_payload(buf, len)` | **F4**: No validation that `len` ≤ 32 (nRF24 max payload) or that `buf` ≥ `len` bytes |
| `hal_espidf.cpp:55` | `memcpy(t.tx_data, tx, len)` | Bounded by `len <= 4` check — PASS |
| `hal_espidf.cpp:73` | `memcpy(rx, t.rx_data, len)` | Bounded by `len <= 4` check — PASS |
| `status.h:128` | `snprintf(buf, len, ...)` | Properly bounded by `len` parameter — PASS |
| `diag.cpp:85` | `uint8_t addr[4]; read_reg_multi(..., addr, 4)` | Buffer = 4 bytes, read = 4 bytes — PASS |
| `main.cpp:81-82` | `uint8_t buf[32]; radio.read_payload(buf, 32)` | Buffer = 32 bytes, len = 32 (max) — PASS |
| `ble_config.h:127` | `extra_channels[seq_idx - 3]` | **F3**: Null dereference if `extra_channels == nullptr` and `seq_idx ≥ 3` |

### 3. Lifetime Safety

**Status: PASS (with advisory)**

| Area | Assessment |
|------|-----------|
| `Driver::hal_` reference | Non-owning `Hal&`, lifetime documented as "must outlive Driver". In main.cpp both are `static` with `hal` declared first — PASS |
| `RxConfig::extra_channels` | Non-owning `const uint8_t*`, lifetime documented. Advisory: dangling risk if caller allows array to go out of scope |
| Returning references to locals | None found — PASS |
| Use-after-free | No dynamic deallocation occurs — PASS |

### 4. Ownership Clarity

**Status: FINDING (F1)**

| Area | Assessment |
|------|-----------|
| `Driver::hal_` | Non-owning reference — documented — PASS |
| `EspIdfHal::spi_handle_` | **F1**: Acquired in `init()`, never released. No clear ownership transfer or cleanup path |
| `RxConfig::extra_channels` | Non-owning raw pointer, documented — PASS |
| Static `hal` and `radio` in main.cpp | Static lifetime, clear ownership — PASS |
| No bare new/delete | No heap allocations in user code — PASS |

### 5. Stack Depth

**Status: ADVISORY (F5)**

**Task:** `ble_sniffer_task` (main.cpp:136)  
**Stack allocated:** 4096 bytes via `xTaskCreatePinnedToCore("ble_sniffer", 4096, NULL, 5, NULL, 1)`

**Call chain depth analysis:**

```
ble_sniffer_task()                     ~120 bytes (locals: buf[32], st_buf[96], counters)
  ├── nrf24::ble::switch_channel()      ~20 bytes
  │   ├── radio.ce_low()                 ~8 bytes
  │   ├── radio.write_reg()              ~8 bytes
  │   │   └── spi_xfer()                 ~48 bytes (spi_transaction_t on stack)
  │   ├── clear_irq_flags()             ~8 bytes
  │   └── radio.flush_rx()              ~8 bytes
  ├── nrf24::ble::rx_available()         ~16 bytes
  ├── radio.read_payload()              ~8 bytes
  │   └── spi_xfer()                     ~48 bytes
  ├── nrf24::ble::dewhiten()            ~8 bytes
  ├── printf()                           ~256-512 bytes (ESP-IDF printf stack)
  └── nrf24::Status::format()           ~32 bytes
      └── snprintf()                    ~64 bytes
```

**Estimated peak stack usage:** ~1200-1800 bytes  
**Margin:** 4096 / 1500 ≈ **2.7x** — above 1.5x threshold  
**F5: `uxTaskGetStackHighWaterMark()` never called to verify at runtime.**

### 6. Heap Fragmentation

**Status: PASS**

| Assessment | Detail |
|-----------|--------|
| Dynamic allocation in init | `spi_bus_initialize()` allocates internally — ESP-IDF managed, happens once |
| Dynamic allocation in steady-state | **NONE** — all library code uses stack/static allocation exclusively |
| Periodic malloc/free in main loop | **NONE** — `ble_sniffer_task` uses only stack-local variables |
| Risk | Very low — no heap churn |

### 7. Smart Pointer Usage

**Status: PASS (N/A)**

No heap allocations in user code. All objects are stack-allocated or static. `std::unique_ptr` and `std::shared_ptr` are not needed because there are no owning heap allocations to manage. This is the correct approach for an embedded project with deterministic memory requirements.

### 8. DMA Safety

**Status: PASS**

| Assessment | Detail |
|-----------|--------|
| DMA disabled | `SPI_DMA_DISABLED` passed to `spi_bus_initialize()` (hal_espidf.cpp:20) |
| Transfer mode | `spi_device_polling_transmit()` — polling, no DMA |
| Small transfers | Use `tx_data[4]`/`rx_data[4]` embedded in `spi_transaction_t` — PASS |
| Large transfers | Caller-provided stack buffers passed as `tx_buffer`/`rx_buffer` — polling copies byte-by-byte, no alignment requirement |
| Cache coherency | Not relevant with DMA disabled — PASS |

### 9. Sanitizer Integration

**Status: FINDING (F2)**

**tests/CMakeLists.txt** contains no sanitizer flags:
```cmake
add_executable(test_rf_setup test_rf_setup.cpp)
target_include_directories(test_rf_setup PRIVATE ...)
target_link_libraries(test_rf_setup GTest::gtest_main)
```

**Missing:**
- `-fsanitize=address` compile flag
- `-fno-omit-frame-pointer` compile flag
- `-fsanitize=address` link flag
- No Valgrind target

### 10. Virtual Destructors

**Status: PASS**

| Class | Virtual methods? | Virtual destructor? | Assessment |
|-------|-----------------|---------------------|-----------|
| `nrf24::Hal` | Yes (3 pure virtual) | `virtual ~Hal() = default;` | CORRECT |
| `nrf24::EspIdfHal` | Yes (overrides) | Inherited from Hal | CORRECT |
| `nrf24::RxPw` | No | N/A | Not polymorphic — no destructor needed |
| `nrf24::RxPwP0`–`RxPwP5` | No | N/A | Not polymorphic — no destructor needed |

---

## Findings

### F1 — [6] RAII Violation: EspIdfHal Resources Never Released

**Severity:** 6 (Medium-High)  
**File:** `components/nrf24_espidf/include/nrf24_espidf/hal_espidf.h` and `components/nrf24_espidf/src/hal_espidf.cpp`

**Vulnerable code:**

```cpp
// hal_espidf.h:34-72 — Class declaration has no destructor
class EspIdfHal final : public Hal {
public:
    void init(const EspIdfPins &pins);
    // ... no destructor, no deinit()
private:
    spi_device_handle_t spi_handle_{};
    gpio_num_t ce_pin_{};
    gpio_num_t mosi_pin_{};
};

// hal_espidf.cpp:7-41 — Resources acquired but never released
void EspIdfHal::init(const EspIdfPins &pins) {
    // ...
    ESP_ERROR_CHECK(spi_bus_initialize(pins.spi_host, &bus_cfg, SPI_DMA_DISABLED));
    ESP_ERROR_CHECK(spi_bus_add_device(pins.spi_host, &dev_cfg, &spi_handle_));
    gpio_config(&gpio_cfg);
    // No corresponding cleanup in destructor or deinit()
}
```

**Why it's a problem:** `spi_bus_initialize()`, `spi_bus_add_device()`, and `gpio_config()` acquire hardware resources. Without a destructor calling `spi_bus_remove_device()`, `spi_bus_free()`, and resetting GPIO, these resources leak when the object is destroyed. The current code is safe only because `EspIdfHal` is declared `static` (main.cpp:35) and never destroyed. But the class design violates RAII: if a future user creates `EspIdfHal` on the stack or as a member, destruction silently leaks SPI bus and GPIO resources.

**Recommended fix:** Add a destructor that calls `spi_bus_remove_device()`, `spi_bus_free()`, and resets GPIO. Consider adding a `deinit()` method for explicit early cleanup. Track initialization state with a `bool initialized_` member to avoid double-cleanup.

---

### F2 — [5] Host-Side Tests Lack ASAN Integration

**Severity:** 5 (Medium)  
**File:** `tests/CMakeLists.txt`

**Current code:**

```cmake
add_executable(test_rf_setup test_rf_setup.cpp)
target_include_directories(test_rf_setup PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/nrf24l01plus/include)
target_link_libraries(test_rf_setup GTest::gtest_main)
```

**Why it's a problem:** AddressSanitizer (ASAN) catches use-after-free, heap/stack buffer overflow, and memory leaks at test time. Without it, memory errors in the library code (especially in `Status::format()`, memcpy paths, or any future code) can go undetected. This is a standard practice gap, not an active bug.

**Recommended fix:**

```cmake
target_compile_options(test_rf_setup PRIVATE -fsanitize=address -fno-omit-frame-pointer -g)
target_link_options(test_rf_setup PRIVATE -fsanitize=address)
```

---

### F3 — [6] Null Pointer Dereference in RxConfig::channel_at()

**Severity:** 6 (Medium-High)  
**File:** `components/nrf24l01plus/include/nrf24l01plus/ble_config.h:124-128`

**Vulnerable code:**

```cpp
constexpr uint8_t channel_at(uint8_t seq_idx) const
{
    if (seq_idx < 3) return ADV_CHANNELS[seq_idx].ch_idx;
    return extra_channels[seq_idx - 3];  // <-- extra_channels may be nullptr
}
```

**Why it's a problem:** `extra_channels` defaults to `nullptr` and `extra_channel_count` defaults to `0`. If a caller passes `seq_idx >= 3` when `extra_channels == nullptr`, this dereferences a null pointer — undefined behavior (crash on most platforms). The function has no bounds check or null guard. While the contract says the caller should only pass `0 .. total_channels()-1`, there is no runtime enforcement.

**Current callers:**
- `main.cpp:59`: `cfg.channel_at(seq_i)` where `seq_i` cycles from 0 to `cfg.total_channels()-1`. With default config, `total_channels() == 3`, so `seq_i` ranges 0-2. SAFE in current usage. But the defensive programming gap means any future misuse crashes.

**Recommended fix:** Add a null check or assertion:

```cpp
constexpr uint8_t channel_at(uint8_t seq_idx) const
{
    if (seq_idx < 3) return ADV_CHANNELS[seq_idx].ch_idx;
    // extra_channels must be non-null for seq_idx >= 3
    return extra_channels ? extra_channels[seq_idx - 3] : 0;
}
```

Or use `assert(extra_channels != nullptr && seq_idx < total_channels())` for debug builds.

---

### F4 — [4] Driver API Lacks Input Bounds Validation

**Severity:** 4 (Medium)  
**Files:**
- `components/nrf24l01plus/include/nrf24l01plus/driver.h:77`
- `components/nrf24l01plus/include/nrf24l01plus/driver.h:90`
- `components/nrf24l01plus/include/nrf24l01plus/driver.h:101`
- `components/nrf24l01plus/src/driver.cpp:18-31`

**Vulnerable code:**

```cpp
// driver.h:77
void write_reg_multi(uint8_t reg, const uint8_t *data, uint8_t len);
// driver.h:90
void read_reg_multi(uint8_t reg, uint8_t *buf, uint8_t len);
// driver.h:101
void read_payload(uint8_t *buf, uint8_t len);
```

**Why it's a problem:** These functions accept `len` and raw buffer pointers without any validation:
- `read_payload(buf, len)` — nRF24L01+ max payload is 32 bytes; `len > 32` reads garbage from SPI and may overflow `buf`
- `write_reg_multi(reg, data, len)` — address registers accept max 5 bytes; `len > 5` writes beyond the register field
- `read_reg_multi(reg, buf, len)` — same concern
- No null-pointer checks on `data`/`buf`

Current callers use correct values (e.g., `read_payload(buf, 32)`, `write_reg_multi(..., addr, 4)`), but the API contract has no enforcement. A future caller passing `read_payload(buf, 5)` with a 5-byte buffer and `len=32` would overflow.

**Recommended fix:** Add bounds assertions or parameter validation:

```cpp
void read_payload(uint8_t *buf, uint8_t len) {
    assert(len <= 32 && buf != nullptr);
    hal_.spi_xfer(cmd::R_RX_PAYLOAD, nullptr, buf, len);
}
```

---

### F5 — [3] FreeRTOS Task Stack Depth Not Verified at Runtime

**Severity:** 3 (Low)  
**File:** `main/main.cpp:136`

**Vulnerable code:**

```cpp
xTaskCreatePinnedToCore(ble_sniffer_task, "ble_sniffer", 4096, NULL, 5, NULL, 1);
```

**Why it's a problem:** The 4096-byte stack is estimated at ~2.7x the projected peak usage (~1500 bytes). This exceeds the 1.5x requirement, but `uxTaskGetStackHighWaterMark()` is never called to verify actual consumption at runtime. Stack usage can increase unexpectedly due to deeper printf implementations, ESP-IDF SPI driver changes, or future code additions. Without runtime monitoring, a slow stack creep would go undetected until overflow.

**Recommended fix:** Add periodic high-water mark logging:

```cpp
printf("[stack] high water mark: %u bytes\n",
       uxTaskGetStackHighWaterMark(NULL));
```

Also add `configCHECK_FOR_STACK_OVERFLOW=2` in menuconfig for hardware-fault detection.

---

### F6 — [3] ADV_CHANNELS Index Without Bounds Validation

**Severity:** 3 (Low)  
**File:** `components/nrf24l01plus/src/ble_config.cpp:41` and `components/nrf24l01plus/include/nrf24l01plus/ble_config.h:62`

**Vulnerable code:**

```cpp
// ble_config.h:62
uint8_t initial_channel_idx = 0; ///< Index into ADV_CHANNELS (0–2) for initial tune

// ble_config.cpp:41
rf_ch.channel = ADV_CHANNELS[config.initial_channel_idx].rf_ch;
```

**Why it's a problem:** `ADV_CHANNELS` is a 3-element array. `initial_channel_idx` is `uint8_t` (0–255). If a caller sets `initial_channel_idx = 3`, the array access is out of bounds. The constraint is only documented in a comment — no runtime validation.

**Recommended fix:** Add bounds check in `configure_rx()` or use `assert(initial_channel_idx < 3)`.

---

### F7 — [2] Semantic Buffer Access Beyond PDU Length

**Severity:** 2 (Low)  
**File:** `main/main.cpp:88-94`

**Vulnerable code:**

```cpp
uint8_t pdu_type = buf[0] & 0x0F;
uint8_t pdu_len = buf[1] & 0x3F;
const char *type = pdu_type_names[pdu_type < 7 ? pdu_type : 7];

printf("[ch%u] %-17s  %02X:%02X:%02X:%02X:%02X:%02X  len=%u\n",
       ble_ch, type,
       buf[7], buf[6], buf[5], buf[4], buf[3], buf[2],
       pdu_len);
```

**Why it's a problem:** `buf[2]`–`buf[7]` are accessed as the BLE advertiser MAC address regardless of `pdu_len`. For a valid BLE ADV_IND PDU, the MAC starts at byte 2 and is 6 bytes, so indices 2–7 require the PDU to be at least 8 bytes. The `read_payload(buf, 32)` always fills all 32 bytes from the SPI FIFO, so indices 0–31 are always within the buffer — no actual overflow. However, for short or corrupted packets, the displayed "MAC address" bytes are FIFO residue, not meaningful data. This is a logic/display bug, not a memory safety violation.

**Recommended fix:** Validate `pdu_len >= 6` before displaying the MAC, or document that the sniffer assumes advertising PDU format.

---

## Verdict

```
VERDICT: CONDITIONAL PASS
SEVERITY: 6
FINDINGS:
  - [6] components/nrf24_espidf/include/nrf24_espidf/hal_espidf.h:34  EspIdfHal acquires SPI bus/device/GPIO resources in init() with no destructor or deinit() — RAII violation, resources leak if object is destroyed
  - [6] components/nrf24l01plus/include/nrf24l01plus/ble_config.h:127  RxConfig::channel_at() dereferences extra_channels (defaults to nullptr) when seq_idx >= 3 — null pointer dereference for any misuse
  - [5] tests/CMakeLists.txt:17  Host-side test binary lacks -fsanitize=address and -fno-omit-frame-pointer — no ASAN integration to catch memory errors at test time
  - [4] components/nrf24l01plus/include/nrf24l01plus/driver.h:77,90,101  Driver::write_reg_multi/read_reg_multi/read_payload accept len and buffer pointers without bounds validation — nRF24 limits (32-byte payload, 5-byte address) not enforced
  - [3] main/main.cpp:136  FreeRTOS task stack depth 4096 bytes not verified with uxTaskGetStackHighWaterMark() — estimated 2.7x margin adequate but unvalidated
  - [3] components/nrf24l01plus/src/ble_config.cpp:41  ADV_CHANNELS[initial_channel_idx] without bounds check — out-of-bounds if initial_channel_idx > 2
  - [2] main/main.cpp:88-94  buf[2]-buf[7] accessed as MAC address regardless of pdu_len — reads FIFO residue for short/corrupt packets (within 32-byte buffer, no overflow)
ROUTING: none (conditional pass — non-blocking fixes recommended before production use)
```

### Condition for Full Approval

1. **F1 (severity 6):** Add destructor to `EspIdfHal` that releases SPI bus/device/GPIO, or add `deinit()` method with RAII guard. Track initialization state to prevent double-cleanup.
2. **F3 (severity 6):** Add null-pointer guard or bounds assertion in `RxConfig::channel_at()`.
3. **F2 (severity 5):** Add `-fsanitize=address -fno-omit-frame-pointer -g` to `tests/CMakeLists.txt`.

F4–F7 are advisory and recommended for hardening but not blocking.

### Positive Observations

- **Zero bare new/delete/malloc/free** — the entire codebase uses stack and static allocation exclusively
- **Correct virtual destructor** on the `Hal` interface base class
- **Well-documented lifetime contracts** — `Driver::hal_` reference and `RxConfig::extra_channels` pointer have explicit lifetime documentation
- **Proper `snprintf` usage** — `Status::format()` respects buffer size parameter
- **No DMA risk** — DMA explicitly disabled, polling mode used
- **No heap fragmentation risk** — zero dynamic allocation in steady-state paths
- **ESP-IDF SPI transaction data arrays** correctly bounded by `len <= 4` checks
- **Design follows memory-safety best practices** for embedded: value types, stack-first, reference semantics for non-owning views
