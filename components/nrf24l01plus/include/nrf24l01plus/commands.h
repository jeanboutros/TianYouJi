#pragma once

#include <cstdint>

namespace nrf24 {

/**
 * @brief nRF24L01+ SPI command set (Product Specification Table 8).
 *
 * Commands fall into two categories:
 * - **Fixed commands** — single opcode, no embedded address.
 * - **Variable commands** — a base opcode OR'd with a register address
 *   or pipe number in the lower bits.
 *
 * For variable commands, use the helper functions below (e.g.
 * `cmd_r_register(addr)`) to compose the final command byte.
 *
 * @code
 *   // Fixed command:
 *   hal.spi_xfer(cmd::FLUSH_RX, nullptr, nullptr, 0);
 *
 *   // Variable command (read STATUS register):
 *   hal.spi_xfer(nrf24::cmd_r_register(nrf24::reg::STATUS), nullptr, &val, 1);
 * @endcode
 */
namespace cmd {

/* ── Fixed commands ─────────────────────────────────────────────────── */

/** @brief Read RX payload (1–32 bytes).  Payload is removed from FIFO. */
constexpr uint8_t R_RX_PAYLOAD = 0x61;

/** @brief Write TX payload (1–32 bytes).  Used in TX mode. */
constexpr uint8_t W_TX_PAYLOAD = 0xA0;

/** @brief Flush TX FIFO.  Used in TX mode. */
constexpr uint8_t FLUSH_TX = 0xE1;

/** @brief Flush RX FIFO.  Used in RX mode.  Undefined in TX mode. */
constexpr uint8_t FLUSH_RX = 0xE2;

/** @brief Reuse last transmitted payload.  TX payload is not removed. */
constexpr uint8_t REUSE_TX_PL = 0xE3;

/** @brief Read RX payload width for the top payload in the RX FIFO. */
constexpr uint8_t R_RX_PL_WID = 0x60;

/**
 * @brief Write TX payload without requesting ACK from the receiver.
 *
 * Requires EN_DYN_ACK (FEATURE register bit 0) to be enabled.
 */
constexpr uint8_t W_TX_PAYLOAD_NOACK = 0xB0;

/** @brief No operation.  Useful for reading the STATUS register. */
constexpr uint8_t NOP = 0xFF;

/* ── Base opcodes for variable commands ─────────────────────────────── */

/**
 * @brief R_REGISTER base opcode.
 *
 * Final command = R_REGISTER_BASE | (address & 0x1F).
 * Bits [7:5] = 000.
 */
constexpr uint8_t R_REGISTER_BASE = 0x00;

/**
 * @brief W_REGISTER base opcode.
 *
 * Final command = W_REGISTER_BASE | (address & 0x1F).
 * Bits [7:5] = 001.
 */
constexpr uint8_t W_REGISTER_BASE = 0x20;

/**
 * @brief W_ACK_PAYLOAD base opcode.
 *
 * Final command = W_ACK_PAYLOAD_BASE | (pipe & 0x07).
 * Writes payload for transmission in an ACK packet on the given pipe.
 * Requires EN_ACK_PAY (FEATURE register bit 1) to be enabled.
 */
constexpr uint8_t W_ACK_PAYLOAD_BASE = 0xA8;

} // namespace cmd

/* ── Helper functions for variable commands ─────────────────────────── */

/**
 * @brief Compose an R_REGISTER command byte.
 *
 * @param addr  5-bit register address.  Use constants from
 *              nrf24l01plus/registers/addresses.h (e.g. nrf24::reg::STATUS).
 * @return      Command byte: 0b000A_AAAA.
 */
constexpr uint8_t cmd_r_register(uint8_t addr)
{
    return static_cast<uint8_t>(cmd::R_REGISTER_BASE | (addr & 0x1F));
}

/**
 * @brief Compose a W_REGISTER command byte.
 *
 * @param addr  5-bit register address.  Use constants from
 *              nrf24l01plus/registers/addresses.h (e.g. nrf24::reg::CONFIG).
 * @return      Command byte: 0b001A_AAAA.
 */
constexpr uint8_t cmd_w_register(uint8_t addr)
{
    return static_cast<uint8_t>(cmd::W_REGISTER_BASE | (addr & 0x1F));
}

/**
 * @brief Compose a W_ACK_PAYLOAD command byte.
 *
 * @param pipe  Pipe number (0–5).
 * @return      Command byte: 0b1010_1PPP.
 */
constexpr uint8_t cmd_w_ack_payload(uint8_t pipe)
{
    return static_cast<uint8_t>(cmd::W_ACK_PAYLOAD_BASE | (pipe & 0x07));
}

} // namespace nrf24
