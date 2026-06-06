#pragma once

#include <cstdint>
#include <cstdio>

namespace nrf24 {

/* ═══════════════════════════════════════════════════════════════════════════
 * nRF24L01+ — RF_CH register (address 0x05)
 *
 * Register bit layout (reset value: 0x02 = 0b 0000 0010):
 *
 *   bit 7     bits 6:0
 *   (rsvd)    RF_CH
 *
 * Frequency = 2400 + RF_CH [MHz].
 * Valid range: 0–125 (2400–2525 MHz).
 * Reset: channel 2 (2402 MHz).
 * ═══════════════════════════════════════════════════════════════════════════ */


/**
 * @brief Typed representation of the nRF24L01+ RF_CH register (address 0x05).
 *
 * @code
 *   nrf24::RfCh cfg;
 *   cfg.channel = 76;  // 2476 MHz
 *   nrf24_write_reg(nrf24::RfCh::ADDRESS, cfg.to_byte());
 * @endcode
 *
 * Default: channel 2 (2402 MHz).
 */
struct RfCh {
    static constexpr uint8_t ADDRESS     = 0x05;
    static constexpr uint8_t RESET_VALUE = 0x02;

    uint8_t channel = 2; ///< RF channel 0–125 (frequency = 2400 + channel MHz) — reset: 2

    /**
     * @brief Serialise into the raw byte for RF_CH.
     *
     * @return Raw byte ready to write to register 0x05 (bits 6:0 only).
     */
    constexpr uint8_t to_byte() const {
        return channel & 0x7F;
    }

    /**
     * @brief Reconstruct from a raw byte read from the device.
     *
     * @param byte  Raw value read from register 0x05.
     * @return      Populated RfCh matching the byte's fields.
     */
    static constexpr RfCh from_byte(uint8_t byte) {
        RfCh r;
        r.channel = byte & 0x7F;
        return r;
    }

    /**
     * @brief Format all RF_CH fields as a human-readable string.
     *
     * @code
     *   char buf[80];
     *   nrf24::RfCh cfg = nrf24::RfCh::from_byte(0x4C);
     *   cfg.format(buf, sizeof(buf));
     *   // "channel: 76, freq: 2476 MHz"
     * @endcode
     *
     * @param buf  Destination buffer (recommend >= 80 bytes).
     * @param len  Size of buf in bytes.
     * @return     Number of characters written (excluding null terminator).
     */
    int format(char *buf, size_t len) const {
        return snprintf(buf, len, "channel: %u, freq: %u MHz",
                        static_cast<unsigned>(channel),
                        static_cast<unsigned>(2400 + channel));
    }
};

} // namespace nrf24
