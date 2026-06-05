#pragma once

#include <cstdint>

namespace nrf24 {

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
 *   nrf24::RfSetup cfg;
 *   cfg.tx_power = nrf24::TxPower::dBm0;        // maximum range
 *   cfg.tx_power = nrf24::TxPower::Minus18dBm;  // minimum range, save power
 * @endcode
 */
enum class TxPower : uint8_t {
    Minus18dBm = 0b00, ///< -18 dBm — lowest power
    Minus12dBm = 0b01, ///< -12 dBm
    Minus6dBm  = 0b10, ///<  -6 dBm
    dBm0       = 0b11, ///<   0 dBm — maximum range (reset default)
};

/**
 * @brief Return the RF_SETUP register contribution of a @ref TxPower value.
 *
 * Shifts the 2-bit RF_PWR field into bits 2:1.
 *
 * @code
 *   to_reg(TxPower::Minus18dBm) → 0b 0000 0000 = 0x00
 *   to_reg(TxPower::dBm0)       → 0b 0000 0110 = 0x06
 * @endcode
 *
 * @param v  Selected TX power level.
 * @return   Byte with RF_PWR placed at bits 2:1; all other bits zero.
 */
constexpr uint8_t to_reg(TxPower v) {
    return static_cast<uint8_t>(v) << 1;
}

/** @cond INTERNAL — implementation details, not part of the public API. */
namespace detail {

enum class DrBit : uint8_t { Low = 0, High = 1 };

struct DataRateBits {
    DrBit dr_low;  ///< RF_DR_LOW  — bit 5 of RF_SETUP
    DrBit dr_high; ///< RF_DR_HIGH — bit 3 of RF_SETUP
};

} // namespace detail
/** @endcond */

/**
 * @brief Air data rate options for the nRF24L01+ RF_SETUP register.
 *
 * Abstracts the two non-adjacent hardware bits (RF_DR_LOW bit 5 and
 * RF_DR_HIGH bit 3) into a single named choice.
 *
 * @code
 *   RF_DR_LOW  RF_DR_HIGH   Rate
 *       0           0       1 Mbps   (reset default)
 *       0           1       2 Mbps
 *       1           0       250 kbps
 *       1           1       Reserved — do not use
 * @endcode
 *
 * Usage:
 * @code
 *   cfg.data_rate = nrf24::DataRate::Kbps250;  // longest range
 *   cfg.data_rate = nrf24::DataRate::Mbps2;    // highest throughput
 * @endcode
 */
enum class DataRate : uint8_t {
    Mbps1   = 0b00, ///< 1 Mbps   — RF_DR_LOW=0, RF_DR_HIGH=0 (reset default)
    Mbps2   = 0b01, ///< 2 Mbps   — RF_DR_LOW=0, RF_DR_HIGH=1
    Kbps250 = 0b10, ///< 250 kbps — RF_DR_LOW=1, RF_DR_HIGH=0 (longest range)
};

/** @cond INTERNAL */
namespace detail {
constexpr DataRateBits to_bits(DataRate rate) {
    const uint8_t v = static_cast<uint8_t>(rate);
    return {
        static_cast<DrBit>((v >> 1) & 0x01), /* RF_DR_LOW  = upper bit */
        static_cast<DrBit>((v >> 0) & 0x01), /* RF_DR_HIGH = lower bit */
    };
}
} // namespace detail
/** @endcond */

/**
 * @brief Return the RF_SETUP register contribution of a @ref DataRate value.
 *
 * Splits the rate into RF_DR_LOW (bit 5) and RF_DR_HIGH (bit 3) and places
 * both in a single byte ready to OR into the register.
 *
 * @code
 *   to_reg(DataRate::Mbps1)   → 0b 0000 0000 = 0x00  (both bits 0)
 *   to_reg(DataRate::Mbps2)   → 0b 0000 1000 = 0x08  (bit 3 set)
 *   to_reg(DataRate::Kbps250) → 0b 0010 0000 = 0x20  (bit 5 set)
 * @endcode
 *
 * @param v  Selected data rate.
 * @return   Byte with RF_DR_LOW at bit 5 and RF_DR_HIGH at bit 3; all other bits zero.
 */
constexpr uint8_t to_reg(DataRate v) {
    const detail::DataRateBits b = detail::to_bits(v);
    return (static_cast<uint8_t>(b.dr_low)  << 5)
         | (static_cast<uint8_t>(b.dr_high) << 3);
}

/**
 * @brief Continuous carrier wave control for RF_SETUP bit 7.
 *
 * When @ref Enabled, the nRF24L01+ outputs an unmodulated centered carrier.
 * See datasheet §RF_SETUP and the continuous carrier wave test procedure.
 */
enum class ContWave : uint8_t {
    Disabled = 0, ///< Normal operation (reset default)
    Enabled  = 1, ///< Output unmodulated carrier — bit 7 set
};

/**
 * @brief Return the RF_SETUP register contribution of a @ref ContWave value.
 *
 * Places the CONT_WAVE flag at bit 7.
 *
 * @code
 *   to_reg(ContWave::Disabled) → 0b 0000 0000 = 0x00
 *   to_reg(ContWave::Enabled)  → 0b 1000 0000 = 0x80
 * @endcode
 *
 * @param v  Selected continuous wave setting.
 * @return   Byte with CONT_WAVE at bit 7; all other bits zero.
 */
constexpr uint8_t to_reg(ContWave v) {
    return static_cast<uint8_t>(v) << 7;
}

/**
 * @brief PLL lock control for RF_SETUP bit 4.
 *
 * Force PLL lock signal. Only used in test. (datasheet §RF_SETUP bit 4)
 */
enum class PllLock : uint8_t {
    Disabled = 0, ///< Normal operation (reset default)
    Enabled  = 1, ///< Force PLL lock — bit 4 set
};

/**
 * @brief Return the RF_SETUP register contribution of a @ref PllLock value.
 *
 * Places the PLL_LOCK flag at bit 4.
 *
 * @code
 *   to_reg(PllLock::Disabled) → 0b 0000 0000 = 0x00
 *   to_reg(PllLock::Enabled)  → 0b 0001 0000 = 0x10
 * @endcode
 *
 * @param v  Selected PLL lock setting.
 * @return   Byte with PLL_LOCK at bit 4; all other bits zero.
 */
constexpr uint8_t to_reg(PllLock v) {
    return static_cast<uint8_t>(v) << 4;
}

/**
 * @brief Typed, human-readable representation of the nRF24L01+ RF_SETUP
 *        register (address 0x06).
 *
 * Instead of remembering raw bit positions, configure the radio like this:
 *
 * @code
 *   nrf24::RfSetup cfg;                                    // defaults: 2 Mbps, 0 dBm
 *   cfg.data_rate = nrf24::DataRate::Kbps250;              // change to 250 kbps
 *   cfg.tx_power  = nrf24::TxPower::Minus6dBm;            // reduce TX power
 *   nrf24_write_reg(nrf24::RfSetup::ADDRESS, cfg.to_byte());  // → 0x24
 * @endcode
 *
 * To verify what is currently programmed on the device:
 * @code
 *   nrf24::RfSetup actual = nrf24::RfSetup::from_byte(
 *       nrf24_read_reg(nrf24::RfSetup::ADDRESS));
 *   // inspect actual.data_rate, actual.tx_power, etc.
 * @endcode
 *
 * Default field values match the nRF24L01+ power-on reset state (0x0E):
 * 2 Mbps, 0 dBm, no continuous wave, no forced PLL lock.
 */
struct RfSetup {
    /* ── Constants ────────────────────────────────────────────────────── */

