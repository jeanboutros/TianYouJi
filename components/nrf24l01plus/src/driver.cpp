#include "nrf24l01plus/driver.h"

namespace nrf24 {

uint8_t Driver::read_reg(uint8_t reg)
{
    uint8_t val = 0;
    hal_.spi_xfer(static_cast<uint8_t>(0x00 | (reg & 0x1F)), nullptr, &val, 1);
    return val;
}

void Driver::write_reg(uint8_t reg, uint8_t value)
{
    hal_.spi_xfer(static_cast<uint8_t>(0x20 | (reg & 0x1F)), &value, nullptr, 1);
}

void Driver::write_reg_multi(uint8_t reg, const uint8_t *data, uint8_t len)
{
    hal_.spi_xfer(static_cast<uint8_t>(0x20 | (reg & 0x1F)), data, nullptr, len);
}

void Driver::read_reg_multi(uint8_t reg, uint8_t *buf, uint8_t len)
{
    hal_.spi_xfer(static_cast<uint8_t>(0x00 | (reg & 0x1F)), nullptr, buf, len);
}

void Driver::read_payload(uint8_t *buf, uint8_t len)
{
    hal_.spi_xfer(0x61, nullptr, buf, len); /* R_RX_PAYLOAD */
}

void Driver::flush_rx()
{
    hal_.spi_xfer(0xE2, nullptr, nullptr, 0); /* FLUSH_RX */
}

void Driver::flush_tx()
{
    hal_.spi_xfer(0xE1, nullptr, nullptr, 0); /* FLUSH_TX */
}

void Driver::ce_high()
{
    hal_.ce_high();
}

void Driver::ce_low()
{
    hal_.ce_low();
}

} // namespace nrf24
