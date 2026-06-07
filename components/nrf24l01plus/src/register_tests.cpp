/**
 * @file register_tests.cpp
 * @brief Runtime verification for non-constexpr nRF24L01+ register paths.
 *
 * RfSetup::to_byte() and from_byte() are non-constexpr because the
 * DataRate field spans non-adjacent bits (RF_DR_LOW bit 5, RF_DR_HIGH
 * bit 3), so they cannot be exercised with @c static_assert.  This file
 * provides a runtime test runner that covers those paths.
 *
 * All constexpr register tests live in
 * @ref nrf24l01plus/registers/static_asserts.h and are compiled
 * as part of @c static_asserts.cpp (zero runtime cost).
 */

#include "nrf24l01plus/register_tests.h"
#include "nrf24l01plus/registers.h"
#include <cstdio>
#include <cstdlib>

namespace nrf24 {
namespace test {

/* ═══════════════════════════════════════════════════════════════════════════
 * RfSetup (0x06) — runtime tests
 *
 * Bit layout: bit 7 CONT_WAVE, 6 (rsvd), 5 RF_DR_LOW, 4 PLL_LOCK,
 *             3 RF_DR_HIGH, 2:1 RF_PWR, 0 (obs/rsvd)
 * Valid mask: 0xBE (bits 6,0 reserved/obsolete)
 * Reset value: 0x0E
 *
 * to_byte() and from_byte() are NOT constexpr, so exhaustive
 * compile-time round-trips are not possible (see static_asserts.h).
 * ═══════════════════════════════════════════════════════════════════════════ */

static bool test_rf_setup()
{
    /* Reset value: 2 Mbps + 0 dBm = 0x08 | 0x06 = 0x0E */
    if (RfSetup().to_byte() != 0x0E) {
        printf("FAIL: RfSetup default != 0x0E (got 0x%02X)\n",
               RfSetup().to_byte());
        return false;
    }
    printf("  [PASS] RfSetup default == 0x0E\n");

    /* 250kbps + 0 dBm = 0x20 | 0x06 = 0x26 */
    {
        RfSetup s;
        s.data_rate = DataRate::Kbps250;
        s.tx_power  = TxPower::dBm0;
        if (s.to_byte() != 0x26) {
            printf("FAIL: RfSetup 250kbps+0dBm != 0x26 (got 0x%02X)\n",
                   s.to_byte());
            return false;
        }
        printf("  [PASS] RfSetup 250kbps + 0dBm == 0x26\n");
    }

    /* 1Mbps + -18 dBm = 0x00 | 0x00 = 0x00 */
    {
        RfSetup s;
        s.data_rate = DataRate::Mbps1;
        s.tx_power  = TxPower::Minus18dBm;
        if (s.to_byte() != 0x00) {
            printf("FAIL: RfSetup 1Mbps-18dBm != 0x00 (got 0x%02X)\n",
                   s.to_byte());
            return false;
        }
        printf("  [PASS] RfSetup 1Mbps + -18dBm == 0x00\n");
    }

    /* All bits set: cont_wave + pll_lock + 250kbps + 0dBm
     * 0x80 | 0x20 | 0x10 | 0x06 = 0xB6 */
    {
        RfSetup s;
        s.cont_wave = ContWave::Enabled;
        s.pll_lock  = PllLock::Enabled;
        s.data_rate = DataRate::Kbps250;
        s.tx_power  = TxPower::dBm0;
        if (s.to_byte() != 0xB6) {
            printf("FAIL: RfSetup all-set != 0xB6 (got 0x%02X)\n",
                   s.to_byte());
            return false;
        }
        printf("  [PASS] RfSetup all features == 0xB6\n");
    }

    /* Round-trip tests */
    {
        const uint8_t test_bytes[] = {0x0E, 0x26, 0x00, 0xB6};
        for (uint8_t b : test_bytes) {
            auto roundtrip = RfSetup::from_byte(b).to_byte();
            if (roundtrip != b) {
                printf("FAIL: RfSetup round-trip 0x%02X → 0x%02X\n",
                       b, roundtrip);
                return false;
            }
        }
        printf("  [PASS] RfSetup round-trip (0x0E, 0x26, 0x00, 0xB6)\n");
    }

    /* Field extraction from 0x26 (250kbps, 0dBm) */
    {
        auto s = RfSetup::from_byte(0x26);
        if (s.data_rate != DataRate::Kbps250
         || s.tx_power  != TxPower::dBm0
         || s.cont_wave != ContWave::Disabled
         || s.pll_lock  != PllLock::Disabled) {
            printf("FAIL: RfSetup from_byte(0x26) field extraction\n");
            return false;
        }
        printf("  [PASS] RfSetup from_byte(0x26) fields\n");
    }

    /* Field extraction from 0x0E (2Mbps, 0dBm) — reset default */
    {
        auto s = RfSetup::from_byte(0x0E);
        if (s.data_rate != DataRate::Mbps2
         || s.tx_power  != TxPower::dBm0) {
            printf("FAIL: RfSetup from_byte(0x0E) field extraction\n");
            return false;
        }
        printf("  [PASS] RfSetup from_byte(0x0E) fields\n");
    }

    return true;
}

bool run_register_tests()
{
    printf("=== nRF24L01+ Register Runtime Tests ===\n");
    if (!test_rf_setup()) {
        printf("FAILED: RfSetup runtime tests\n");
        return false;
    }
    printf("\nAll register runtime tests passed!\n");
    return true;
}

} // namespace test
} // namespace nrf24