    static constexpr uint8_t ADDRESS     = 0x06; ///< Register address in the nRF24L01+ register map
    static constexpr uint8_t RESET_VALUE = 0x0E; ///< Power-on reset value (1 Mbps, 0 dBm)

    /* ── Public data members ──────────────────────────────────────────── */

    DataRate data_rate = DataRate::Mbps2;    ///< Air data rate (bits 5 and 3) — reset default: 2 Mbps
    TxPower  tx_power  = TxPower::dBm0;     ///< TX output power (bits 2:1)

    ContWave cont_wave = ContWave::Disabled; ///< Continuous carrier transmit — bit 7 (datasheet §RF_SETUP)
    PllLock  pll_lock  = PllLock::Disabled;  ///< Force PLL lock signal, only used in test — bit 4 (datasheet §RF_SETUP)

    /* ── Methods ──────────────────────────────────────────────────────── */

    /**
     * @brief Serialise this configuration into the raw byte for RF_SETUP.
     *
     * Combines each field's register contribution using the @ref to_reg()
     * overloads, then ORs them together:
     *
     * @code
     *   bit 7 ← to_reg(cont_wave)
     *   bit 6 ← 0 (reserved)
     *   bit 5 ← to_reg(data_rate) RF_DR_LOW
     *   bit 4 ← pll_lock
     *   bit 3 ← to_reg(data_rate) RF_DR_HIGH
     *   bits 2:1 ← to_reg(tx_power)
     *   bit 0 ← 0 (obsolete)
     * @endcode
     *
     * Examples:
     * @code
     *   nrf24::RfSetup().to_byte()
     *   // DataRate::Mbps2, TxPower::dBm0 → 0b 0000 1110 = 0x0E
     *
     *   nrf24::RfSetup{DataRate::Kbps250, TxPower::dBm0}.to_byte()
     *   // RF_DR_LOW=1(bit5), RF_DR_HIGH=0(bit3), PWR=0b11(bits2:1) → 0b 0010 0110 = 0x26
     * @endcode
     *
     * You can also compose the register value manually using to_reg():
     * @code
     *   uint8_t reg = nrf24::to_reg(nrf24::TxPower::dBm0)
     *               | nrf24::to_reg(nrf24::DataRate::Kbps250)
     *               | nrf24::to_reg(nrf24::ContWave::Disabled);
     * @endcode
     *
     * @return Raw byte ready to write to register 0x06.
     */
    uint8_t to_byte() const {
        return to_reg(cont_wave)
             | to_reg(data_rate)
             | to_reg(pll_lock)
             | to_reg(tx_power);
    }

    /**
     * @brief Reconstruct an RfSetup from a raw byte read back from the device.
     *
     * Useful for verifying what the hardware currently has programmed, or for
     * round-trip testing that to_byte() / from_byte() are inverses.
     *
     * @code
     *   RfSetup a;
     *   a.data_rate = nrf24::DataRate::Kbps250;
     *   RfSetup b = RfSetup::from_byte(a.to_byte());
     *   // b.data_rate == nrf24::DataRate::Kbps250  ✓
     * @endcode
     *
     * @param byte  Raw value read from register 0x06.
     * @return      Populated RfSetup matching the byte's fields.
     */
    static RfSetup from_byte(uint8_t byte) {
        RfSetup s;
        const uint8_t dr_low  = (byte >> 5) & 0x01;
        const uint8_t dr_high = (byte >> 3) & 0x01;
        s.data_rate  = static_cast<DataRate>((dr_low << 1) | dr_high);
        s.tx_power   = static_cast<TxPower>((byte >> 1) & 0x03);
        s.pll_lock   = static_cast<PllLock>((byte >> 4) & 0x01);
        s.cont_wave  = static_cast<ContWave>((byte >> 7) & 0x01);
        return s;
    }
};

} // namespace nrf24
