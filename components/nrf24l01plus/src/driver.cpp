#include "nrf24l01plus/driver.h"
#include "esp_check.h"

namespace nrf24 {

void Driver::init(const PinConfig &pins)
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

uint8_t Driver::read_reg(uint8_t reg)
{
    spi_transaction_t t = {};
    t.flags  = SPI_TRANS_USE_RXDATA;
    t.cmd    = static_cast<uint16_t>(0x00 | (reg & 0x1F));
    t.length = 8;
    ESP_ERROR_CHECK(spi_device_polling_transmit(spi_handle_, &t));
    return t.rx_data[0];
}

void Driver::write_reg(uint8_t reg, uint8_t value)
{
    spi_transaction_t t = {};
    t.flags      = SPI_TRANS_USE_TXDATA;
    t.cmd        = static_cast<uint16_t>(0x20 | (reg & 0x1F));
    t.length     = 8;
    t.tx_data[0] = value;
    ESP_ERROR_CHECK(spi_device_polling_transmit(spi_handle_, &t));
}

void Driver::write_reg_multi(uint8_t reg, const uint8_t *data, uint8_t len)
{
    spi_transaction_t t = {};
    t.cmd       = static_cast<uint16_t>(0x20 | (reg & 0x1F));
    t.length    = static_cast<uint32_t>(len) * 8;
    t.tx_buffer = data;
    ESP_ERROR_CHECK(spi_device_polling_transmit(spi_handle_, &t));
}

void Driver::read_reg_multi(uint8_t reg, uint8_t *buf, uint8_t len)
{
    spi_transaction_t t = {};
    t.cmd       = static_cast<uint16_t>(0x00 | (reg & 0x1F));
    t.length    = static_cast<uint32_t>(len) * 8;
    t.rx_buffer = buf;
    ESP_ERROR_CHECK(spi_device_polling_transmit(spi_handle_, &t));
}

void Driver::read_payload(uint8_t *buf, uint8_t len)
{
    spi_transaction_t t = {};
    t.cmd       = 0x61; /* R_RX_PAYLOAD */
    t.length    = static_cast<uint32_t>(len) * 8;
    t.rx_buffer = buf;
    ESP_ERROR_CHECK(spi_device_polling_transmit(spi_handle_, &t));
}

void Driver::flush_rx()
{
    spi_transaction_t t = {};
    t.cmd    = 0xE2; /* FLUSH_RX */
    t.length = 0;
    ESP_ERROR_CHECK(spi_device_polling_transmit(spi_handle_, &t));
}

void Driver::flush_tx()
{
    spi_transaction_t t = {};
    t.cmd    = 0xE1; /* FLUSH_TX */
    t.length = 0;
    ESP_ERROR_CHECK(spi_device_polling_transmit(spi_handle_, &t));
}

void Driver::ce_high()
{
    gpio_set_level(ce_pin_, 1);
}

void Driver::ce_low()
{
    gpio_set_level(ce_pin_, 0);
}

} // namespace nrf24
