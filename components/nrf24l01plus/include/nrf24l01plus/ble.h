#pragma once

#include <cstdint>

namespace nrf24 {
namespace ble {

/**
 * @brief Apply BLE data whitening / de-whitening in-place.
 *
 * BLE whitens PDU + CRC with a 7-bit LFSR (polynomial x^7 + x^4 + 1,
 * Bluetooth Core Spec Vol 6 Part B §3.2).  The operation is its own
 * inverse (XOR-based), so the same function whitens and de-whitens.
 *
 * LFSR initialisation: bit 6 = 1, bits [5:0] = channel_index[5:0].
 *
 * @code
 *   // De-whiten a packet received on BLE advertising channel 37:
 *   nrf24::ble::dewhiten(buf, 32, 37);
 * @endcode
 *
 * @param data         Pointer to the data buffer (modified in-place).
 * @param len          Number of bytes to process.
 * @param channel_idx  BLE channel index (0–39).  Advertising: 37, 38, 39.
 */
void dewhiten(uint8_t *data, uint8_t len, uint8_t channel_idx);

} // namespace ble
} // namespace nrf24
