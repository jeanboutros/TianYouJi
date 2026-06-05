#include <gtest/gtest.h>
#include <nrf24l01plus/registers.h>

using namespace nrf24;

/* ═══════════════════════════════════════════════════════════════════════════
 * to_reg() — individual field contributions
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(ToReg, TxPower_BitsAt_2_1) {
    EXPECT_EQ(to_reg(TxPower::Minus18dBm), 0x00);
    EXPECT_EQ(to_reg(TxPower::Minus12dBm), 0x02);
    EXPECT_EQ(to_reg(TxPower::Minus6dBm),  0x04);
    EXPECT_EQ(to_reg(TxPower::dBm0),       0x06);
}

TEST(ToReg, DataRate_NonAdjacentBits_5_3) {
    EXPECT_EQ(to_reg(DataRate::Mbps1),   0x00); /* DR_LOW=0(bit5), DR_HIGH=0(bit3) */
    EXPECT_EQ(to_reg(DataRate::Mbps2),   0x08); /* DR_LOW=0, DR_HIGH=1 → bit 3 */
    EXPECT_EQ(to_reg(DataRate::Kbps250), 0x20); /* DR_LOW=1 → bit 5, DR_HIGH=0 */
}

TEST(ToReg, ContWave_Bit7) {
    EXPECT_EQ(to_reg(ContWave::Disabled), 0x00);
    EXPECT_EQ(to_reg(ContWave::Enabled),  0x80);
}

