#pragma once

#include <cstdint>
#include "nrf24l01plus/hal.h"

namespace nrf24 {

/**
 * @brief Platform-independent driver for the nRF24L01+ transceiver.
 *
 * All hardware access is delegated to a Hal implementation supplied at
 * construction.  This class encodes the nRF24L01+ SPI command protocol
 * and provides a convenient register/payload API.
 *
 * @code
 *   EspIdfHal hal;
 *   hal.init(pins);
 *   nrf24::Driver radio(hal);
 *   radio.write_reg(0x00, 0x03);   // CONFIG: PWR_UP + PRIM_RX
 *   radio.ce_high();                // Enter RX mode
 * @endcode
 */
class Driver {
public:
    /**
     * @brief Construct the driver with a HAL implementation.
     *
     * @param hal  Reference to a platform-specific Hal.  Must outlive
     *             this Driver instance.
     */
    explicit Driver(Hal &hal) : hal_(hal) {}

    /**
     * @brief Read a single-byte register.
     *
     * Sends the R_REGISTER command (bits [7:5] = 000) with the 5-bit
     * register address and clocks out one byte on MISO.
     *
     * @code
     *   cmd byte: 0b000A_AAAA   (A = reg address)
     *   read_reg(0x07) -> cmd = 0x07, reads STATUS
     * @endcode
     *
     * @param reg  Register address (only bits [4:0] used).
     * @return     The byte value read from the register.
     */
    uint8_t read_reg(uint8_t reg);

    /**
     * @brief Write a single-byte register.
     *
     * Sends the W_REGISTER command (bits [7:5] = 001) with the 5-bit
     * register address followed by one data byte on MOSI.
     *
     * @code
     *   cmd byte: 0b001A_AAAA   (A = reg address)
     *   write_reg(0x00, 0x03) -> cmd = 0x20, writes CONFIG
     * @endcode
     *
     * @param reg    Register address (only bits [4:0] used).
     * @param value  Byte to write into the register.
     */
    void write_reg(uint8_t reg, uint8_t value);

    /**
     * @brief Write a multi-byte register (e.g. TX_ADDR, RX_ADDR_Px).
     *
     * Sends W_REGISTER followed by `len` bytes from `data`.
     *
     * @param reg   Register address (only bits [4:0] used).
     * @param data  Pointer to bytes to write (LSByte first per nRF24 convention).
     * @param len   Number of bytes (1-5 for address registers).
     */
    void write_reg_multi(uint8_t reg, const uint8_t *data, uint8_t len);

    /**
     * @brief Read a multi-byte register.
     *
     * Sends R_REGISTER followed by `len` clock bytes, storing MISO data
     * into `buf`.
     *
     * @param reg  Register address (only bits [4:0] used).
     * @param buf  Destination buffer (must be >= len bytes).
     * @param len  Number of bytes to read.
     */
    void read_reg_multi(uint8_t reg, uint8_t *buf, uint8_t len);

    /**
     * @brief Read the RX payload from the FIFO.
     *
     * Sends the R_RX_PAYLOAD command (0x61) and clocks out `len` bytes.
     * The payload is removed from the RX FIFO after reading.
     *
     * @param buf  Destination buffer (must be >= len bytes).
     * @param len  Number of payload bytes to read (1-32).
     */
    void read_payload(uint8_t *buf, uint8_t len);

    /**
     * @brief Flush the RX FIFO.
     *
     * Sends the FLUSH_RX command (0xE2).  Should be used in RX mode
     * only; behaviour is undefined in TX mode per the datasheet.
     */
    void flush_rx();

    /**
     * @brief Flush the TX FIFO.
     *
     * Sends the FLUSH_TX command (0xE1).  Should be used in TX mode
     * only; behaviour is undefined in RX mode per the datasheet.
     */
    void flush_tx();

    /**
     * @brief Drive CE pin high (activate RX or start TX).
     */
    void ce_high();

    /**
     * @brief Drive CE pin low (return to standby-I).
     */
    void ce_low();

    /**
     * @brief Access the underlying HAL.
     *
     * @return Reference to the Hal implementation passed at construction.
     */
    Hal &hal() { return hal_; }

private:
    Hal &hal_;
};

} // namespace nrf24
