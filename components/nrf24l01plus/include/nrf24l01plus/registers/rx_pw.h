#pragma once

#include <cstdint>

namespace nrf24 {

/* ═══════════════════════════════════════════════════════════════════════════
 * nRF24L01+ — RX_PW_P0 through RX_PW_P5 registers (addresses 0x11–0x16)
 *
 * Register bit layout (reset value: 0x00 for all pipes):
 *
 *   bits 7:6       bits 5:0
 *   (reserved)     RX_PW_Px
 *
 * RX_PW_Px[5:0] sets the number of bytes expected in the RX payload for
 * pipe x:
 *   0     = pipe not used
 *   1–32  = number of payload bytes
 *
 * Bits 7:6 are reserved and read as 0.
 * ═══════════════════════════════════════════════════════════════════════════ */


/**
 * @brief Base struct modelling any RX_PW_Px register of the nRF24L01+.
 *
 * All six RX payload width registers (0x11–0x16) share the same 6-bit
 * field layout. Per-pipe structs inherit from this and define their
 * specific ADDRESS constant.
 *
 * @code
 *   nrf24::RxPwP0 pw;
 *   pw.payload_width = 16;  // expect 16-byte payloads on pipe 0
 *   nrf24_write_reg(nrf24::RxPwP0::ADDRESS, pw.to_byte());
 *
 *   // Read back:
 *   auto actual = nrf24::RxPwP0::from_byte(
 *       nrf24_read_reg(nrf24::RxPwP0::ADDRESS));
 *   // actual.payload_width == 16
 * @endcode
 *
 * A value of 0 means the pipe is not used. Valid range: 0–32.
 */
struct RxPw {
    static constexpr uint8_t RESET_VALUE = 0x00; ///< Power-on reset: pipe not used

    uint8_t payload_width = 0; ///< Number of bytes in RX payload (bits 5:0, valid 0–32)

    /**
     * @brief Serialise into the raw byte for an RX_PW_Px register.
     *
     * Masks to 6 bits (bits 5:0). Values above 32 are silently clamped
     * by the mask to the lower 6 bits.
     *
     * @code
     *   RxPw{0}.to_byte()   → 0x00  (pipe not used)
     *   RxPw{16}.to_byte()  → 0x10  (16 bytes)
     *   RxPw{32}.to_byte()  → 0x20  (maximum payload)
     * @endcode
     *
     * @return Raw byte ready to write to the register.
     */
    constexpr uint8_t to_byte() const {
        return payload_width & 0x3F;
    }

    /**
     * @brief Reconstruct an RxPw from a raw byte read back from the device.
     *
     * @code
     *   auto pw = RxPw::from_byte(0x20);
     *   // pw.payload_width == 32
     * @endcode
     *
     * @param byte  Raw value read from an RX_PW_Px register.
     * @return      Populated RxPw with payload_width extracted from bits 5:0.
     */
    static constexpr RxPw from_byte(uint8_t byte) {
        return RxPw{static_cast<uint8_t>(byte & 0x3F)};
    }
};

/**
 * @brief RX payload width register for pipe 0 (address 0x11).
 */
struct RxPwP0 : RxPw {
    static constexpr uint8_t ADDRESS = 0x11;
};

/**
 * @brief RX payload width register for pipe 1 (address 0x12).
 */
struct RxPwP1 : RxPw {
    static constexpr uint8_t ADDRESS = 0x12;
};

/**
 * @brief RX payload width register for pipe 2 (address 0x13).
 */
struct RxPwP2 : RxPw {
    static constexpr uint8_t ADDRESS = 0x13;
};

/**
 * @brief RX payload width register for pipe 3 (address 0x14).
 */
struct RxPwP3 : RxPw {
    static constexpr uint8_t ADDRESS = 0x14;
};

/**
 * @brief RX payload width register for pipe 4 (address 0x15).
 */
struct RxPwP4 : RxPw {
    static constexpr uint8_t ADDRESS = 0x15;
};

/**
 * @brief RX payload width register for pipe 5 (address 0x16).
 */
struct RxPwP5 : RxPw {
    static constexpr uint8_t ADDRESS = 0x16;
};

} // namespace nrf24
