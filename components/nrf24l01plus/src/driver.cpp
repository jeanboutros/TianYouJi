#include "nrf24l01plus/driver.h"
#include "nrf24l01plus/commands.h"
#include <cstring>

namespace nrf24 {

uint8_t Driver::read_reg(uint8_t reg)
{
    uint8_t val = 0;
    if (!hal_.spi_xfer(cmd_r_register(reg), nullptr, &val, 1)) {
        return 0xFF; /* SPI failure — MISO stuck high sentinel */
    }
    return val;
}

bool Driver::write_reg(uint8_t reg, uint8_t value)
{
    return hal_.spi_xfer(cmd_w_register(reg), &value, nullptr, 1);
}

bool Driver::write_reg_multi(uint8_t reg, const uint8_t *data, uint8_t len)
{
    return hal_.spi_xfer(cmd_w_register(reg), data, nullptr, len);
}

bool Driver::read_reg_multi(uint8_t reg, uint8_t *buf, uint8_t len)
{
    return hal_.spi_xfer(cmd_r_register(reg), nullptr, buf, len);
}

bool Driver::read_payload(uint8_t *buf, uint8_t len)
{
    return hal_.spi_xfer(cmd::R_RX_PAYLOAD, nullptr, buf, len);
}

bool Driver::flush_rx()
{
    return hal_.spi_xfer(cmd::FLUSH_RX, nullptr, nullptr, 0);
}

bool Driver::flush_tx()
{
    return hal_.spi_xfer(cmd::FLUSH_TX, nullptr, nullptr, 0);
}

void Driver::ce_high()
{
    hal_.ce_high();
}

void Driver::ce_low()
{
    hal_.ce_low();
}

bool Driver::write_and_verify(uint8_t reg, uint8_t value)
{
    if (!write_reg(reg, value)) return false;
    uint8_t readback = read_reg(reg);
    return readback == value;
}

bool Driver::write_and_verify_multi(uint8_t reg, const uint8_t *data, uint8_t len)
{
    if (!write_reg_multi(reg, data, len)) return false;
    uint8_t buf[5]; /* max address width is 5 bytes */
    if (len > sizeof(buf)) return false;
    if (!read_reg_multi(reg, buf, len)) return false;
    return memcmp(data, buf, len) == 0;
}

} // namespace nrf24
