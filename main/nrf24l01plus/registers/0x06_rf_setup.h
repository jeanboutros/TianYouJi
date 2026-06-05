#pragma once

#include <cstdint>

/* ═══════════════════════════════════════════════════════════════════════════
 * nRF24L01+ — RF_SETUP register (address 0x06)
 *
 * Register bit layout (reset value: 0x0E = 0b 0000 1110):
 *
 *   bit 7      bit 6    bit 5      bit 4     bit 3       bits 2:1   bit 0
 *   CONT_WAVE  (rsvd)   RF_DR_LOW  PLL_LOCK  RF_DR_HIGH  RF_PWR     (obs)
 *
 * Data rate is encoded across two non-adjacent bits:
 *
 *   RF_DR_LOW(5)  RF_DR_HIGH(3)   Rate
 *       0              0          1 Mbps   ← reset default
 *       0              1          2 Mbps
 *       1              0          250 kbps
 *       1              1          Reserved
 * ═══════════════════════════════════════════════════════════════════════════ */


/**
 * @brief TX output power options for the nRF24L01+ RF_SETUP register.
 *
 * Maps directly to the RF_PWR[1:0] field (bits 2:1).
 *
 * @code
 *   RF_PWR[1:0]   Power      Typical current draw (TX)
 *   00            -18 dBm    7.0 mA
 *   01            -12 dBm    7.5 mA
 *   10             -6 dBm    9.0 mA
 *   11              0 dBm   11.3 mA  ← maximum range
 * @endcode
 *
 * Usage:
 * @code
 *   RfSetup cfg;
 *   cfg.tx_power = NrfTxPower::dBm0;        // maximum range
 *   cfg.tx_power = NrfTxPower::Minus18dBm;  // minimum range, save power
 * @endcode
 */
enum class NrfTxPower : uint8_t {
    Minus18dBm = 0b00, ///< -18 dBm — lowest power
    Minus12dBm = 0b01, ///< -12 dBm
    Minus6dBm  = 0b10, ///<  -6 dBm
    dBm0       = 0b11, ///<   0 dBm — maximum range (reset default)
};

/**
 * @brief Air data rate options for the nRF24L01+ RF_SETUP register.
 *
 * Encoded across two non-adjacent bits — RF_DR_LOW (bit 5) and RF_DR_HIGH
 * (bit 3) — which this enum abstracts into a single clean choice.
 *
 * @code
 *   RF_DR_LOW  RF_DR_HIGH   Rate       Internal enum value
 *       0           0       1 Mbps     0b00  ← reset default
 *       0           1       2 Mbps     0b01
 *       1           0       250 kbps   0b10
 *       1           1       Reserved   (do not use)
 * @endcode
 *
 * The enum value encodes [RF_DR_LOW | RF_DR_HIGH] as a 2-bit number so
 * @ref RfSetup::to_byte() can split it back out with simple bit shifts.
 *
 * Usage:
 * @code
 *   cfg.data_rate = NrfDataRate::Kbps250;  // best range, lowest throughput
 *   cfg.data_rate = NrfDataRate::Mbps2;    // highest throughput
 * @endcode
 */
enum class NrfDataRate : uint8_t {
    Mbps1   = 0b00, ///< 1 Mbps   — RF_DR_LOW=0, RF_DR_HIGH=0 (reset default)
    Mbps2   = 0b01, ///< 2 Mbps   — RF_DR_LOW=0, RF_DR_HIGH=1
    Kbps250 = 0b10, ///< 250 kbps — RF_DR_LOW=1, RF_DR_HIGH=0 (longest range)
};

/**
 * @brief Typed, human-readable representation of the nRF24L01+ RF_SETUP
 *        register (address 0x06).
 *
 * Instead of remembering raw bit positions, configure the radio like this:
 *
 * @code
 *   RfSetup cfg;                              // defaults: 1 Mbps, 0 dBm
 *   cfg.data_rate = NrfDataRate::Kbps250;     // change to 250 kbps
 *   cfg.tx_power  = NrfTxPower::Minus6dBm;   // reduce TX power
 *   nrf24_write_reg(REG_RF_SETUP, cfg.to_byte());  // → 0x24
 * @endcode
 *
 * To verify what is currently programmed on the device:
 * @code
 *   RfSetup actual = RfSetup::from_byte(nrf24_read_reg(REG_RF_SETUP));
 *   // inspect actual.data_rate, actual.tx_power, etc.
 * @endcode
 *
 * Default field values match the nRF24L01+ power-on reset state (0x0E):
 * 1 Mbps, 0 dBm, no continuous wave, no forced PLL lock.
 */
