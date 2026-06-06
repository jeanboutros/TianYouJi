#include "nrf24l01plus/diag.h"

#include <cstdio>
#include <cstring>
#include "nrf24l01plus/registers.h"
#include "nrf24l01plus/ble_config.h"

namespace nrf24 {
namespace diag {

/** @brief Mask for all valid bits in a register with no reserved bits. */
static constexpr uint8_t REG_MASK_ALL = 0xFF;

/** @brief Mask for bits [6:0] of RF_CH (bit 7 is reserved, per datasheet §9.4). */
static constexpr uint8_t REG_MASK_RF_CH = 0x7F;

/** @brief Mask for bits [5:0] of RX_PW_Px (bits [7:6] are reserved, per datasheet §9.8). */
static constexpr uint8_t REG_MASK_RX_PW = 0x3F;

bool verify_ble_rx(Driver &radio, const ble::RxConfig &config)
{
    printf("\n=== NRF24L01+ Diagnostic ===\n");
    bool all_pass = true;

    auto status = radio.read_reg(Status{});
    uint8_t raw_status = status.to_byte();
    bool spi_ok = (raw_status != 0xFF);
    printf("  SPI comms           STATUS=0x%02X  [%s]\n",
           raw_status, spi_ok ? "PASS" : "FAIL -- check wiring / power");
    if (!spi_ok) {
        printf("  Cannot reach device -- aborting further checks.\n\n");
        return false;
    }

    /* Build expected register values using typed structs */
    Config exp_config;
    exp_config.power_mode   = PowerMode::Up;
    exp_config.primary      = PrimaryMode::RX;
    exp_config.crc_mode     = CrcMode::Disabled;
    exp_config.crc_encoding = CrcEncoding::Bytes1;
    exp_config.irq_mask     = IrqMask::None;

    EnAa exp_en_aa;
    for (int i = 0; i < 6; ++i) exp_en_aa.pipe[i] = false;

    EnRxAddr exp_en_rx;
    for (int i = 0; i < 6; ++i) exp_en_rx.pipe[i] = false;
    exp_en_rx.pipe[0] = true;

    SetupAw exp_aw;
    exp_aw.address_width = AddressWidth::Bytes4;

    SetupRetr exp_retr;
    exp_retr.delay = AutoRetransmitDelay::Us250;
    exp_retr.count = AutoRetransmitCount::Disabled;

    RfCh exp_rf_ch;
    exp_rf_ch.channel = ble::ADV_CHANNELS[config.initial_channel_idx].rf_ch;

    RfSetup exp_rf;
    exp_rf.data_rate = DataRate::Mbps1;
    exp_rf.tx_power  = TxPower::dBm0;
    exp_rf.cont_wave = ContWave::Disabled;
    exp_rf.pll_lock  = PllLock::Disabled;

    RxPw exp_pw{config.payload_width};

    const struct {
        const char *name;
        uint8_t     reg;
        uint8_t     expected;
        uint8_t     mask;
    } checks[] = {
        { "CONFIG",     reg::CONFIG,     exp_config.to_byte(),  REG_MASK_ALL },
        { "EN_AA",      reg::EN_AA,      exp_en_aa.to_byte(),   REG_MASK_ALL },
        { "EN_RXADDR",  reg::EN_RXADDR,  exp_en_rx.to_byte(),   REG_MASK_ALL },
        { "SETUP_AW",   reg::SETUP_AW,   exp_aw.to_byte(),      REG_MASK_ALL },
        { "SETUP_RETR", reg::SETUP_RETR, exp_retr.to_byte(),    REG_MASK_ALL },
        { "RF_CH",      reg::RF_CH,      exp_rf_ch.to_byte(),   REG_MASK_RF_CH },
        { "RF_SETUP",   reg::RF_SETUP,   exp_rf.to_byte(),      REG_MASK_ALL },
        { "RX_PW_P0",   reg::RX_PW_P0,  exp_pw.to_byte(),      REG_MASK_RX_PW },
    };

    for (int i = 0; i < (int)(sizeof(checks) / sizeof(checks[0])); i++) {
        uint8_t val  = radio.read_reg(checks[i].reg);
        uint8_t got  = val  & checks[i].mask;
        uint8_t want = checks[i].expected & checks[i].mask;
        bool    pass = (got == want);
        printf("  %-10s  reg=0x%02X  exp=0x%02X  got=0x%02X  [%s]\n",
               checks[i].name, checks[i].reg, want, got,
               pass ? "PASS" : "FAIL");
        if (!pass) all_pass = false;
    }

    uint8_t addr[4];

    radio.read_reg_multi(reg::RX_ADDR_P0, addr, 4);
    bool rx_ok = (memcmp(addr, ble::ADV_ACCESS_ADDR, 4) == 0);
    printf("  RX_ADDR_P0  exp=%02X:%02X:%02X:%02X  got=%02X:%02X:%02X:%02X  [%s]\n",
           ble::ADV_ACCESS_ADDR[0], ble::ADV_ACCESS_ADDR[1],
           ble::ADV_ACCESS_ADDR[2], ble::ADV_ACCESS_ADDR[3],
           addr[0], addr[1], addr[2], addr[3], rx_ok ? "PASS" : "FAIL");
    if (!rx_ok) all_pass = false;

    radio.read_reg_multi(reg::TX_ADDR, addr, 4);
    bool tx_ok = (memcmp(addr, ble::ADV_ACCESS_ADDR, 4) == 0);
    printf("  TX_ADDR     exp=%02X:%02X:%02X:%02X  got=%02X:%02X:%02X:%02X  [%s]\n",
           ble::ADV_ACCESS_ADDR[0], ble::ADV_ACCESS_ADDR[1],
           ble::ADV_ACCESS_ADDR[2], ble::ADV_ACCESS_ADDR[3],
           addr[0], addr[1], addr[2], addr[3], tx_ok ? "PASS" : "FAIL");
    if (!tx_ok) all_pass = false;

    printf("\nDiagnostic: %s\n\n",
           all_pass ? "PASS -- device ready" : "FAIL -- check connections / power");
    return all_pass;
}

} // namespace diag
} // namespace nrf24
