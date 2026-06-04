# RF Legal Boundaries: What's Illegal and How to Avoid It

## Overview

This document analyzes two open-source projects — **RF-Clown** and **BWifiKill ESP32 V4.0** — to explain line-by-line what makes certain code illegal, and how legal code can accidentally drift into illegal territory.

> **This is an educational document.** Understanding how attacks work is essential to defending against them and avoiding accidental violations. None of the illegal patterns should be implemented.

---

## Part 1: Laws That Apply

### International: ITU Radio Regulations
The ISM (Industrial, Scientific, Medical) band at 2.4–2.4835 GHz is license-free **for low-power devices that do not cause harmful interference**.

### United States: FCC Part 15
- Max EIRP: 1 watt (30 dBm) for frequency-hopping spread spectrum
- **Section 15.5(b)**: "Operation of an intentional radiator [...] shall not cause harmful interference"
- **Section 15.5(c)**: Devices must accept interference from other sources
- Penalty: Up to $100,000 per violation + criminal prosecution

### European Union: ETSI EN 300 328 + Radio Equipment Directive 2014/53/EU
- Max EIRP: 100 mW (20 dBm) for wideband data transmission
- Duty cycle limits apply depending on sub-band
- Equipment must be CE marked

### Key definitions
| Term | Meaning |
|---|---|
| **Intentional radiator** | A device that intentionally generates RF energy (your NRF24 transmitting) |
| **Harmful interference** | Any emission that degrades, obstructs, or interrupts authorized radio communications |
| **Jamming** | Intentionally causing harmful interference — always illegal without government authorization |

---

## Part 2: Line-by-Line Analysis of Illegal Code

### Example A: NRF24 Constant Carrier Jammer (RF-Clown)

Source: `RfClown/RfClown.ino` + `RfClown/config.h`

```cpp
// From config.h — target channels for BLE jamming
const byte ble_channels[] = {2, 26, 80};
```

**Explanation:** BLE uses 3 advertising channels:
- Channel 37 → 2402 MHz → NRF24 channel **2**
- Channel 38 → 2426 MHz → NRF24 channel **26**
- Channel 39 → 2480 MHz → NRF24 channel **80**

```cpp
// From RfClown.ino — the jamming initialization
void configure_Radio(RF24 &radio, const byte *channels, size_t size) {
  configureNrf(radio);
  radio.printPrettyDetails();
  for (size_t i = 0; i < size; i++) {
    radio.setChannel(channels[i]);         // [1] Set to a BLE advertising channel
    radio.startConstCarrier(RF24_PA_MAX, channels[i]); // [2] ← THIS IS THE ILLEGAL LINE
  }
}
```

**Line-by-line breakdown:**

| Line | What it does | Why it's illegal |
|---|---|---|
| `radio.setChannel(channels[i])` | Tunes the NRF24 to a specific frequency | Alone: legal (you can tune to any ISM channel) |
| `radio.startConstCarrier(RF24_PA_MAX, channels[i])` | **Emits a continuous, unmodulated carrier wave at maximum power** | This is the definition of a jammer — it creates a constant signal that drowns out all BLE advertisements on that channel. No data is being communicated. |
| `RF24_PA_MAX` | Sets transmit power to maximum (+0 dBm for NRF24, up to +20 dBm with PA module) | At max power, the interference radius is maximized |
| Loop through all 3 channels with 3 radios | Covers ALL BLE advertising channels simultaneously | Makes it impossible for ANY BLE device to advertise or discover in range |

**Why this is a jammer, not communication:**
- `startConstCarrier()` sends a **continuous unmodulated RF signal** — no preamble, no address, no payload
- It never stops transmitting
- It's specifically targeting frequencies used by other people's devices
- The intent is to prevent communication, not to communicate

---

### Example B: Channel-Hopping Jammer Loop (RF-Clown main loop)

