#include "nrf24l01plus/ble.h"

namespace nrf24 {
namespace ble {

void dewhiten(uint8_t *data, uint8_t len, uint8_t channel_idx)
{
    uint8_t lfsr = (channel_idx & 0x3F) | 0x40;
    for (uint8_t i = 0; i < len; i++) {
        uint8_t w = 0;
        for (int b = 0; b < 8; b++) {
            if (lfsr & 0x01) {
                w |= (1 << b);
                lfsr ^= 0x48; /* feedback: x^7(bit6) and x^4(bit3) after shift */
            }
            lfsr >>= 1;
        }
        data[i] ^= w;
    }
}

} // namespace ble
} // namespace nrf24
