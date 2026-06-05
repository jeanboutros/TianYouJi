#pragma once

#include <nrf24l01plus/hal.h>
#include "driver/spi_master.h"
#include "driver/gpio.h"

namespace nrf24 {

/**
 * @brief ESP-IDF SPI/GPIO pin configuration for the nRF24L01+ HAL.
 */
struct EspIdfPins {
    gpio_num_t miso;
    gpio_num_t mosi;
    gpio_num_t sclk;
    gpio_num_t csn;
    gpio_num_t ce;
    spi_host_device_t spi_host;
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
 *              .csn=GPIO_NUM_17, .ce=GPIO_NUM_5, .spi_host=SPI3_HOST });
 *   nrf24::Driver radio(hal);
 * @endcode
 */
class EspIdfHal final : public Hal {
public:
    /**
     * @brief Initialise the SPI bus, device, and CE GPIO.
     *
     * @param pins  Pin assignment and SPI host selection.
     */
    void init(const EspIdfPins &pins);

    void spi_xfer(uint8_t cmd, const uint8_t *tx, uint8_t *rx, uint8_t len) override;
    void ce_high() override;
    void ce_low() override;

    /**
     * @brief Get the CE pin number (for platform-specific use outside the lib).
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
    gpio_num_t ce_pin_{};
};

} // namespace nrf24
