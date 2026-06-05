#pragma once

#include <cstdint>
#include <cstdio>

namespace nrf24 {

/* ═══════════════════════════════════════════════════════════════════════════
 * nRF24L01+ — STATUS register (address 0x07)
 *
 * Register bit layout (reset value: 0x0E = 0b 0000 1110):
 *
 *   bit 7    bit 6   bit 5   bit 4    bits 3:1   bit 0
 *   (rsvd)   RX_DR   TX_DS   MAX_RT   RX_P_NO    TX_FULL
 *
 * Interrupt flags (bits 6:4) are cleared by writing 1 to the bit.
 * RX_P_NO: 000–101 = pipe number, 111 = RX FIFO empty.
 * Reset: RX_P_NO=111 (0x0E).
 * ═══════════════════════════════════════════════════════════════════════════ */


/**
 * @brief RX data pipe number from STATUS bits 3:1.
 *
 * @code
 *   RX_P_NO   Meaning
 *   000–101   Data pipe number with payload available
 *   110       Not used
 *   111       RX FIFO empty (reset default)
 * @endcode
 */
enum class RxPipeNo : uint8_t {
    Pipe0  = 0,
    Pipe1  = 1,
    Pipe2  = 2,
    Pipe3  = 3,
    Pipe4  = 4,
    Pipe5  = 5,
    NotUsed = 6,
    Empty  = 7, ///< RX FIFO empty (reset default)
};

/**
 * @brief Typed representation of the nRF24L01+ STATUS register (address 0x07).
 *
 * This register is also returned as the first byte of every SPI transaction.
 *
 * @code
 *   nrf24::Status st = nrf24::Status::from_byte(spi_status);
 *   if (st.rx_dr) { // new data available }
 *   if (st.max_rt) { // retransmit limit reached }
 * @endcode
 *
 * To clear interrupt flags, write 1 to the corresponding bit:
 * @code
 *   nrf24_write_reg(nrf24::Status::ADDRESS, 0x70); // clear all IRQ flags
 * @endcode
 */
struct Status {
    static constexpr uint8_t ADDRESS     = 0x07;
    static constexpr uint8_t RESET_VALUE = 0x0E;

    bool     rx_dr    = false;          ///< Data Ready RX FIFO interrupt — bit 6
    bool     tx_ds    = false;          ///< Data Sent TX FIFO interrupt — bit 5
    bool     max_rt   = false;          ///< Max retransmits interrupt — bit 4
    RxPipeNo rx_p_no  = RxPipeNo::Empty; ///< Pipe# for available RX data — bits 3:1
    bool     tx_full  = false;          ///< TX FIFO full flag — bit 0

    /**
     * @brief Serialise into the raw byte for STATUS.
     *
     * Useful for building a write value to clear interrupt flags.
     *
     * @return Raw byte ready to write to register 0x07.
     */
    constexpr uint8_t to_byte() const {
        return (static_cast<uint8_t>(rx_dr)   << 6)
             | (static_cast<uint8_t>(tx_ds)   << 5)
             | (static_cast<uint8_t>(max_rt)  << 4)
             | (static_cast<uint8_t>(rx_p_no) << 1)
             | (static_cast<uint8_t>(tx_full));
    }

    /**
     * @brief Reconstruct from a raw byte (read from register or SPI status).
     *
     * @param byte  Raw value read from register 0x07.
     * @return      Populated Status matching the byte's fields.
     */
    static constexpr Status from_byte(uint8_t byte) {
        Status s;
        s.rx_dr   = (byte >> 6) & 0x01;
        s.tx_ds   = (byte >> 5) & 0x01;
        s.max_rt  = (byte >> 4) & 0x01;
        s.rx_p_no = static_cast<RxPipeNo>((byte >> 1) & 0x07);
        s.tx_full = byte & 0x01;
        return s;
    }

    /**
     * @brief Format all STATUS fields as a human-readable "key: value" string.
     *
     * @code
     *   char buf[96];
     *   nrf24::Status st = nrf24::Status::from_byte(0x4E);
     *   st.format(buf, sizeof(buf));
     *   printf("%s\n", buf);
     *   // "rx_dr: 1, tx_ds: 0, max_rt: 0, rx_p_no: 7 (empty), tx_full: 0"
     * @endcode
     *
     * @param buf  Destination buffer (recommend >= 80 bytes).
     * @param len  Size of buf in bytes.
     * @return     Number of characters written (excluding null terminator).
     */
    int format(char *buf, size_t len) const {
        const char *pipe_str;
        switch (rx_p_no) {
            case RxPipeNo::Pipe0:   pipe_str = "0"; break;
            case RxPipeNo::Pipe1:   pipe_str = "1"; break;
            case RxPipeNo::Pipe2:   pipe_str = "2"; break;
            case RxPipeNo::Pipe3:   pipe_str = "3"; break;
            case RxPipeNo::Pipe4:   pipe_str = "4"; break;
            case RxPipeNo::Pipe5:   pipe_str = "5"; break;
            case RxPipeNo::NotUsed: pipe_str = "6 (unused)"; break;
            case RxPipeNo::Empty:   pipe_str = "7 (empty)"; break;
            default:                pipe_str = "?"; break;
        }
        return snprintf(buf, len,
                        "rx_dr: %u, tx_ds: %u, max_rt: %u, "
                        "rx_p_no: %s, tx_full: %u",
                        rx_dr, tx_ds, max_rt, pipe_str, tx_full);
    }
};

} // namespace nrf24
