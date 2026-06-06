#pragma once

#include <cstdint>
#include <cstdio>

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

    /**
     * @brief Format all SETUP_RETR fields as a human-readable string.
     *
     * @code
     *   char buf[80];
     *   nrf24::SetupRetr sr = nrf24::SetupRetr::from_byte(0x93);
     *   sr.format(buf, sizeof(buf));
     *   // "delay: 2500 us, count: 3"
     * @endcode
     *
     * @param buf  Destination buffer (recommend >= 80 bytes).
     * @param len  Size of buf in bytes.
     * @return     Number of characters written (excluding null terminator).
     */
    int format(char *buf, size_t len) const {
        const char *delay_str;
        switch (delay) {
            case AutoRetransmitDelay::Us250:  delay_str = "250 us";  break;
            case AutoRetransmitDelay::Us500:  delay_str = "500 us";  break;
            case AutoRetransmitDelay::Us750:  delay_str = "750 us";  break;
            case AutoRetransmitDelay::Us1000: delay_str = "1000 us"; break;
            case AutoRetransmitDelay::Us1250: delay_str = "1250 us"; break;
            case AutoRetransmitDelay::Us1500: delay_str = "1500 us"; break;
            case AutoRetransmitDelay::Us1750: delay_str = "1750 us"; break;
            case AutoRetransmitDelay::Us2000: delay_str = "2000 us"; break;
            case AutoRetransmitDelay::Us2250: delay_str = "2250 us"; break;
            case AutoRetransmitDelay::Us2500: delay_str = "2500 us"; break;
            case AutoRetransmitDelay::Us2750: delay_str = "2750 us"; break;
            case AutoRetransmitDelay::Us3000: delay_str = "3000 us"; break;
            case AutoRetransmitDelay::Us3250: delay_str = "3250 us"; break;
            case AutoRetransmitDelay::Us3500: delay_str = "3500 us"; break;
            case AutoRetransmitDelay::Us3750: delay_str = "3750 us"; break;
            case AutoRetransmitDelay::Us4000: delay_str = "4000 us"; break;
            default:                           delay_str = "unknown"; break;
        }
        const char *count_str;
        switch (count) {
            case AutoRetransmitCount::Disabled: count_str = "disabled"; break;
            case AutoRetransmitCount::Count1:   count_str = "1";       break;
            case AutoRetransmitCount::Count2:   count_str = "2";       break;
            case AutoRetransmitCount::Count3:   count_str = "3";       break;
            case AutoRetransmitCount::Count4:   count_str = "4";       break;
            case AutoRetransmitCount::Count5:   count_str = "5";       break;
            case AutoRetransmitCount::Count6:   count_str = "6";       break;
            case AutoRetransmitCount::Count7:   count_str = "7";       break;
            case AutoRetransmitCount::Count8:   count_str = "8";       break;
            case AutoRetransmitCount::Count9:   count_str = "9";       break;
            case AutoRetransmitCount::Count10:  count_str = "10";      break;
            case AutoRetransmitCount::Count11:  count_str = "11";      break;
            case AutoRetransmitCount::Count12:  count_str = "12";      break;
            case AutoRetransmitCount::Count13:  count_str = "13";      break;
            case AutoRetransmitCount::Count14:  count_str = "14";      break;
            case AutoRetransmitCount::Count15:  count_str = "15";      break;
            default:                            count_str = "unknown";  break;
        }
        return snprintf(buf, len, "delay: %s, count: %s", delay_str, count_str);
    }
};

} // namespace nrf24
