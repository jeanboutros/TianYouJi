#include "hal_espidf.h"
#include "esp_check.h"
#include <cstring>

namespace nrf24 {

void EspIdfHal::init(const EspIdfPins &pins)
{
    ce_pin_ = pins.ce;

    /* SPI bus */
    spi_bus_config_t bus_cfg = {};
    bus_cfg.miso_io_num     = pins.miso;
    bus_cfg.mosi_io_num     = pins.mosi;
    bus_cfg.sclk_io_num     = pins.sclk;
    bus_cfg.quadwp_io_num   = -1;
    bus_cfg.quadhd_io_num   = -1;
    bus_cfg.max_transfer_sz = 33;
    ESP_ERROR_CHECK(spi_bus_initialize(pins.spi_host, &bus_cfg, SPI_DMA_DISABLED));

    /* SPI device: 8-bit command, mode 0, 8 MHz */
    spi_device_interface_config_t dev_cfg = {};
    dev_cfg.command_bits   = 8;
    dev_cfg.address_bits   = 0;
    dev_cfg.mode           = 0;
    dev_cfg.clock_speed_hz = 8000000;
    dev_cfg.spics_io_num   = pins.csn;
    dev_cfg.queue_size     = 1;
    ESP_ERROR_CHECK(spi_bus_add_device(pins.spi_host, &dev_cfg, &spi_handle_));

    /* CE pin: output, initially low (standby-I) */
    gpio_config_t gpio_cfg = {
        .pin_bit_mask   = (1ULL << pins.ce),
        .mode           = GPIO_MODE_OUTPUT,
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
        /* Write-only: use inline buffer for 1-4 bytes, heap for more */
        if (len <= 4) {
            t.flags = SPI_TRANS_USE_TXDATA;
            memcpy(t.tx_data, tx, len);
        } else {
            t.tx_buffer = tx;
        }
    } else if (!tx && rx) {
        /* Read-only: use inline buffer for 1-4 bytes */
        if (len <= 4) {
            t.flags = SPI_TRANS_USE_RXDATA;
        } else {
            t.rx_buffer = rx;
        }
    } else if (tx && rx) {
        /* Bidirectional — set both buffers */
        t.tx_buffer = tx;
        t.rx_buffer = rx;
    }

    ESP_ERROR_CHECK(spi_device_polling_transmit(spi_handle_, &t));

    /* Copy back from inline rx buffer if used */
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

} // namespace nrf24
