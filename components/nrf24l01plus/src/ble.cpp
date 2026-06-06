#include "nrf24l01plus/ble.h"

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

} // namespace ble
} // namespace nrf24
