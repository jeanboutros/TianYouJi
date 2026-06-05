#pragma once

#include <cstdint>

namespace nrf24 {

/* ═══════════════════════════════════════════════════════════════════════════
 * nRF24L01+ — SETUP_RETR register (address 0x04)
 *
 * Register bit layout (reset value: 0x03 = 0b 0000 0011):
 *
 *   bits 7:4     bits 3:0
 *   ARD          ARC
 *
 * ARD: Auto Retransmit Delay = (ARD + 1) × 250 µs
 *   0000 =  250 µs
 *   0001 =  500 µs
 *   ...
 *   1111 = 4000 µs
 *
 * ARC: Auto Retransmit Count
 *   0000 = retransmit disabled
 *   0001 = up to 1 retransmit
 *   ...
 *   1111 = up to 15 retransmits
 *
 * Reset: ARD=0 (250 µs), ARC=3 (up to 3 retransmits).
 * ═══════════════════════════════════════════════════════════════════════════ */


/**
 * @brief Auto Retransmit Delay options for SETUP_RETR bits 7:4.
 *
 * Delay between retransmit attempts = (enum_value + 1) × 250 µs.
 *
 * @code
 *   ARD value   Delay
 *   0           250 µs
 *   1           500 µs
 *   5          1500 µs (reset default)
 *   15         4000 µs
 * @endcode
 */
enum class AutoRetransmitDelay : uint8_t {
    Us250  = 0,  ///<  250 µs (reset default)
    Us500  = 1,  ///<  500 µs
    Us750  = 2,  ///<  750 µs
    Us1000 = 3,  ///< 1000 µs
    Us1250 = 4,  ///< 1250 µs
    Us1500 = 5,  ///< 1500 µs
    Us1750 = 6,  ///< 1750 µs
    Us2000 = 7,  ///< 2000 µs
    Us2250 = 8,  ///< 2250 µs
    Us2500 = 9,  ///< 2500 µs
    Us2750 = 10, ///< 2750 µs
    Us3000 = 11, ///< 3000 µs
    Us3250 = 12, ///< 3250 µs
    Us3500 = 13, ///< 3500 µs
    Us3750 = 14, ///< 3750 µs
    Us4000 = 15, ///< 4000 µs
};

/**
 * @brief Return the SETUP_RETR register contribution of an @ref AutoRetransmitDelay.
 *
 * Places ARD at bits 7:4.
 *
 * @code
 *   to_reg(AutoRetransmitDelay::Us250)  → 0x00
 *   to_reg(AutoRetransmitDelay::Us4000) → 0xF0
 * @endcode
 *
 * @param v  Selected delay.
 * @return   Byte with ARD at bits 7:4; all other bits zero.
 */
constexpr uint8_t to_reg(AutoRetransmitDelay v) {
    return static_cast<uint8_t>(v) << 4;
}

/**
 * @brief Auto Retransmit Count options for SETUP_RETR bits 3:0.
 *
 * @code
 *   ARC value   Retransmits
 *   0           Disabled
 *   1           Up to 1
 *   ...
 *   15          Up to 15
 * @endcode
 */
enum class AutoRetransmitCount : uint8_t {
    Disabled = 0,  ///< Retransmit disabled
    Count1   = 1,  ///< Up to 1 retransmit
    Count2   = 2,  ///< Up to 2 retransmits
    Count3   = 3,  ///< Up to 3 retransmits (reset default)
    Count4   = 4,  ///< Up to 4 retransmits
    Count5   = 5,  ///< Up to 5 retransmits
    Count6   = 6,  ///< Up to 6 retransmits
    Count7   = 7,  ///< Up to 7 retransmits
    Count8   = 8,  ///< Up to 8 retransmits
    Count9   = 9,  ///< Up to 9 retransmits
    Count10  = 10, ///< Up to 10 retransmits
    Count11  = 11, ///< Up to 11 retransmits
    Count12  = 12, ///< Up to 12 retransmits
    Count13  = 13, ///< Up to 13 retransmits
    Count14  = 14, ///< Up to 14 retransmits
    Count15  = 15, ///< Up to 15 retransmits
};

/**
 * @brief Return the SETUP_RETR register contribution of an @ref AutoRetransmitCount.
 *
 * Places ARC at bits 3:0.
 *
 * @code
 *   to_reg(AutoRetransmitCount::Disabled) → 0x00
 *   to_reg(AutoRetransmitCount::Count15)  → 0x0F
 * @endcode
 *
 * @param v  Selected retransmit count.
 * @return   Byte with ARC at bits 3:0; all other bits zero.
 */
constexpr uint8_t to_reg(AutoRetransmitCount v) {
    return static_cast<uint8_t>(v);
}

/**
 * @brief Typed representation of the nRF24L01+ SETUP_RETR register (address 0x04).
 *
 * @code
 *   nrf24::SetupRetr cfg;
 *   cfg.delay = nrf24::AutoRetransmitDelay::Us1500;
 *   cfg.count = nrf24::AutoRetransmitCount::Count15;
 *   nrf24_write_reg(nrf24::SetupRetr::ADDRESS, cfg.to_byte()); // → 0x5F
 * @endcode
 *
 * Default: 250 µs delay, 3 retransmits (0x03).
 */
struct SetupRetr {
    static constexpr uint8_t ADDRESS     = 0x04;
    static constexpr uint8_t RESET_VALUE = 0x03;

    AutoRetransmitDelay delay = AutoRetransmitDelay::Us250;  ///< ARD bits 7:4 — reset: 250 µs
    AutoRetransmitCount count = AutoRetransmitCount::Count3; ///< ARC bits 3:0 — reset: 3

    /**
     * @brief Serialise into the raw byte for SETUP_RETR.
     *
     * @return Raw byte ready to write to register 0x04.
     */
    constexpr uint8_t to_byte() const {
        return to_reg(delay) | to_reg(count);
    }

    /**
     * @brief Reconstruct from a raw byte read from the device.
     *
     * @param byte  Raw value read from register 0x04.
     * @return      Populated SetupRetr matching the byte's fields.
     */
    static constexpr SetupRetr from_byte(uint8_t byte) {
        SetupRetr s;
        s.delay = static_cast<AutoRetransmitDelay>((byte >> 4) & 0x0F);
        s.count = static_cast<AutoRetransmitCount>(byte & 0x0F);
        return s;
    }
};

} // namespace nrf24
