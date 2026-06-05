#pragma once

#include <cstdint>

namespace nrf24 {

/* ═══════════════════════════════════════════════════════════════════════════
 * nRF24L01+ — FEATURE register (address 0x1D)
 *
 * Register bit layout (reset value: 0x00):
 *
 *   bits 7:3   bit 2      bit 1        bit 0
 *   (rsvd)     EN_DPL     EN_ACK_PAY   EN_DYN_ACK
 *
 * EN_DPL:     Enables Dynamic Payload Length
 * EN_ACK_PAY: Enables Payload with ACK
 * EN_DYN_ACK: Enables the W_TX_PAYLOAD_NOACK command
 *
 * Reset: all disabled.
 *
 * Note: On some clones, FEATURE must be activated via the ACTIVATE command
 * (0x50 + 0x73) before it can be written.
 * ═══════════════════════════════════════════════════════════════════════════ */


/**
 * @brief Dynamic Payload Length enable for FEATURE bit 2.
 */
enum class DplMode : uint8_t {
    Disabled = 0, ///< Dynamic payload length disabled (reset default)
    Enabled  = 1, ///< Dynamic payload length enabled
};

/**
 * @brief Return the FEATURE register contribution of a @ref DplMode value.
 *
 * Places EN_DPL at bit 2.
 *
 * @param v  Selected DPL mode.
 * @return   Byte with EN_DPL at bit 2; all other bits zero.
 */
constexpr uint8_t to_reg(DplMode v) {
    return static_cast<uint8_t>(v) << 2;
}

/**
 * @brief ACK Payload enable for FEATURE bit 1.
 */
enum class AckPayMode : uint8_t {
    Disabled = 0, ///< Payload with ACK disabled (reset default)
    Enabled  = 1, ///< Payload with ACK enabled
};

/**
 * @brief Return the FEATURE register contribution of an @ref AckPayMode value.
 *
 * Places EN_ACK_PAY at bit 1.
 *
 * @param v  Selected ACK payload mode.
 * @return   Byte with EN_ACK_PAY at bit 1; all other bits zero.
 */
constexpr uint8_t to_reg(AckPayMode v) {
    return static_cast<uint8_t>(v) << 1;
}

/**
 * @brief Dynamic ACK enable for FEATURE bit 0.
 *
 * When enabled, allows W_TX_PAYLOAD_NOACK command to send a packet
 * without requesting an ACK from the receiver.
 */
enum class DynAckMode : uint8_t {
    Disabled = 0, ///< W_TX_PAYLOAD_NOACK disabled (reset default)
    Enabled  = 1, ///< W_TX_PAYLOAD_NOACK enabled
};

/**
 * @brief Return the FEATURE register contribution of a @ref DynAckMode value.
 *
 * Places EN_DYN_ACK at bit 0.
 *
 * @param v  Selected dynamic ACK mode.
 * @return   Byte with EN_DYN_ACK at bit 0; all other bits zero.
 */
constexpr uint8_t to_reg(DynAckMode v) {
    return static_cast<uint8_t>(v);
}

/**
 * @brief Typed representation of the nRF24L01+ FEATURE register (address 0x1D).
 *
 * @code
 *   nrf24::Feature cfg;
 *   cfg.en_dpl     = nrf24::DplMode::Enabled;
 *   cfg.en_ack_pay = nrf24::AckPayMode::Enabled;
 *   cfg.en_dyn_ack = nrf24::DynAckMode::Enabled;
 *   nrf24_write_reg(nrf24::Feature::ADDRESS, cfg.to_byte()); // → 0x07
 * @endcode
 *
 * Default: all features disabled (0x00).
 */
struct Feature {
    static constexpr uint8_t ADDRESS     = 0x1D;
    static constexpr uint8_t RESET_VALUE = 0x00;

    DplMode    en_dpl     = DplMode::Disabled;    ///< Enable Dynamic Payload Length — bit 2
    AckPayMode en_ack_pay = AckPayMode::Disabled; ///< Enable Payload with ACK — bit 1
    DynAckMode en_dyn_ack = DynAckMode::Disabled; ///< Enable W_TX_PAYLOAD_NOACK — bit 0

    /**
     * @brief Serialise into the raw byte for FEATURE.
     *
     * @return Raw byte ready to write to register 0x1D.
     */
    constexpr uint8_t to_byte() const {
        return to_reg(en_dpl) | to_reg(en_ack_pay) | to_reg(en_dyn_ack);
    }

    /**
     * @brief Reconstruct from a raw byte read from the device.
     *
     * @param byte  Raw value read from register 0x1D.
     * @return      Populated Feature matching the byte's fields.
     */
    static constexpr Feature from_byte(uint8_t byte) {
        Feature f;
        f.en_dpl     = static_cast<DplMode>((byte >> 2) & 0x01);
        f.en_ack_pay = static_cast<AckPayMode>((byte >> 1) & 0x01);
        f.en_dyn_ack = static_cast<DynAckMode>(byte & 0x01);
        return f;
    }
};

} // namespace nrf24
