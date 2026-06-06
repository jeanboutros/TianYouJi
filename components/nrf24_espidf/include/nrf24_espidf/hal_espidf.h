#pragma once

#include <nrf24l01plus/hal.h>
#include "driver/spi_master.h"
#include "driver/gpio.h"

namespace nrf24 {

/**
 * @brief ESP-IDF SPI/GPIO pin configuration for the nRF24L01+ HAL.
 */
struct EspIdfPins {
    gpio_num_t miso;       ///< SPI MISO pin
    gpio_num_t mosi;       ///< SPI MOSI pin
    gpio_num_t sclk;       ///< SPI clock pin
    gpio_num_t csn;        ///< Chip-select (active low)
    gpio_num_t ce;         ///< Chip-enable (RX/TX activation)
    spi_host_device_t spi_host;  ///< ESP-IDF SPI host (e.g. SPI3_HOST)
};

/**
 * @brief ESP-IDF implementation of the nRF24L01+ HAL.
 *
 * Provides SPI transport via esp_driver_spi and CE pin control via
 * esp_driver_gpio.  Call init() once before passing to a Driver.
 *
 * @code
 *   nrf24::EspIdfHal hal;
 *   hal.init({ .miso=GPIO_NUM_19, .mosi=GPIO_NUM_23, .sclk=GPIO_NUM_18,
 *              .csn=GPIO_NUM_17, .ce=GPIO_NUM_4, .spi_host=SPI3_HOST });
 *   nrf24::Driver radio(hal);
 * @endcode
 */
class EspIdfHal final : public Hal {
public:
    /**
     * @brief Destructor — removes the SPI device and frees the SPI bus.
     *
     * If init() was called, removes the SPI device handle.  The SPI bus
     * is freed only if spi_host_ is valid, which requires init() to have
     * stored it.  Safe to call on an uninitialised instance (spi_handle_
     * is null, spi_host_ is unset).
     */
    ~EspIdfHal() override;

    /**
     * @brief Initialise the SPI bus, device, and CE GPIO.
     *
     * @param pins  Pin assignment and SPI host selection.
     */
    void init(const EspIdfPins &pins);

    bool spi_xfer(uint8_t cmd, const uint8_t *tx, uint8_t *rx, uint8_t len) override;
    void ce_high() override;
    void ce_low() override;
    void delay_ms(uint32_t ms) override;
    /**
     * @brief Busy-wait for the specified number of microseconds.
     *
     * Uses the ESP-IDF ROM busy-wait function (esp_rom_delay_us),
     * which is appropriate for delays too short for FreeRTOS
     * scheduler granularity (sub-millisecond).
     *
     * @param us  Duration in microseconds.
     */
    void delay_us(uint32_t us) override;

    /**
     * @brief Read the current logic level of the CE GPIO pin.
     *
     * @return 1 if CE pin is HIGH, 0 if LOW.
     */
    int ce_read() const override;

    /**
     * @brief Return a monotonic timestamp in microseconds.
     *
     * @return Current timestamp in microseconds (monotonic).
     */
    int64_t timestamp_us() const override;

    /**
     * @brief Get the MOSI pin number (for platform-specific post-init control).
     *
     * @return The GPIO number configured for MOSI.
     */
    gpio_num_t mosi_pin() const { return mosi_pin_; }

    /**
     * @brief Get the CE pin number.
     *
     * @return The GPIO number configured for CE.
     */
    gpio_num_t ce_pin() const { return ce_pin_; }

    /**
     * @brief Get the underlying SPI device handle.
     *
     * @return The ESP-IDF SPI device handle.
     */
    spi_device_handle_t spi_handle() const { return spi_handle_; }

private:
    spi_device_handle_t spi_handle_{};
    spi_host_device_t spi_host_{}; ///< SPI host used for bus initialisation (needed for cleanup)
    gpio_num_t ce_pin_{};
    gpio_num_t mosi_pin_{};
    bool initialized_ = false; ///< True after init() has been called successfully
};

} // namespace nrf24
