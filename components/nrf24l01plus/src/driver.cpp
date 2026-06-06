#include "nrf24l01plus/driver.h"
#include "nrf24l01plus/commands.h"
#include <cstring>

namespace nrf24 {

uint8_t Driver::read_reg(uint8_t reg)
{
    uint8_t val = 0;
    hal_.spi_xfer(cmd_r_register(reg), nullptr, &val, 1);
    return val;
}

void Driver::write_reg(uint8_t reg, uint8_t value)
{
    hal_.spi_xfer(cmd_w_register(reg), &value, nullptr, 1);
}

void Driver::write_reg_multi(uint8_t reg, const uint8_t *data, uint8_t len)
{
    hal_.spi_xfer(cmd_w_register(reg), data, nullptr, len);
}

void Driver::read_reg_multi(uint8_t reg, uint8_t *buf, uint8_t len)
{
    hal_.spi_xfer(cmd_r_register(reg), nullptr, buf, len);
}

void Driver::read_payload(uint8_t *buf, uint8_t len)
{
    hal_.spi_xfer(cmd::R_RX_PAYLOAD, nullptr, buf, len);
}

void Driver::flush_rx()
{
    hal_.spi_xfer(cmd::FLUSH_RX, nullptr, nullptr, 0);
}

void Driver::flush_tx()
{
    hal_.spi_xfer(cmd::FLUSH_TX, nullptr, nullptr, 0);
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
    write_reg(reg, value);
    uint8_t readback = read_reg(reg);
    return readback == value;
}

bool Driver::write_and_verify_multi(uint8_t reg, const uint8_t *data, uint8_t len)
{
    write_reg_multi(reg, data, len);
    uint8_t buf[5]; // max address width is 5 bytes
    if (len > sizeof(buf)) return false;
    read_reg_multi(reg, buf, len);
    return memcmp(data, buf, len) == 0;
}

} // namespace nrf24
