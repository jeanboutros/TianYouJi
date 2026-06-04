# NRF24L01+ Passive Spectrum Scan

## Overview

The NRF24L01+ can act as a simple 2.4 GHz spectrum analyzer by using its **Received Power Detector (RPD)** register. This is completely passive (RX only) — no transmission occurs, making it legal everywhere.

---

## 1. How It Works

The NRF24L01+ RPD register (0x09) indicates whether RF energy above **-64 dBm** was detected on the current channel. By cycling through all 126 channels and reading RPD, you build a picture of 2.4 GHz spectrum usage.

### RPD register (0x09)

| Bit | Value | Meaning |
|---|---|---|
| 0 | 0 | No signal detected above -64 dBm |
| 0 | 1 | Signal detected above -64 dBm on current channel |

**Limitation:** RPD is binary (above/below threshold), not a power level measurement. For a "heatmap" effect, sample each channel multiple times and count how often RPD=1.

---

## 2. Scanning Algorithm

```
For each channel 0..125:
    1. Set RF_CH to channel
    2. Enter RX mode (CE high)
    3. Wait ~200µs (enough for RPD to latch)
    4. Read RPD register
    5. CE low (back to standby)
    6. Record result
Repeat N times for statistical accuracy
```

### Why 200µs?

RPD needs approximately 170µs to settle after entering RX mode. The 200µs wait ensures a valid reading.

---

## 3. Implementation

```c
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#define NUM_CHANNELS  126
#define SAMPLES       100

static uint16_t channel_hits[NUM_CHANNELS];

void nrf24_spectrum_scan(void) {
    memset(channel_hits, 0, sizeof(channel_hits));

    // Disable auto-ACK (we're just scanning, not communicating)
    nrf24_write_reg(0x01, 0x00);  // EN_AA: all disabled

    // Disable all RX pipes except pipe 0
    nrf24_write_reg(0x02, 0x01);  // EN_RXADDR: pipe 0 only

    // Set to 2Mbps for wider bandwidth detection
    nrf24_write_reg(0x06, 0x08);  // RF_SETUP: 2Mbps, -18dBm (power irrelevant for RX)

    // Power up in RX mode
    nrf24_write_reg(0x00, 0x0F);  // CONFIG: PWR_UP=1, PRIM_RX=1
    vTaskDelay(pdMS_TO_TICKS(2));  // Power-up delay

    for (int sample = 0; sample < SAMPLES; sample++) {
        for (int ch = 0; ch < NUM_CHANNELS; ch++) {
            // Set channel
            nrf24_write_reg(0x05, ch);

            // Enter RX mode
            nrf24_ce_high();
            esp_rom_delay_us(200);  // Wait for RPD to settle

            // Read RPD
            uint8_t rpd = nrf24_read_reg(0x09);
            if (rpd & 0x01) {
                channel_hits[ch]++;
            }

            // Back to standby
            nrf24_ce_low();
        }
    }
}
```

---

## 4. Displaying Results

### ASCII bar chart

```c
void print_spectrum(void) {
    printf("\n=== 2.4 GHz Spectrum (NRF24 channels 0-125) ===\n");
    printf("Channel : Frequency : Hits/%d : Bar\n", SAMPLES);
    printf("--------|-----------|--------|----\n");

    for (int ch = 0; ch < NUM_CHANNELS; ch++) {
        if (channel_hits[ch] > 0) {
            int freq = 2400 + ch;
            int bar_len = (channel_hits[ch] * 40) / SAMPLES;  // Scale to 40 chars

            printf("  %3d   : %4d MHz  : %3d    : ", ch, freq, channel_hits[ch]);
            for (int i = 0; i < bar_len; i++) printf("█");
            printf("\n");
        }
    }
}
```

### Expected output interpretation

| Channels | Frequency | What you'll see |
|---|---|---|
| 1–14 | 2401–2414 MHz | WiFi channel 1 (center 2412) |
| 21–34 | 2421–2434 MHz | WiFi channel 6 (center 2437) |
| 51–64 | 2451–2464 MHz | WiFi channel 11 (center 2462) |
| 2, 26, 80 | 2402, 2426, 2480 MHz | BLE advertising channels 37/38/39 |
| Sporadic hits across range | — | Bluetooth classic (frequency hopping) |

---

## 5. Improving Accuracy

### Multiple samples per channel

RPD is binary, so more samples = better resolution. 100–200 samples per pass gives a good picture.

### Continuous monitoring

```c
void app_main(void) {
    // Init SPI + NRF24...

    while (1) {
        nrf24_spectrum_scan();
        print_spectrum();
        printf("\n--- Rescanning in 2 seconds ---\n");
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
```

### Timing considerations

- 126 channels × 200µs = ~25ms per sweep
- 100 sweeps = ~2.5 seconds total scan time
- Faster sweeps (fewer samples) catch transient bursts
- Slower sweeps (more samples) give better statistical picture

---

## 6. What This WON'T Detect

| Signal | Detectable? | Why |
|---|---|---|
| Active WiFi AP with clients | Yes | Continuous broadband signal, strong |
| Idle WiFi AP (beacons only) | Sometimes | Beacons are brief, may miss them |
| BLE advertising | Yes (on ch 2,26,80) | Regular advertising pulses |
| Bluetooth Classic | Sometimes | Frequency-hopping, very brief per channel |
| Microwave oven | Yes | Strong broadband interference |
| Zigbee | Yes (on specific channels) | Continuous during transmission |

---

## 7. Safety: This Is Legal

This scan is **purely passive** — the NRF24 is in RX mode at all times:
- CE goes high to enter RX, low to enter standby — no transmission occurs
- RF_PWR setting is irrelevant (only matters for TX)
- We never call `W_TX_PAYLOAD` or pulse CE in TX mode
- Analogous to using a radio receiver or spectrum analyzer

---

## 8. Advanced: Logging to Monitor Protocol Activity

```c
// Detect which protocols are active
void analyze_spectrum(void) {
    int wifi_ch1 = 0, wifi_ch6 = 0, wifi_ch11 = 0;
    int ble_adv = 0;

    for (int ch = 1; ch <= 14; ch++) wifi_ch1 += channel_hits[ch];
    for (int ch = 21; ch <= 34; ch++) wifi_ch6 += channel_hits[ch];
    for (int ch = 51; ch <= 64; ch++) wifi_ch11 += channel_hits[ch];
    
    ble_adv = channel_hits[2] + channel_hits[26] + channel_hits[80];

    printf("WiFi Ch1: %d | Ch6: %d | Ch11: %d\n", wifi_ch1, wifi_ch6, wifi_ch11);
    printf("BLE advertising activity: %d\n", ble_adv);
}
```

---

## 9. Verification

**Success criteria:**
- Channels near your WiFi router show high hit counts
- Channels 2, 26, 80 show BLE advertising activity (from your phone or smartwatch)
- Empty channels show 0 or very low hit counts
- The total scan completes in a few seconds

---

## References

- [NRF24L01+ Product Specification v1.0](https://infocenter.nordicsemi.com/pdf/nRF24L01P_PS_v1.0.pdf) — Section 6.4: Received Power Detector
- NRF24L01 datasheet: `docs/datasheets/az087_c_20.pdf`
- [ESP-IDF SPI Master Driver](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/spi_master.html) *(verified 2026-06-04)*
