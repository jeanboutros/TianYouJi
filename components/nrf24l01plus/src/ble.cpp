#include "nrf24l01plus/ble.h"

#include <cstdio>

namespace nrf24 {
namespace ble {

/* Galois LFSR seed bit: bit 1 forced high per Bluetooth Core Spec Vol 6 Part B §3.2.
 * Same as the `| 2` in Dmitry Grinberg's btLeWhiten — places channel index
 * at bits [6:0] with bit 1 always set. */
static constexpr uint8_t GALOIS_LFSR_SEED_BIT = 0x02;

/* Galois LFSR polynomial mask for x^7 + x^4 + 1.
 * When bit 7 is set after shift, XOR feedback taps at bits 4 and 0. */
static constexpr uint8_t GALOIS_LFSR_POLY = 0x11;

void dewhiten(uint8_t *data, uint8_t len, uint8_t channel_idx)
{
    /* Step 1: bit-swap each byte (nRF24L01+ MSbit-first → BLE LSbit-first) */
    for (uint8_t i = 0; i < len; i++)
        data[i] = swapbits(data[i]);

    /* Step 2: Galois LFSR dewhitening (Dmitry Grinberg's algorithm).
     * Polynomial x^7+x^4+1 — see GALOIS_LFSR_POLY.
     * Seed: swapbits(channel_idx) | GALOIS_LFSR_SEED_BIT. */
    uint8_t lfsr = swapbits(channel_idx) | GALOIS_LFSR_SEED_BIT;
    for (uint8_t i = 0; i < len; i++) {
        for (uint8_t m = 1; m; m <<= 1) {
            if (lfsr & 0x80) {
                lfsr ^= GALOIS_LFSR_POLY;
                data[i] ^= m;
            }
            lfsr <<= 1;
        }
    }
}

/* Spec-defined PDU type names indexed by BleAdvPduType value.
 * Per Bluetooth Core Spec Vol 6 Part B §2.3 Table 2.2.
 * Only indices 0–7 are valid; callers handle 8–15 as UNKNOWN. */
static const char *PDU_TYPE_NAMES[] = {
    "ADV_IND",        // 0
    "ADV_DIRECT_IND", // 1
    "ADV_NONCONN_IND",// 2
    "SCAN_REQ",       // 3
    "SCAN_RSP",       // 4
    "CONNECT_IND",    // 5
    "ADV_SCAN_IND",   // 6
    "ADV_EXT_IND",    // 7 — BLE 5.0+ extended advertising
};

/* Static buffer for sprintf'd UNKNOWN type names.
 * Worst case: "UNKNOWN(type=15)" = 19 chars + NUL = 20 bytes.
 * Not thread-safe — acceptable for single-threaded sniffer output. */
static char unknown_buf[20];

const char *pdu_type_name(BleAdvPduType t)
{
    auto val = static_cast<uint8_t>(t);
    if (val <= 7) return PDU_TYPE_NAMES[val];
    std::snprintf(unknown_buf, sizeof(unknown_buf),
                  "UNKNOWN(type=%u)", static_cast<unsigned>(val));
    return unknown_buf;
}

} // namespace ble
} // namespace nrf24
