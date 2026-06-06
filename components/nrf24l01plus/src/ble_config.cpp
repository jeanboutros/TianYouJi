#include "nrf24l01plus/ble_config.h"

namespace nrf24 {
namespace ble {

void configure_rx(Driver &radio, const RxConfig &config)
{
    /* Power up, PRX mode, CRC disabled (BLE handles its own CRC) */
    Config cfg;
    cfg.power_mode   = PowerMode::Up;
    cfg.primary      = PrimaryMode::RX;
    cfg.crc_mode     = CrcMode::Disabled;
    cfg.crc_encoding = CrcEncoding::Bytes1;
    cfg.irq_mask     = IrqMask::None;
    radio.write_reg(cfg);

    /* Disable auto-ACK on all pipes */
    EnAa en_aa;
    for (int i = 0; i < 6; ++i) en_aa.pipe[i] = false;
    radio.write_reg(en_aa);

    /* Enable pipe 0 only */
    EnRxAddr en_rx;
    for (int i = 0; i < 6; ++i) en_rx.pipe[i] = false;
    en_rx.pipe[0] = true;
    radio.write_reg(en_rx);

    /* 4-byte address width (BLE access address is 4 bytes) */
    SetupAw aw;
    aw.address_width = AddressWidth::Bytes4;
    radio.write_reg(aw);

    /* No retransmission */
    SetupRetr retr;
    retr.delay = AutoRetransmitDelay::Us250;
    retr.count = AutoRetransmitCount::Disabled;
    radio.write_reg(retr);

    /* Initial RF channel */
    RfCh rf_ch;
    rf_ch.channel = ADV_CHANNELS[config.initial_channel_idx].rf_ch;
    radio.write_reg(rf_ch);

    /* 1 Mbps, 0 dBm (BLE advertising uses 1 Mbps) */
    RfSetup rf;
    rf.data_rate = DataRate::Mbps1;
    rf.tx_power  = TxPower::dBm0;
    rf.cont_wave = ContWave::Disabled;
    rf.pll_lock  = PllLock::Disabled;
    radio.write_reg(rf);

    /* Payload width on pipe 0 */
    RxPwP0 pw0;
    pw0.payload_width = config.payload_width;
    radio.write_reg(pw0);

    /* Set pipe 0 RX address and TX address to BLE advertising access address */
    radio.write_reg_multi(reg::RX_ADDR_P0, ADV_ACCESS_ADDR, 4);
    radio.write_reg_multi(reg::TX_ADDR, ADV_ACCESS_ADDR, 4);

    /* Flush RX FIFO and clear interrupt flags */
    radio.flush_rx();
    clear_irq_flags(radio);

    /* Per datasheet §6.1.2 Table 9: Tpd2stby = 1.5 ms after PWR_UP=1.
     * Wait for the oscillator to stabilise before asserting CE.
     * 2 ms provides margin over the 1.5 ms minimum. */
    radio.hal().delay_ms(2);

    /* Transition to RX mode — CE must be HIGH for the device to receive.
     * Per datasheet §6.1 Table 6: PWR_UP=1, PRIM_RX=1, CE=1 → RX Mode. */
    radio.ce_high();
}

} // namespace ble
} // namespace nrf24
