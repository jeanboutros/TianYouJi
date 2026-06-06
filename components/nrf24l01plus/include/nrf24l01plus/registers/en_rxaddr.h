#pragma once

#include <cstdint>
#include <cstdio>

namespace nrf24 {

/* ═══════════════════════════════════════════════════════════════════════════
 * nRF24L01+ — EN_RXADDR register (address 0x02)
 *
 * Register bit layout (reset value: 0x03 = 0b 0000 0011):
 *
 *   bits 7:6   bit 5    bit 4    bit 3    bit 2    bit 1    bit 0
 *   (rsvd)     ERX_P5   ERX_P4   ERX_P3   ERX_P2   ERX_P1   ERX_P0
 *
 * Each bit enables the corresponding RX data pipe.
 * Reset: pipes 0 and 1 enabled.
 * ═══════════════════════════════════════════════════════════════════════════ */


/**
 * @brief Typed representation of the nRF24L01+ EN_RXADDR register (address 0x02).
 *
 * Controls which data pipes are enabled for receiving.
 *
 * @code
 *   nrf24::EnRxAddr cfg;
 *   cfg.pipe[0] = true;   // enable pipe 0
 *   cfg.pipe[1] = true;   // enable pipe 1
 *   cfg.pipe[2] = true;   // also enable pipe 2
 *   nrf24_write_reg(nrf24::EnRxAddr::ADDRESS, cfg.to_byte());
 * @endcode
 *
 * Default values match power-on reset state (0x03): pipes 0,1 enabled.
 */
struct EnRxAddr {
    static constexpr uint8_t ADDRESS     = 0x02;
    static constexpr uint8_t RESET_VALUE = 0x03;

    bool pipe[6] = {true, true, false, false, false, false}; ///< Per-pipe RX enable (bits 5:0)

    /**
     * @brief Serialise into the raw byte for EN_RXADDR.
     *
     * @return Raw byte ready to write to register 0x02.
     */
    constexpr uint8_t to_byte() const {
        uint8_t v = 0;
        for (int i = 0; i < 6; ++i) {
            if (pipe[i]) v |= (1u << i);
        }
        return v;
    }

    /**
     * @brief Reconstruct from a raw byte read from the device.
     *
     * @param byte  Raw value read from register 0x02.
     * @return      Populated EnRxAddr matching the byte's fields.
     */
    static constexpr EnRxAddr from_byte(uint8_t byte) {
        EnRxAddr rx{};
        for (int i = 0; i < 6; ++i) {
            rx.pipe[i] = (byte >> i) & 0x01;
        }
        return rx;
    }

    /**
     * @brief Format all EN_RXADDR fields as a human-readable string.
     *
     * @code
     *   char buf[80];
     *   nrf24::EnRxAddr rx = nrf24::EnRxAddr::from_byte(0x03);
     *   rx.format(buf, sizeof(buf));
     *   // "pipe[0..5]: 1/1/0/0/0/0"
     * @endcode
     *
     * @param buf  Destination buffer (recommend >= 80 bytes).
     * @param len  Size of buf in bytes.
     * @return     Number of characters written (excluding null terminator).
     */
    int format(char *buf, size_t len) const {
        return snprintf(buf, len, "pipe[0..5]: %d/%d/%d/%d/%d/%d",
                        pipe[0], pipe[1], pipe[2],
                        pipe[3], pipe[4], pipe[5]);
    }
};

} // namespace nrf24