```cpp
// From RfClown.ino — the main loop
void loop() {
  // ... menu handling ...
  
  if (current_Mode == BLE_MODULE) {
    int randomIndex = random(0, sizeof(ble_channels) / sizeof(ble_channels[0]));
    byte channel = ble_channels[randomIndex];  // [1] Pick random BLE channel
    RadioA.setChannel(channel);                // [2] All 3 radios hop to same channel
    RadioB.setChannel(channel);
    RadioC.setChannel(channel);
  } else if (current_Mode == Bluetooth_MODULE) {
    int randomIndex = random(0, sizeof(bluetooth_channels) / sizeof(bluetooth_channels[0]));
    byte channel = bluetooth_channels[randomIndex];
    RadioA.setChannel(channel);
    RadioB.setChannel(channel);
    RadioC.setChannel(channel);
  }
  // ... more modes for WiFi, USB wireless, Zigbee, etc.
}
```

**What this does:**
1. The `loop()` runs continuously without any `delay()` — thousands of times per second
2. Each iteration picks a random channel from the target set
3. All 3 NRF24 modules (already in constant-carrier mode from `configure_Radio()`) hop to that channel
4. The effect: random noise sweeping across all target channels at high speed

**Why this is illegal:**
- The radios are already in `startConstCarrier` mode (set during init)
- The loop hops them across channels = **spread-spectrum jamming**
- There is NO communication happening — just interference generation
- Targets OTHER PEOPLE'S devices (Bluetooth headphones, medical devices, keyboards)

**The channel arrays from config.h show targeting:**

```cpp
const byte bluetooth_channels[] = {32, 34, 46, 48, 50, 52, 0, 1, 2, 4, 6, 8, 22, 24, 26, 28, 30, 74, 76, 78, 80};
const byte ble_channels[]       = {2, 26, 80};
const byte WiFi_channels[]      = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
const byte zigbee_channels[]    = {11, 15, 20, 25};
```

These correspond to the known frequency allocations of each protocol. The code is designed to specifically target and disrupt each one.

---

### Example C: BWifiKill "BLE Spam" and "Beacon Spam"

From BWifiKill's feature list:
- **BLE Spam (POP)**: Generates thousands of fake BLE advertisement packets per second
- **Beacon Spam**: Generates fake WiFi SSIDs flooding nearby scanners

**How BLE Spam works conceptually (typical implementation pattern):**

```cpp
// ILLEGAL PATTERN — DO NOT IMPLEMENT
while (1) {
    // Generate random MAC address
    uint8_t mac[6] = {random(), random(), random(), random(), random(), random()};
    
    // Set new random name
    esp_ble_gap_set_device_name("FakeDevice_XXXX");
    
    // Start advertising immediately, no interval compliance
    esp_ble_gap_start_advertising(&adv_params);
    
    // Stop and restart with new identity — hundreds of times per second
    delay(1);  // 1ms between advertisements = flooding
    esp_ble_gap_stop_advertising();
}
```

**Why this crosses into illegal:**
- Normal BLE: 1 device advertises every 20ms–10.24s (BLE spec minimum is 20ms)
- Spam: generates hundreds of UNIQUE fake devices per second
- Result: nearby phones' Bluetooth scan fills with garbage, legitimate devices can't be discovered
- This is a **denial of service attack** on the BLE advertisement channel

---

### Example D: WiFi Deauthentication (BWifiKill "Deauther")

**Conceptual pattern (DO NOT IMPLEMENT):**

```cpp
// ILLEGAL — DO NOT IMPLEMENT
// Construct a Type 0 (Management), Subtype 12 (Deauthentication) frame
uint8_t deauth_frame[] = {
    0xC0, 0x00,                         // Frame Control: Deauthentication
    0x00, 0x00,                         // Duration
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // Destination: broadcast
    0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, // Source: spoofed AP MAC
    0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, // BSSID: spoofed AP MAC
    0x00, 0x00,                         // Sequence number
    0x07, 0x00                          // Reason code: Class 3 frame received
};

// Send repeatedly to kick all clients off a network
esp_wifi_80211_tx(WIFI_IF_STA, deauth_frame, sizeof(deauth_frame), false);
```

**Why this is illegal:**
- Sends forged management frames pretending to be an access point
- Forces OTHER PEOPLE'S devices to disconnect from THEIR network
- Even on your own network, it could affect neighbors on the same channel
- Violates FCC rules (Section 333 of Communications Act: willful interference)

---

## Part 3: How Legal Code Drifts Into Illegal Territory

