#pragma once

#include <cstdint>
#include <cstdio>

namespace nrf24 {

/* ═══════════════════════════════════════════════════════════════════════════
 * nRF24L01+ — FIFO_STATUS register (address 0x17) — mostly READ ONLY
 *
 * Register bit layout (reset value: 0x11 = 0b 0001 0001):
 *
 *   bit 7    bit 6      bit 5    bit 4      bits 3:2   bit 1    bit 0
 *   (rsvd)   TX_REUSE   TX_FULL  TX_EMPTY   (rsvd)     RX_FULL  RX_EMPTY
 *
 * TX_REUSE: Set when REUSE_TX_PL command issued. Cleared by W_TX_PAYLOAD
 *           or FLUSH_TX. Read only.
 * Reset: TX_EMPTY=1, RX_EMPTY=1, all else 0.
 * ═══════════════════════════════════════════════════════════════════════════ */


/**
 * @brief Typed representation of the nRF24L01+ FIFO_STATUS register (address 0x17).
 *
 * Read-only status of TX and RX FIFOs.
 *
 * @code
 *   nrf24::FifoStatus fs = nrf24::FifoStatus::from_byte(
 *       nrf24_read_reg(nrf24::FifoStatus::ADDRESS));
 *   if (fs.rx_empty) { // no data available }
 *   if (fs.tx_full)  { // TX FIFO full, cannot write }
 * @endcode
 */
struct FifoStatus {
    static constexpr uint8_t ADDRESS     = 0x17;
    static constexpr uint8_t RESET_VALUE = 0x11;

    bool tx_reuse = false; ///< Last TX payload reused (REUSE_TX_PL active) — bit 6
    bool tx_full  = false; ///< TX FIFO full — bit 5
    bool tx_empty = true;  ///< TX FIFO empty — bit 4 (reset: 1)
    bool rx_full  = false; ///< RX FIFO full — bit 1
    bool rx_empty = true;  ///< RX FIFO empty — bit 0 (reset: 1)

    /**
     * @brief Serialise into raw byte (for test/symmetry).
     *
     * @return Raw byte representation.
     */
    constexpr uint8_t to_byte() const {
        return (static_cast<uint8_t>(tx_reuse) << 6)
             | (static_cast<uint8_t>(tx_full)  << 5)
             | (static_cast<uint8_t>(tx_empty) << 4)
             | (static_cast<uint8_t>(rx_full)  << 1)
             | (static_cast<uint8_t>(rx_empty));
    }

    /**
     * @brief Reconstruct from a raw byte read from the device.
     *
     * @param byte  Raw value read from register 0x17.
     * @return      Populated FifoStatus matching the byte's fields.
     */
    static constexpr FifoStatus from_byte(uint8_t byte) {
        FifoStatus f;
        f.tx_reuse = (byte >> 6) & 0x01;
        f.tx_full  = (byte >> 5) & 0x01;
        f.tx_empty = (byte >> 4) & 0x01;
        f.rx_full  = (byte >> 1) & 0x01;
        f.rx_empty = byte & 0x01;
        return f;
    }

    /**
     * @brief Format all FIFO_STATUS fields as a human-readable string.
     *
     * @code
     *   char buf[80];
     *   nrf24::FifoStatus fs = nrf24::FifoStatus::from_byte(0x11);
     *   fs.format(buf, sizeof(buf));
     *   // "tx_reuse: 0, tx_full: 0, tx_empty: 1, rx_full: 0, rx_empty: 1"
     * @endcode
     *
     * @param buf  Destination buffer (recommend >= 80 bytes).
     * @param len  Size of buf in bytes.
     * @return     Number of characters written (excluding null terminator).
     */
    int format(char *buf, size_t len) const {
        return snprintf(buf, len, "tx_reuse: %u, tx_full: %u, tx_empty: %u, rx_full: %u, rx_empty: %u",
                        static_cast<unsigned>(tx_reuse), static_cast<unsigned>(tx_full),
                        static_cast<unsigned>(tx_empty), static_cast<unsigned>(rx_full),
                        static_cast<unsigned>(rx_empty));
    }
};

} // namespace nrf24
