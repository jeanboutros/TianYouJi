#pragma once

#include <cstdint>
#include "driver/spi_master.h"
#include "driver/gpio.h"

namespace nrf24 {

/**
 * @brief Pin and SPI host configuration for the nRF24L01+ driver.
 *
 * All fields must be set before passing to Driver::init().
 */
struct PinConfig {
    gpio_num_t miso;       ///< SPI MISO pin
    gpio_num_t mosi;       ///< SPI MOSI pin
    gpio_num_t sclk;       ///< SPI clock pin
    gpio_num_t csn;        ///< Chip-select (active low)
    gpio_num_t ce;         ///< Chip-enable (RX/TX activation)
    spi_host_device_t spi_host;  ///< ESP-IDF SPI host (e.g. SPI3_HOST)
};

/**
 * @brief Hardware driver for the nRF24L01+ transceiver.
 *
 * Manages SPI communication and CE pin control.  One instance per
 * physical radio module.  Call init() once before any other method.
 *
 * @code
 *   nrf24::Driver radio;
 *   radio.init({
 *       .miso = GPIO_NUM_19, .mosi = GPIO_NUM_23, .sclk = GPIO_NUM_18,
 *       .csn  = GPIO_NUM_17, .ce   = GPIO_NUM_5,
 *       .spi_host = SPI3_HOST
 *   });
 *   radio.write_reg(0x00, 0x03);   // CONFIG: PWR_UP + PRIM_RX
 *   radio.ce_high();                // Enter RX mode
 * @endcode
 */
class Driver {
public:
    /**
     * @brief Initialise the SPI bus, add the nRF24 device, and configure CE.
     *
     * Sets SPI mode 0, 8 MHz clock, 8-bit command phase.  CE pin is
     * driven low after initialisation (standby-I mode).
     *
     * @param pins  Pin assignment and SPI host selection.
     */
    void init(const PinConfig &pins);

    /**
     * @brief Read a single-byte register.
     *
     * Sends the R_REGISTER command (bits [7:5] = 000) with the 5-bit
     * register address and clocks out one byte on MISO.
     *
     * @code
     *   cmd byte: 0b000A_AAAA   (A = reg address)
     *   nrf24_read_reg(0x07) → cmd = 0x07, reads STATUS
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
     *   nrf24_write_reg(0x00, 0x03) → cmd = 0x20, writes CONFIG
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
     * @param len   Number of bytes (1–5 for address registers).
     */
    void write_reg_multi(uint8_t reg, const uint8_t *data, uint8_t len);

    /**
     * @brief Read a multi-byte register.
     *
     * Sends R_REGISTER followed by `len` clock bytes, storing MISO data
     * into `buf`.
     *
     * @param reg  Register address (only bits [4:0] used).
     * @param buf  Destination buffer (must be ≥ len bytes).
     * @param len  Number of bytes to read.
     */
    void read_reg_multi(uint8_t reg, uint8_t *buf, uint8_t len);

    /**
     * @brief Read the RX payload from the FIFO.
     *
     * Sends the R_RX_PAYLOAD command (0x61) and clocks out `len` bytes.
     * The payload is removed from the RX FIFO after reading.
     *
     * @param buf  Destination buffer (must be ≥ len bytes).
     * @param len  Number of payload bytes to read (1–32).
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
     * @brief Get the CE pin number.
     *
     * @return The GPIO number configured for CE.
     */
    gpio_num_t ce_pin() const { return ce_pin_; }

    /**
     * @brief Get the underlying SPI device handle.
     *
     * Useful for advanced/custom SPI transactions not covered by
     * the convenience methods.
     *
     * @return The ESP-IDF SPI device handle.
     */
    spi_device_handle_t spi_handle() const { return spi_handle_; }

private:
    spi_device_handle_t spi_handle_{};
    gpio_num_t ce_pin_{};
};

} // namespace nrf24
