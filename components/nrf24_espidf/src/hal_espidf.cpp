#include "nrf24_espidf/hal_espidf.h"
#include "esp_check.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>

namespace nrf24 {

void EspIdfHal::init(const EspIdfPins &pins)
{
    ce_pin_   = pins.ce;
    mosi_pin_ = pins.mosi;

    /* SPI bus */
    spi_bus_config_t bus_cfg = {};
    bus_cfg.miso_io_num     = pins.miso;
    bus_cfg.mosi_io_num     = pins.mosi;
    bus_cfg.sclk_io_num     = pins.sclk;
    bus_cfg.quadwp_io_num   = -1;
    bus_cfg.quadhd_io_num   = -1;
    bus_cfg.max_transfer_sz = 33;
    ESP_ERROR_CHECK(spi_bus_initialize(pins.spi_host, &bus_cfg, SPI_DMA_DISABLED));

    /* SPI device: 8-bit command, mode 0, 1 MHz.
     * 1 MHz chosen for clone chip (Si24R1, BK2425) compatibility.
     * Genuine nRF24L01+ supports up to 10 MHz, but clones often
     * misinterpret register writes above 1–2 MHz, causing silent
     * configuration failures.  1 MHz is safe for all known variants. */
    spi_device_interface_config_t dev_cfg = {};
    dev_cfg.command_bits   = 8;
    dev_cfg.address_bits   = 0;
    dev_cfg.mode           = 0;
    dev_cfg.clock_speed_hz = 1000000;
    dev_cfg.spics_io_num   = pins.csn;
    dev_cfg.queue_size     = 1;
    ESP_ERROR_CHECK(spi_bus_add_device(pins.spi_host, &dev_cfg, &spi_handle_));

    /* CE pin: input-output, initially low (standby-I).
     * GPIO_MODE_INPUT_OUTPUT enables the input buffer so that
     * gpio_get_level() returns the actual pad level, which is
     * essential for CE readback diagnostics.  With OUTPUT-only
     * mode, gpio_get_level() always returns 0 regardless of the
     * driven level (ESP-IDF GPIO driver documentation).
     *
     * Note: GPIO5 is SPI3_HOST's native IO_MUX CS0 pin.  Setting
     * the function selector to PIN_FUNC_GPIO disconnects the
     * IO_MUX CS0 signal from the pad, but moving CE to a
     * non-IOMUX pin (e.g. GPIO4) would eliminate this overlap. */
    gpio_config_t gpio_cfg = {
        .pin_bit_mask   = (1ULL << pins.ce),
        .mode           = GPIO_MODE_INPUT_OUTPUT,
        .pull_up_en     = GPIO_PULLUP_DISABLE,
        .pull_down_en   = GPIO_PULLDOWN_DISABLE,
        .intr_type      = GPIO_INTR_DISABLE,
    };
    gpio_config(&gpio_cfg);
    gpio_set_level(pins.ce, 0);
}

void EspIdfHal::spi_xfer(uint8_t cmd, const uint8_t *tx, uint8_t *rx, uint8_t len)
{
    spi_transaction_t t = {};
    t.cmd    = cmd;
    t.length = static_cast<uint32_t>(len) * 8;

    if (len == 0) {
        /* Command-only transaction (e.g. FLUSH_RX, FLUSH_TX) */
    } else if (tx && !rx) {
        if (len <= 4) {
            t.flags = SPI_TRANS_USE_TXDATA;
            memcpy(t.tx_data, tx, len);
        } else {
            t.tx_buffer = tx;
        }
    } else if (!tx && rx) {
        if (len <= 4) {
            t.flags = SPI_TRANS_USE_RXDATA;
        } else {
            t.rx_buffer = rx;
        }
    } else if (tx && rx) {
        t.tx_buffer = tx;
        t.rx_buffer = rx;
    }

    ESP_ERROR_CHECK(spi_device_polling_transmit(spi_handle_, &t));

    if (rx && (t.flags & SPI_TRANS_USE_RXDATA) && len <= 4) {
        memcpy(rx, t.rx_data, len);
    }
}

void EspIdfHal::ce_high()
{
    gpio_set_level(ce_pin_, 1);
}

void EspIdfHal::ce_low()
{
    gpio_set_level(ce_pin_, 0);
}

void EspIdfHal::delay_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

void EspIdfHal::delay_us(uint32_t us)
{
    esp_rom_delay_us(us);
}

} // namespace nrf24
