#pragma once

#include <cstdint>

namespace nrf24 {

/**
 * @brief Hardware abstraction layer for the nRF24L01+ driver.
 *
 * Implement this interface to port the driver to any platform.
 * The driver calls these methods for all hardware interaction —
 * SPI transfers and CE pin control.
 *
 * Each SPI transaction consists of an 8-bit command byte followed by
 * zero or more data bytes.  The command is always clocked out on MOSI;
 * data bytes may be TX-only, RX-only, or absent.
 *
 * @code
 *   // Example: ESP-IDF implementation sketch
 *   class EspIdfHal : public nrf24::Hal {
 *   public:
 *       void spi_xfer(uint8_t cmd, const uint8_t* tx, uint8_t* rx, uint8_t len) override;
 *       void ce_high() override;
 *       void ce_low() override;
 *   };
 * @endcode
 */
class Hal {
public:
    virtual ~Hal() = default;

    /**
     * @brief Perform one SPI transaction: command byte + optional data.
     *
     * The implementation must:
     * 1. Assert CSN low
     * 2. Clock out `cmd` (8 bits)
     * 3. Clock `len` data bytes: send from `tx` (if non-null) while
     *    capturing MISO into `rx` (if non-null).  If `tx` is null,
     *    send zeros.  If `rx` is null, discard MISO data.
     * 4. De-assert CSN
     *
     * @param cmd  8-bit SPI command byte.  Use constants and helpers from
     *              nrf24l01plus/commands.h (e.g. cmd::FLUSH_RX, cmd_r_register()).
     * @param tx   Transmit buffer (may be nullptr for read-only transactions).
     * @param rx   Receive buffer (may be nullptr for write-only transactions).
     * @param len  Number of data bytes after the command (0 for command-only).
     */
    virtual void spi_xfer(uint8_t cmd, const uint8_t *tx, uint8_t *rx, uint8_t len) = 0;

    /**
     * @brief Drive the CE pin high (activate RX or start TX pulse).
     */
    virtual void ce_high() = 0;

    /**
     * @brief Drive the CE pin low (return to standby-I).
     */
    virtual void ce_low() = 0;

    /**
     * @brief Busy-wait for the specified number of milliseconds.
     *
     * Used for nRF24L01+ timing constraints that require the host to
     * wait before proceeding — e.g. Tpd2stby (1.5 ms after PWR_UP=1
     * before CE is safe to assert, per datasheet §6.1.2 Table 9).
     *
     * Implementations should yield the CPU where possible (e.g.
     * FreeRTOS vTaskDelay) rather than spinning.
     *
     * @param ms  Duration in milliseconds.  The actual granularity
     *            depends on the platform tick rate (e.g. 1 ms per tick
     *            on a 1000 Hz FreeRTOS config).
     */
    virtual void delay_ms(uint32_t ms) = 0;
};

} // namespace nrf24