| Legal pattern | Accidental drift | How to prevent |
|---|---|---|
| **BLE advertising (1 name, proper interval)** | `while(1)` loop without delay → floods channel | Always use `adv_int_min >= 0x0020` (20ms). Use ESP-IDF's built-in advertising state machine instead of manual start/stop loops |
| **NRF24 TX to your own receiver** | Forgot to set destination address → broadcasts to all | Always configure a specific 5-byte RX address. Verify with `radio.getPayloadSize()` on receiver |
| **Spectrum scan (RX only)** | Accidentally left TX enabled after switching from TX mode | Always call `radio.stopListening()` then `radio.powerDown()` when done. Check `radio.getMode()` before exiting |
| **Point-to-point data link** | Retry loop with no backoff on failure → continuous TX | Implement max retry count (`radio.setRetries(delay, count)` with count ≤ 15). Add exponential backoff in application layer |
| **Testing at maximum power** | Left `RF24_PA_MAX` in production code | Default to `RF24_PA_MIN` (-18 dBm). Only increase for range testing between YOUR devices |
| **NRF24 as BLE advertiser (educational)** | Sending advertisements in a tight loop | Send ONE packet, wait at least 100ms before next. Never exceed BLE spec minimum interval (20ms) |
| **Channel scanning for analysis** | Setting the radio to TX mode on a channel you're "scanning" | Only use `radio.startListening()` (RX mode) for scanning. Never call `radio.write()` or `startConstCarrier()` during a scan |
| **Cycling through device names** | Changing MAC + name every millisecond = BLE spam | Change at most once per advertising cycle (≥ 1 second recommended). Keep the same MAC for at least 15 minutes (RPA timeout in BLE spec) |

### The critical boundary: INTENT + EFFECT

The legal line is:
1. **Are you communicating data to YOUR OWN device?** → Legal
2. **Are you transmitting to disrupt OTHER people's devices?** → Illegal
3. **Are you transmitting continuously without purpose?** → Illegal (even accidentally)

---

## Part 4: Safety Checklist for Every RF Project

Before flashing any code that uses NRF24 TX or BLE advertising:

- [ ] TX power is set to minimum needed (start with `RF24_PA_MIN`)
- [ ] Proper delay between transmissions (≥ 20ms for BLE, ≥ 100ms recommended for NRF24 educational use)
- [ ] TX is disabled when not actively sending (`radio.powerDown()` or `radio.stopListening()`)
- [ ] Only transmitting to YOUR OWN receivers (known 5-byte address)
- [ ] Not targeting channels specifically to disrupt others
- [ ] Code has automatic TX timeout (e.g., stop after N packets or T seconds)
- [ ] `startConstCarrier()` is NEVER used (this function has no legitimate communication purpose)
- [ ] Testing at minimum power in a controlled environment
- [ ] No tight `while(1)` loop around `radio.write()` without delay

---

## Part 5: Summary — What We Will Build (LEGAL)

| What | Why it's legal |
|---|---|
| Read NRF24 registers via SPI | No RF transmission at all |
| Send data between YOUR OWN two NRF24 modules | Private point-to-point communication, within ISM band rules |
| Scan 2.4 GHz spectrum using carrier detect (RX only) | Passive reception, no transmission |
| Broadcast ONE BLE advertisement with a custom name | Normal BLE device behaviour, proper interval |
| Craft a BLE-format packet on NRF24 and send to YOUR OWN scanner | Educational single-packet experiment, not flooding |

---

## References

- [ESP-IDF BLE GAP API](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/bluetooth/esp_gap_ble.html) *(verified 2026-06-04)*
- [RF-Clown source code (RfClown.ino)](https://github.com/cifertech/RF-Clown/blob/main/RfClown/RfClown.ino) *(verified 2026-06-04)*
- [RF-Clown config.h (channel definitions)](https://github.com/cifertech/RF-Clown/blob/main/RfClown/config.h) *(verified 2026-06-04)*
- [BWifiKill ESP32 V4.0 README (feature list)](https://github.com/pepeangell5/BWifiKill-ESP32-V4.0) *(verified 2026-06-04)*
- [FCC Part 15 — Radio Frequency Devices](https://www.ecfr.gov/current/title-47/chapter-I/subchapter-A/part-15) *(official US regulation)*
- [ETSI EN 300 328 — Wideband data transmission systems](https://www.etsi.org/deliver/etsi_en/300300_300399/300328/) *(EU standard for 2.4 GHz ISM)*
