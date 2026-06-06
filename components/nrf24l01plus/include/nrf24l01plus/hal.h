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

    /**
     * @brief Busy-wait for the specified number of microseconds.
     *
     * Used for nRF24L01+ timing constraints that are too short for
     * millisecond granularity — e.g. Tstby2a (130 µs max Standby-I →
     * RX settling time) and RPD validity (170 µs after CE HIGH, per
     * datasheet §6.1.7 Table 16).
     *
     * Implementations should use a busy-wait spin (not a task yield)
     * because the delay is too short for OS scheduler granularity.
     *
     * @code
     *   // Wait 200 µs for RX settling after CE HIGH
     *   hal.delay_us(200);
     * @endcode
     *
     * @param us  Duration in microseconds.  Accuracy depends on the
     *            platform's busy-wait timer resolution.
     */
    virtual void delay_us(uint32_t us) = 0;

    /**
     * @brief Read the current logic level of the CE GPIO pin.
     *
     * Returns 1 if the pin is HIGH, 0 if LOW.  The implementation must
     * have the input buffer enabled (e.g. GPIO_MODE_INPUT_OUTPUT on
     * ESP32) for this to return meaningful values.
     *
     * Used by diagnostic phases to verify that ce_high()/ce_low()
     * actually changed the physical pin state.
     *
     * @return 1 if CE pin is HIGH, 0 if LOW.
     */
    virtual int ce_read() const = 0;

    /**
     * @brief Return a monotonic timestamp in microseconds.
     *
     * Used by diagnostic phases to measure CE transition timing and
     * phase execution duration.  The epoch is arbitrary; only differences
     * between successive calls are meaningful.
     *
     * @return Current timestamp in microseconds (monotonic).
     */
    virtual int64_t timestamp_us() const = 0;

    /**
     * @brief Yield the CPU for the specified number of milliseconds.
     *
     * Semantic equivalent of delay_ms() — intended for human-scale waits
     * (retry delays, poll intervals) rather than nRF24 microsecond timing.
     * The default implementation delegates to delay_ms().
     *
     * @param ms  Duration in milliseconds.
     */
    void sleep_ms(uint32_t ms) { delay_ms(ms); }
};

} // namespace nrf24
