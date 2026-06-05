#pragma once

/**
 * @file addresses.h
 * @brief nRF24L01+ register address constants.
 *
 * Provides a flat vocabulary for driver-level code:
 * @code
 *   nrf24_write_reg(nrf24::reg::RF_SETUP, rf.to_byte());
 *   uint8_t st = nrf24_read_reg(nrf24::reg::STATUS);
 * @endcode
 *
 * All addresses come from the nRF24L01+ Product Specification v1.0,
 * Table 28 — Register Map.
 */

#include <cstdint>

namespace nrf24 {
namespace reg {

inline constexpr uint8_t CONFIG      = 0x00; ///< Configuration Register
inline constexpr uint8_t EN_AA       = 0x01; ///< Enable Auto Acknowledgment
inline constexpr uint8_t EN_RXADDR   = 0x02; ///< Enabled RX Addresses
inline constexpr uint8_t SETUP_AW    = 0x03; ///< Setup of Address Widths
inline constexpr uint8_t SETUP_RETR  = 0x04; ///< Setup of Automatic Retransmission
inline constexpr uint8_t RF_CH       = 0x05; ///< RF Channel
inline constexpr uint8_t RF_SETUP    = 0x06; ///< RF Setup Register
inline constexpr uint8_t STATUS      = 0x07; ///< Status Register
inline constexpr uint8_t OBSERVE_TX  = 0x08; ///< Transmit observe register
inline constexpr uint8_t RPD         = 0x09; ///< Received Power Detector
inline constexpr uint8_t RX_ADDR_P0  = 0x0A; ///< Receive address data pipe 0
inline constexpr uint8_t RX_ADDR_P1  = 0x0B; ///< Receive address data pipe 1
inline constexpr uint8_t RX_ADDR_P2  = 0x0C; ///< Receive address data pipe 2
inline constexpr uint8_t RX_ADDR_P3  = 0x0D; ///< Receive address data pipe 3
inline constexpr uint8_t RX_ADDR_P4  = 0x0E; ///< Receive address data pipe 4
inline constexpr uint8_t RX_ADDR_P5  = 0x0F; ///< Receive address data pipe 5
inline constexpr uint8_t TX_ADDR     = 0x10; ///< Transmit address
inline constexpr uint8_t RX_PW_P0    = 0x11; ///< Number of bytes in RX payload pipe 0
inline constexpr uint8_t RX_PW_P1    = 0x12; ///< Number of bytes in RX payload pipe 1
inline constexpr uint8_t RX_PW_P2    = 0x13; ///< Number of bytes in RX payload pipe 2
inline constexpr uint8_t RX_PW_P3    = 0x14; ///< Number of bytes in RX payload pipe 3
inline constexpr uint8_t RX_PW_P4    = 0x15; ///< Number of bytes in RX payload pipe 4
inline constexpr uint8_t RX_PW_P5    = 0x16; ///< Number of bytes in RX payload pipe 5
inline constexpr uint8_t FIFO_STATUS = 0x17; ///< FIFO Status Register
inline constexpr uint8_t DYNPD       = 0x1C; ///< Enable dynamic payload length
inline constexpr uint8_t FEATURE     = 0x1D; ///< Feature Register

} // namespace reg
} // namespace nrf24
