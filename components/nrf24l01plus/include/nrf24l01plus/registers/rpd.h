#pragma once

#include <cstdint>

namespace nrf24 {

/* ═══════════════════════════════════════════════════════════════════════════
 * nRF24L01+ — RPD register (address 0x09) — READ ONLY
 *
 * Register bit layout (reset value: 0x00):
 *
 *   bits 7:1    bit 0
 *   (rsvd)      RPD
 *
 * RPD (Received Power Detector):
 *   1 = Received power level above -64 dBm
 *   0 = Below threshold or no carrier detected
 *
 * nRF24L01+ only (replaces CD on non-plus variant).
 * Latched high when signal > -64 dBm detected during receive.
 * Reset when entering RX mode.
 * ═══════════════════════════════════════════════════════════════════════════ */


/**
 * @brief Typed representation of the nRF24L01+ RPD register (address 0x09).
 *
 * Read-only. Indicates whether a signal above -64 dBm was detected.
 *
 * @code
 *   nrf24::Rpd rpd = nrf24::Rpd::from_byte(
 *       nrf24_read_reg(nrf24::Rpd::ADDRESS));
 *   if (rpd.received_power) { // strong signal present }
 * @endcode
 */
struct Rpd {
    static constexpr uint8_t ADDRESS     = 0x09;
    static constexpr uint8_t RESET_VALUE = 0x00;

    bool received_power = false; ///< 1 = signal > -64 dBm detected — bit 0

    /**
     * @brief Serialise into raw byte (for test/symmetry only; register is read-only).
     *
     * @return Raw byte representation.
     */
    constexpr uint8_t to_byte() const {
        return static_cast<uint8_t>(received_power);
    }

    /**
     * @brief Reconstruct from a raw byte read from the device.
     *
     * @param byte  Raw value read from register 0x09.
     * @return      Populated Rpd matching the byte's fields.
     */
    static constexpr Rpd from_byte(uint8_t byte) {
        Rpd r;
        r.received_power = byte & 0x01;
        return r;
    }
};

} // namespace nrf24
