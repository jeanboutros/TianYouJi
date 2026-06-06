#pragma once

#include <cstdint>
#include <cstdio>

namespace nrf24 {

/* ═══════════════════════════════════════════════════════════════════════════
 * nRF24L01+ — DYNPD register (address 0x1C)
 *
 * Register bit layout (reset value: 0x00):
 *
 *   bits 7:6   bit 5    bit 4    bit 3    bit 2    bit 1    bit 0
 *   (rsvd)     DPL_P5   DPL_P4   DPL_P3   DPL_P2   DPL_P1   DPL_P0
 *
 * Each bit enables dynamic payload length on that pipe.
 * Requires EN_DPL in FEATURE register to be set.
 * Requires ENAA_Px in EN_AA register for the corresponding pipe.
 * Reset: all disabled.
 * ═══════════════════════════════════════════════════════════════════════════ */


/**
 * @brief Typed representation of the nRF24L01+ DYNPD register (address 0x1C).
 *
 * Controls per-pipe dynamic payload length.
 *
 * @code
 *   nrf24::Dynpd cfg;
 *   cfg.pipe[0] = true;  // enable DPL on pipe 0
 *   cfg.pipe[1] = true;  // enable DPL on pipe 1
 *   nrf24_write_reg(nrf24::Dynpd::ADDRESS, cfg.to_byte());
 * @endcode
 *
 * Default: all pipes disabled (0x00).
 */
struct Dynpd {
    static constexpr uint8_t ADDRESS     = 0x1C;
    static constexpr uint8_t RESET_VALUE = 0x00;

    bool pipe[6] = {false, false, false, false, false, false}; ///< Per-pipe DPL enable (bits 5:0)

    /**
     * @brief Serialise into the raw byte for DYNPD.
     *
     * @return Raw byte ready to write to register 0x1C.
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
     * @param byte  Raw value read from register 0x1C.
     * @return      Populated Dynpd matching the byte's fields.
     */
    static constexpr Dynpd from_byte(uint8_t byte) {
        Dynpd d{};
        for (int i = 0; i < 6; ++i) {
            d.pipe[i] = (byte >> i) & 0x01;
        }
        return d;
    }

    /**
     * @brief Format all DYNPD fields as a human-readable string.
     *
     * @code
     *   char buf[80];
     *   nrf24::Dynpd d = nrf24::Dynpd::from_byte(0x03);
     *   d.format(buf, sizeof(buf));
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