struct RfSetup {
    /* ── Public data members ──────────────────────────────────────────── */

    NrfDataRate data_rate = NrfDataRate::Mbps1;   ///< Air data rate (bits 5 and 3)
    NrfTxPower  tx_power  = NrfTxPower::dBm0;     ///< TX output power (bits 2:1)

    /** Enables continuous carrier transmit when high. (datasheet §RF_SETUP bit 7) */
    bool cont_wave = false;

    /** Force PLL lock signal. Only used in test. (datasheet §RF_SETUP bit 4) */
    bool pll_lock  = false;

    /* ── Methods ──────────────────────────────────────────────────────── */

    /**
     * @brief Serialise this configuration into the raw byte for RF_SETUP.
     *
     * Bit packing performed here:
     * @code
     *   bit 7 ← cont_wave
     *   bit 6 ← 0 (reserved)
     *   bit 5 ← RF_DR_LOW  = upper bit of NrfDataRate value
     *   bit 4 ← pll_lock
     *   bit 3 ← RF_DR_HIGH = lower bit of NrfDataRate value
     *   bits 2:1 ← tx_power (NrfTxPower value)
     *   bit 0 ← 0 (obsolete)
     * @endcode
     *
     * Examples:
     * @code
     *   RfSetup().to_byte()
     *   // Mbps1(0b00), dBm0(0b11) → 0b 0000 0110 = 0x06
     *
     *   RfSetup{NrfDataRate::Kbps250, NrfTxPower::dBm0}.to_byte()
     *   // DR_LOW=1(bit5), DR_HIGH=0(bit3), PWR=0b11(bits2:1) → 0b 0010 0110 = 0x26
     * @endcode
     *
     * @return Raw byte ready to write to register 0x06.
     */
    uint8_t to_byte() const {
        const uint8_t dr   = static_cast<uint8_t>(data_rate);
        const uint8_t dr_low  = (dr >> 1) & 0x01; /* upper bit of 2-bit encoding → bit 5 */
        const uint8_t dr_high = (dr >> 0) & 0x01; /* lower bit of 2-bit encoding → bit 3 */
        const uint8_t pwr  = static_cast<uint8_t>(tx_power);

        return (static_cast<uint8_t>(cont_wave) << 7)
             | (dr_low                          << 5)
             | (static_cast<uint8_t>(pll_lock)  << 4)
             | (dr_high                         << 3)
             | (pwr                             << 1);
    }

    /**
     * @brief Reconstruct an RfSetup from a raw byte read back from the device.
     *
     * Useful for verifying what the hardware currently has programmed, or for
     * round-trip testing that to_byte() / from_byte() are inverses.
     *
     * @code
     *   RfSetup a;
     *   a.data_rate = NrfDataRate::Kbps250;
     *   RfSetup b = RfSetup::from_byte(a.to_byte());
     *   // b.data_rate == NrfDataRate::Kbps250  ✓
     * @endcode
     *
     * @param byte  Raw value read from register 0x06.
     * @return      Populated RfSetup matching the byte's fields.
     */
    static RfSetup from_byte(uint8_t byte) {
        RfSetup s;
        const uint8_t dr_low  = (byte >> 5) & 0x01;
        const uint8_t dr_high = (byte >> 3) & 0x01;
        s.data_rate  = static_cast<NrfDataRate>((dr_low << 1) | dr_high);
        s.tx_power   = static_cast<NrfTxPower>((byte >> 1) & 0x03);
        s.pll_lock   = static_cast<bool>((byte >> 4) & 0x01);
        s.cont_wave  = static_cast<bool>((byte >> 7) & 0x01);
        return s;
    }
};
