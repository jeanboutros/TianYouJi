#pragma once

#include <cstdint>

namespace nrf24 {

/* ═══════════════════════════════════════════════════════════════════════════
 * nRF24L01+ — OBSERVE_TX register (address 0x08) — READ ONLY
 *
 * Register bit layout (reset value: 0x00):
 *
 *   bits 7:4    bits 3:0
 *   PLOS_CNT    ARC_CNT
 *
 * PLOS_CNT: Count lost packets. Saturates at 15. Reset by writing RF_CH.
 * ARC_CNT:  Count retransmitted packets for current transmission.
 *           Resets on new transmission.
 * ═══════════════════════════════════════════════════════════════════════════ */


/**
 * @brief Typed representation of the nRF24L01+ OBSERVE_TX register (address 0x08).
 *
 * Read-only register. Provides packet loss and retransmit statistics.
 *
 * @code
 *   nrf24::ObserveTx obs = nrf24::ObserveTx::from_byte(
 *       nrf24_read_reg(nrf24::ObserveTx::ADDRESS));
 *   if (obs.plos_cnt == 15) { // significant packet loss }
 * @endcode
 */
struct ObserveTx {
    static constexpr uint8_t ADDRESS     = 0x08;
    static constexpr uint8_t RESET_VALUE = 0x00;

    uint8_t plos_cnt = 0; ///< Packets lost counter (0–15, saturates) — bits 7:4
    uint8_t arc_cnt  = 0; ///< Retransmit counter for current TX (0–15) — bits 3:0

    /**
     * @brief Serialise into raw byte (for test/symmetry only; register is read-only).
     *
     * @return Raw byte representation.
     */
    constexpr uint8_t to_byte() const {
        return ((plos_cnt & 0x0F) << 4) | (arc_cnt & 0x0F);
    }

    /**
     * @brief Reconstruct from a raw byte read from the device.
     *
     * @param byte  Raw value read from register 0x08.
     * @return      Populated ObserveTx matching the byte's fields.
     */
    static constexpr ObserveTx from_byte(uint8_t byte) {
        ObserveTx o;
        o.plos_cnt = (byte >> 4) & 0x0F;
        o.arc_cnt  = byte & 0x0F;
        return o;
    }
};

} // namespace nrf24
