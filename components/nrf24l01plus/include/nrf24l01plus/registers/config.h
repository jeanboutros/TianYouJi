#pragma once

#include <cstdint>

namespace nrf24 {

/* ═══════════════════════════════════════════════════════════════════════════
 * nRF24L01+ — CONFIG register (address 0x00)
 *
 * Register bit layout (reset value: 0x08 = 0b 0000 1000):
 *
 *   bit 7    bit 6       bit 5       bit 4        bit 3    bit 2  bit 1   bit 0
 *   (rsvd)   MASK_RX_DR  MASK_TX_DS  MASK_MAX_RT  EN_CRC   CRCO   PWR_UP  PRIM_RX
 *
 * Reset state: EN_CRC=1 (CRC enabled), all else 0.
 * EN_CRC is forced high if any pipe has auto-acknowledgment enabled (EN_AA).
 * ═══════════════════════════════════════════════════════════════════════════ */


/**
 * @brief IRQ mask options for CONFIG bits 6:4.
 *
 * Each interrupt source can be independently masked (prevented from
 * driving the IRQ pin low).
 *
 * @code
 *   Bit   Field        Meaning when 1
 *   6     MASK_RX_DR   RX_DR interrupt NOT reflected on IRQ pin
 *   5     MASK_TX_DS   TX_DS interrupt NOT reflected on IRQ pin
 *   4     MASK_MAX_RT  MAX_RT interrupt NOT reflected on IRQ pin
 * @endcode
 */
enum class IrqMask : uint8_t {
    None   = 0b000, ///< All interrupts reflected on IRQ pin (reset default)
    RxDr   = 0b100, ///< Mask RX_DR  — bit 6
    TxDs   = 0b010, ///< Mask TX_DS  — bit 5
    MaxRt  = 0b001, ///< Mask MAX_RT — bit 4
    All    = 0b111, ///< Mask all interrupts
};

/**
 * @brief Bitwise OR for combining IrqMask flags.
 */
constexpr IrqMask operator|(IrqMask a, IrqMask b) {
    return static_cast<IrqMask>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

/**
 * @brief Bitwise AND for testing IrqMask flags.
 */
constexpr IrqMask operator&(IrqMask a, IrqMask b) {
    return static_cast<IrqMask>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}

/**
 * @brief Return the CONFIG register contribution of an @ref IrqMask value.
 *
 * Places the 3-bit mask field into bits 6:4.
 *
 * @code
 *   to_reg(IrqMask::None) → 0x00
 *   to_reg(IrqMask::All)  → 0x70
 *   to_reg(IrqMask::RxDr | IrqMask::TxDs) → 0x60
 * @endcode
 *
 * @param v  Selected interrupt mask combination.
 * @return   Byte with MASK bits at 6:4; all other bits zero.
 */
constexpr uint8_t to_reg(IrqMask v) {
    return static_cast<uint8_t>(v) << 4;
}

/**
 * @brief CRC encoding scheme for CONFIG bit 2.
 *
 * @code
 *   CRCO   Scheme
 *   0      1 byte  CRC (reset default)
 *   1      2 bytes CRC
 * @endcode
 */
enum class CrcEncoding : uint8_t {
    Bytes1 = 0, ///< 1-byte CRC (reset default)
    Bytes2 = 1, ///< 2-byte CRC
};

/**
 * @brief Return the CONFIG register contribution of a @ref CrcEncoding value.
 *
 * Places CRCO at bit 2.
 *
 * @code
 *   to_reg(CrcEncoding::Bytes1) → 0x00
 *   to_reg(CrcEncoding::Bytes2) → 0x04
 * @endcode
 *
 * @param v  Selected CRC encoding.
 * @return   Byte with CRCO at bit 2; all other bits zero.
 */
constexpr uint8_t to_reg(CrcEncoding v) {
    return static_cast<uint8_t>(v) << 2;
}

/**
 * @brief CRC enable for CONFIG bit 3.
 */
enum class CrcMode : uint8_t {
    Disabled = 0, ///< CRC disabled (only if EN_AA is all 0)
    Enabled  = 1, ///< CRC enabled (reset default)
};

/**
 * @brief Return the CONFIG register contribution of a @ref CrcMode value.
 *
 * Places EN_CRC at bit 3.
 *
 * @code
 *   to_reg(CrcMode::Disabled) → 0x00
 *   to_reg(CrcMode::Enabled)  → 0x08
 * @endcode
 *
 * @param v  Selected CRC mode.
 * @return   Byte with EN_CRC at bit 3; all other bits zero.
 */
constexpr uint8_t to_reg(CrcMode v) {
    return static_cast<uint8_t>(v) << 3;
}

/**
 * @brief Power state for CONFIG bit 1.
 */
enum class PowerMode : uint8_t {
    Down = 0, ///< Power down (reset default)
    Up   = 1, ///< Power up
};

/**
 * @brief Return the CONFIG register contribution of a @ref PowerMode value.
 *
 * Places PWR_UP at bit 1.
 *
 * @code
 *   to_reg(PowerMode::Down) → 0x00
 *   to_reg(PowerMode::Up)   → 0x02
 * @endcode
 *
 * @param v  Selected power mode.
 * @return   Byte with PWR_UP at bit 1; all other bits zero.
 */
constexpr uint8_t to_reg(PowerMode v) {
    return static_cast<uint8_t>(v) << 1;
}

/**
 * @brief Primary mode (RX/TX) for CONFIG bit 0.
 */
enum class PrimaryMode : uint8_t {
    TX = 0, ///< Transmitter (reset default)
    RX = 1, ///< Receiver
};

/**
 * @brief Return the CONFIG register contribution of a @ref PrimaryMode value.
 *
 * Places PRIM_RX at bit 0.
 *
 * @code
 *   to_reg(PrimaryMode::TX) → 0x00
 *   to_reg(PrimaryMode::RX) → 0x01
 * @endcode
 *
 * @param v  Selected primary mode.
 * @return   Byte with PRIM_RX at bit 0; all other bits zero.
 */
constexpr uint8_t to_reg(PrimaryMode v) {
    return static_cast<uint8_t>(v);
}

/**
 * @brief Typed representation of the nRF24L01+ CONFIG register (address 0x00).
 *
 * @code
 *   nrf24::Config cfg;
 *   cfg.power_mode  = nrf24::PowerMode::Up;
 *   cfg.primary     = nrf24::PrimaryMode::RX;
 *   cfg.crc_mode    = nrf24::CrcMode::Enabled;
 *   cfg.crc_encoding = nrf24::CrcEncoding::Bytes2;
 *   nrf24_write_reg(nrf24::Config::ADDRESS, cfg.to_byte());
 * @endcode
 *
 * Default field values match power-on reset state (0x08):
 * CRC enabled, 1-byte CRC, power down, TX mode, no IRQ masks.
 */
struct Config {
    static constexpr uint8_t ADDRESS     = 0x00;
    static constexpr uint8_t RESET_VALUE = 0x08;

    IrqMask     irq_mask     = IrqMask::None;        ///< IRQ mask bits 6:4 — reset: none masked
    CrcMode     crc_mode     = CrcMode::Enabled;     ///< EN_CRC bit 3 — reset: enabled
    CrcEncoding crc_encoding = CrcEncoding::Bytes1;  ///< CRCO bit 2 — reset: 1-byte
    PowerMode   power_mode   = PowerMode::Down;      ///< PWR_UP bit 1 — reset: power down
    PrimaryMode primary      = PrimaryMode::TX;      ///< PRIM_RX bit 0 — reset: TX

    /**
     * @brief Serialise this configuration into the raw byte for CONFIG.
     *
     * @return Raw byte ready to write to register 0x00.
     */
    constexpr uint8_t to_byte() const {
        return to_reg(irq_mask)
             | to_reg(crc_mode)
             | to_reg(crc_encoding)
             | to_reg(power_mode)
             | to_reg(primary);
    }

    /**
     * @brief Reconstruct a Config from a raw byte read from the device.
     *
     * @param byte  Raw value read from register 0x00.
     * @return      Populated Config matching the byte's fields.
     */
    static constexpr Config from_byte(uint8_t byte) {
        Config c;
        c.irq_mask     = static_cast<IrqMask>((byte >> 4) & 0x07);
        c.crc_mode     = static_cast<CrcMode>((byte >> 3) & 0x01);
        c.crc_encoding = static_cast<CrcEncoding>((byte >> 2) & 0x01);
        c.power_mode   = static_cast<PowerMode>((byte >> 1) & 0x01);
        c.primary      = static_cast<PrimaryMode>(byte & 0x01);
        return c;
    }
};

} // namespace nrf24
