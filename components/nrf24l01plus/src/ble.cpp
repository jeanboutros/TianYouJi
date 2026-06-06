#include "nrf24l01plus/ble.h"

namespace nrf24 {
namespace ble {

void dewhiten(uint8_t *data, uint8_t len, uint8_t channel_idx)
{
    /* Step 1: bit-swap each byte (nRF24L01+ MSbit-first → BLE LSbit-first) */
    for (uint8_t i = 0; i < len; i++)
        data[i] = swapbits(data[i]);

    /* Step 2: Galois LFSR dewhitening (Dmitry Grinberg's algorithm).
     * Polynomial x^7+x^4+1 encoded as 0x11 in the 7-bit state.
     * Seed: swapbits(channel_idx) | 2. */
    uint8_t lfsr = swapbits(channel_idx) | 0x02;
    for (uint8_t i = 0; i < len; i++) {
        for (uint8_t m = 1; m; m <<= 1) {
            if (lfsr & 0x80) {
                lfsr ^= 0x11;
                data[i] ^= m;
            }
            lfsr <<= 1;
        }
    }
}

} // namespace ble
} // namespace nrf24
