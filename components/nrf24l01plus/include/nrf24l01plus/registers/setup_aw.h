#pragma once

#include <cstdint>

namespace nrf24 {

/* ═══════════════════════════════════════════════════════════════════════════
 * nRF24L01+ — SETUP_AW register (address 0x03)
 *
 * Register bit layout (reset value: 0x03 = 0b 0000 0011):
 *
 *   bits 7:2   bits 1:0
 *   (rsvd)     AW
 *
 * AW encoding:
 *   00 = Illegal
 *   01 = 3 bytes
 *   10 = 4 bytes
 *   11 = 5 bytes (reset default)
 * ═══════════════════════════════════════════════════════════════════════════ */


/**
 * @brief Address width options for the SETUP_AW register.
 *
 * @code
 *   AW[1:0]   Width
 *   00        Illegal
 *   01        3 bytes
 *   10        4 bytes
 *   11        5 bytes (reset default)
 * @endcode
 */
enum class AddressWidth : uint8_t {
    Bytes3 = 0b01, ///< 3-byte address (24-bit)
    Bytes4 = 0b10, ///< 4-byte address (32-bit)
    Bytes5 = 0b11, ///< 5-byte address (40-bit) — reset default
};

/**
 * @brief Return the SETUP_AW register contribution of an @ref AddressWidth value.
 *
 * Places AW at bits 1:0.
 *
 * @code
 *   to_reg(AddressWidth::Bytes3) → 0x01
 *   to_reg(AddressWidth::Bytes4) → 0x02
 *   to_reg(AddressWidth::Bytes5) → 0x03
 * @endcode
 *
 * @param v  Selected address width.
 * @return   Byte with AW at bits 1:0; all other bits zero.
 */
constexpr uint8_t to_reg(AddressWidth v) {
    return static_cast<uint8_t>(v);
}

/**
 * @brief Typed representation of the nRF24L01+ SETUP_AW register (address 0x03).
 *
 * @code
 *   nrf24::SetupAw cfg;
 *   cfg.address_width = nrf24::AddressWidth::Bytes3;  // use 3-byte addresses
 *   nrf24_write_reg(nrf24::SetupAw::ADDRESS, cfg.to_byte());
 * @endcode
 *
 * Default: 5-byte addresses (0x03).
 */
struct SetupAw {
    static constexpr uint8_t ADDRESS     = 0x03;
    static constexpr uint8_t RESET_VALUE = 0x03;

    AddressWidth address_width = AddressWidth::Bytes5; ///< Address field width — reset: 5 bytes

    /**
     * @brief Serialise into the raw byte for SETUP_AW.
     *
     * @return Raw byte ready to write to register 0x03.
     */
    constexpr uint8_t to_byte() const {
        return to_reg(address_width);
    }

    /**
     * @brief Reconstruct from a raw byte read from the device.
     *
     * @param byte  Raw value read from register 0x03.
     * @return      Populated SetupAw matching the byte's fields.
     */
    static constexpr SetupAw from_byte(uint8_t byte) {
        SetupAw s;
        s.address_width = static_cast<AddressWidth>(byte & 0x03);
        return s;
    }
};

} // namespace nrf24
