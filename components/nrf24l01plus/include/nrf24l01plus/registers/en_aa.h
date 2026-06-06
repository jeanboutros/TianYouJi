#pragma once

#include <cstdint>
#include <cstdio>

namespace nrf24 {

/* ═══════════════════════════════════════════════════════════════════════════
 * nRF24L01+ — EN_AA register (address 0x01)
 *
 * Register bit layout (reset value: 0x3F = 0b 0011 1111):
 *
 *   bits 7:6   bit 5    bit 4    bit 3    bit 2    bit 1    bit 0
 *   (rsvd)     ENAA_P5  ENAA_P4  ENAA_P3  ENAA_P2  ENAA_P1  ENAA_P0
 *
 * Each bit enables Enhanced ShockBurst™ auto-acknowledgment on that pipe.
 * Reset: all pipes enabled (bits 5:0 = 1).
 * ═══════════════════════════════════════════════════════════════════════════ */


/**
 * @brief Typed representation of the nRF24L01+ EN_AA register (address 0x01).
 *
 * Controls per-pipe auto-acknowledgment (Enhanced ShockBurst).
 *
 * @code
 *   nrf24::EnAa cfg;
 *   cfg.pipe[0] = true;   // enable auto-ack on pipe 0
 *   cfg.pipe[1] = true;   // enable auto-ack on pipe 1
 *   cfg.pipe[2] = false;  // disable auto-ack on pipe 2
 *   nrf24_write_reg(nrf24::EnAa::ADDRESS, cfg.to_byte());
 * @endcode
 *
 * Default values match power-on reset state (0x3F): all pipes enabled.
 */
struct EnAa {
    static constexpr uint8_t ADDRESS     = 0x01;
    static constexpr uint8_t RESET_VALUE = 0x3F;

    bool pipe[6] = {true, true, true, true, true, true}; ///< Per-pipe auto-ack enable (bits 5:0)

    /**
     * @brief Serialise into the raw byte for EN_AA.
     *
     * @return Raw byte ready to write to register 0x01.
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
     * @param byte  Raw value read from register 0x01.
     * @return      Populated EnAa matching the byte's fields.
     */
    static constexpr EnAa from_byte(uint8_t byte) {
        EnAa aa{};
        for (int i = 0; i < 6; ++i) {
            aa.pipe[i] = (byte >> i) & 0x01;
        }
        return aa;
    }

    /**
     * @brief Format all EN_AA fields as a human-readable string.
     *
     * @code
     *   char buf[80];
     *   nrf24::EnAa aa = nrf24::EnAa::from_byte(0x3F);
     *   aa.format(buf, sizeof(buf));
     *   // "pipe[0..5]: 1/1/1/1/1/1"
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
