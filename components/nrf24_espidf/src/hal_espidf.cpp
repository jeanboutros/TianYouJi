#include "nrf24_espidf/hal_espidf.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>

namespace nrf24 {

void EspIdfHal::init(const EspIdfPins &pins)
{
    ce_pin_    = pins.ce;
    mosi_pin_  = pins.mosi;
    spi_host_  = pins.spi_host;

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
     * GPIO4 is used for CE to avoid overlap with SPI3_HOST's native
     * IO_MUX CS0 pin on GPIO5. */
    gpio_config_t gpio_cfg = {
        .pin_bit_mask   = (1ULL << pins.ce),
        .mode           = GPIO_MODE_INPUT_OUTPUT,
        .pull_up_en     = GPIO_PULLUP_DISABLE,
        .pull_down_en   = GPIO_PULLDOWN_DISABLE,
        .intr_type      = GPIO_INTR_DISABLE,
    };
    gpio_config(&gpio_cfg);
    gpio_set_level(pins.ce, 0);

    initialized_ = true;
}

EspIdfHal::~EspIdfHal()
{
    if (initialized_) {
        if (spi_handle_) {
            spi_bus_remove_device(spi_handle_);
            spi_handle_ = nullptr;
        }
        spi_bus_free(spi_host_);
        initialized_ = false;
    }
}

bool EspIdfHal::spi_xfer(uint8_t cmd, const uint8_t *tx, uint8_t *rx, uint8_t len)
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

    esp_err_t ret = spi_device_polling_transmit(spi_handle_, &t);
    if (ret != ESP_OK) {
        ESP_LOGW("EspIdfHal", "SPI transfer failed: cmd=0x%02X len=%u ret=%d",
                 static_cast<unsigned>(cmd), static_cast<unsigned>(len), ret);
        return false;
    }

    if (rx && (t.flags & SPI_TRANS_USE_RXDATA) && len <= 4) {
        memcpy(rx, t.rx_data, len);
    }

    return true;
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

/**
 * @brief Read the current logic level of the CE GPIO pin.
 *
 * Uses gpio_get_level() which requires the input buffer to be
 * enabled (GPIO_MODE_INPUT_OUTPUT set during init()).
 *
 * @return 1 if CE pin is HIGH, 0 if LOW.
 */
int EspIdfHal::ce_read() const
{
    return gpio_get_level(ce_pin_);
}

/**
 * @brief Return a monotonic timestamp in microseconds.
 *
 * Uses esp_timer_get_time() which returns microseconds since boot.
 * The value is monotonic and suitable for measuring intervals.
 *
 * @return Current timestamp in microseconds (monotonic).
 */
int64_t EspIdfHal::timestamp_us() const
{
    return esp_timer_get_time();
}

} // namespace nrf24