TEST(ToReg, PllLock_Bit4) {
    EXPECT_EQ(to_reg(PllLock::Disabled), 0x00);
    EXPECT_EQ(to_reg(PllLock::Enabled),  0x10);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * RfSetup::to_byte() — known configurations
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(RfSetupToByte, DefaultMatchesResetValue) {
    RfSetup cfg;
    EXPECT_EQ(cfg.to_byte(), RfSetup::RESET_VALUE);
}

TEST(RfSetupToByte, ResetValueConstant) {
    EXPECT_EQ(RfSetup::RESET_VALUE, 0x0E);
}

TEST(RfSetupToByte, AddressConstant) {
    EXPECT_EQ(RfSetup::ADDRESS, 0x06);
}

TEST(RfSetupToByte, Kbps250_dBm0) {
    RfSetup cfg;
    cfg.data_rate = DataRate::Kbps250;
    cfg.tx_power  = TxPower::dBm0;
    EXPECT_EQ(cfg.to_byte(), 0x26); /* bit5 + bits2:1 = 0x20 | 0x06 */
}

TEST(RfSetupToByte, Mbps2_Minus18dBm) {
    RfSetup cfg;
    cfg.data_rate = DataRate::Mbps2;
    cfg.tx_power  = TxPower::Minus18dBm;
    EXPECT_EQ(cfg.to_byte(), 0x08); /* bit3 only */
}

TEST(RfSetupToByte, AllFieldsSet) {
    RfSetup cfg;
    cfg.data_rate = DataRate::Kbps250;
    cfg.tx_power  = TxPower::dBm0;
    cfg.cont_wave = ContWave::Enabled;
    cfg.pll_lock  = PllLock::Enabled;
    /* bit7 + bit5 + bit4 + bits2:1 = 0x80 | 0x20 | 0x10 | 0x06 = 0xB6 */
    EXPECT_EQ(cfg.to_byte(), 0xB6);
}

TEST(RfSetupToByte, ContWaveOnly) {
    RfSetup cfg;
    cfg.data_rate = DataRate::Mbps1;
    cfg.tx_power  = TxPower::Minus18dBm;
    cfg.cont_wave = ContWave::Enabled;
    EXPECT_EQ(cfg.to_byte(), 0x80);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * RfSetup::from_byte() — roundtrip tests
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(RfSetupFromByte, ResetValue) {
    RfSetup s = RfSetup::from_byte(0x0E);
    EXPECT_EQ(s.data_rate, DataRate::Mbps2);
    EXPECT_EQ(s.tx_power,  TxPower::dBm0);
    EXPECT_EQ(s.cont_wave, ContWave::Disabled);
    EXPECT_EQ(s.pll_lock,  PllLock::Disabled);
}

TEST(RfSetupFromByte, Kbps250_dBm0) {
    RfSetup s = RfSetup::from_byte(0x26);
    EXPECT_EQ(s.data_rate, DataRate::Kbps250);
    EXPECT_EQ(s.tx_power,  TxPower::dBm0);
}

TEST(RfSetupFromByte, Mbps2_Minus12dBm) {
    RfSetup s = RfSetup::from_byte(0x0A); /* bit3 + bit1 = 0x08 | 0x02 */
    EXPECT_EQ(s.data_rate, DataRate::Mbps2);
    EXPECT_EQ(s.tx_power,  TxPower::Minus12dBm);
}

TEST(RfSetupFromByte, AllFieldsSet) {
    RfSetup s = RfSetup::from_byte(0xB6);
    EXPECT_EQ(s.data_rate, DataRate::Kbps250);
    EXPECT_EQ(s.tx_power,  TxPower::dBm0);
    EXPECT_EQ(s.cont_wave, ContWave::Enabled);
    EXPECT_EQ(s.pll_lock,  PllLock::Enabled);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Roundtrip: to_byte() → from_byte() for every valid combination
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(RfSetupRoundtrip, AllValidCombinations) {
    const DataRate rates[] = { DataRate::Mbps1, DataRate::Mbps2, DataRate::Kbps250 };
    const TxPower  powers[] = { TxPower::Minus18dBm, TxPower::Minus12dBm,
                                TxPower::Minus6dBm, TxPower::dBm0 };
    const ContWave waves[] = { ContWave::Disabled, ContWave::Enabled };
    const PllLock  locks[] = { PllLock::Disabled, PllLock::Enabled };

    for (auto dr : rates) {
        for (auto pw : powers) {
            for (auto cw : waves) {
                for (auto pl : locks) {
                    RfSetup original;
                    original.data_rate = dr;
                    original.tx_power  = pw;
                    original.cont_wave = cw;
                    original.pll_lock  = pl;

                    uint8_t raw = original.to_byte();
                    RfSetup decoded = RfSetup::from_byte(raw);

                    EXPECT_EQ(decoded.data_rate, dr)
                        << "Failed for raw=0x" << std::hex << (int)raw;
                    EXPECT_EQ(decoded.tx_power, pw)
                        << "Failed for raw=0x" << std::hex << (int)raw;
                    EXPECT_EQ(decoded.cont_wave, cw)
                        << "Failed for raw=0x" << std::hex << (int)raw;
                    EXPECT_EQ(decoded.pll_lock, pl)
                        << "Failed for raw=0x" << std::hex << (int)raw;
                }
            }
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Edge cases
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(RfSetupEdge, FromByte_0xFF_ReservedBitsIgnored) {
    /* 0xFF has reserved bit 6 set, obsolete bit 0 set, and DataRate = 0b11
     * (reserved encoding). from_byte() should still extract fields without
     * crashing — though the DataRate value will be 0b11 which is not a
     * defined enumerator. */
    RfSetup s = RfSetup::from_byte(0xFF);
    EXPECT_EQ(s.tx_power,  TxPower::dBm0);
    EXPECT_EQ(s.cont_wave, ContWave::Enabled);
    EXPECT_EQ(s.pll_lock,  PllLock::Enabled);
    /* DataRate will be 0b11 — cast back to uint8_t to verify extraction */
    EXPECT_EQ(static_cast<uint8_t>(s.data_rate), 0b11);
}

TEST(RfSetupEdge, FromByte_0x00) {
    RfSetup s = RfSetup::from_byte(0x00);
    EXPECT_EQ(s.data_rate, DataRate::Mbps1);
    EXPECT_EQ(s.tx_power,  TxPower::Minus18dBm);
    EXPECT_EQ(s.cont_wave, ContWave::Disabled);
    EXPECT_EQ(s.pll_lock,  PllLock::Disabled);
}

TEST(RfSetupEdge, ReservedBit6_NotContaminating) {
    /* Bit 6 is reserved and should not affect any field */
    RfSetup a = RfSetup::from_byte(0x06);       /* normal reset sans bit6 */
    RfSetup b = RfSetup::from_byte(0x06 | 0x40); /* same + bit6 set */
    EXPECT_EQ(a.data_rate, b.data_rate);
    EXPECT_EQ(a.tx_power,  b.tx_power);
    EXPECT_EQ(a.cont_wave, b.cont_wave);
    EXPECT_EQ(a.pll_lock,  b.pll_lock);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Register address constants (nrf24::reg namespace)
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(RegAddresses, RF_SETUP_MatchesStructAddress) {
    EXPECT_EQ(reg::RF_SETUP, RfSetup::ADDRESS);
}

TEST(RegAddresses, AllAddressesUnique) {
    /* Spot-check a few register addresses don't collide */
    EXPECT_NE(reg::CONFIG, reg::RF_SETUP);
    EXPECT_NE(reg::STATUS, reg::RF_SETUP);
    EXPECT_NE(reg::RF_CH,  reg::RF_SETUP);
}
