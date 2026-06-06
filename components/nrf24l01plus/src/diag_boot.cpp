#include "nrf24l01plus/diag_boot.h"

#include <cstdio>
#include <cstring>
#include "nrf24l01plus/registers.h"
#include "nrf24l01plus/ble.h"
#include "nrf24l01plus/ble_config.h"

namespace nrf24 {
namespace diag {

/** @brief Mask for all valid bits in a register with no reserved bits. */
static constexpr uint8_t REG_MASK_ALL = 0xFF;

/** @brief Mask for bits [6:0] of RF_CH (bit 7 is reserved, per datasheet §9.4). */
static constexpr uint8_t REG_MASK_RF_CH = 0x7F;

/** @brief Mask for bits [5:0] of RX_PW_Px (bits [7:6] are reserved, per datasheet §9.8). */
static constexpr uint8_t REG_MASK_RX_PW = 0x3F;

/** @brief Mask for relevant bits of RF_SETUP (bit 6 and bit 0 are reserved/obsolete). */
static constexpr uint8_t REG_MASK_RF_SETUP = 0xBE;

/**
 * @brief Format a detail string using [diag:phaseN] prefix.
 *
 * Produces machine-parseable output lines in the format:
 *   [diag:phaseN] PHASE_NAME result=STATUS detail="..."
 *
 * @param phase    The diagnostic phase number.
 * @param name     Human-readable phase name.
 * @param status   The phase result status.
 * @param detail   Detail string (may be empty).
 */
static void print_phase(uint8_t phase, const char *name,
                         DiagStatus status, const char *detail)
{
    const char *status_str = "PASS";
    switch (status) {
    case DiagStatus::Pass: status_str = "PASS"; break;
    case DiagStatus::Fail: status_str = "FAIL"; break;
    case DiagStatus::Skip: status_str = "SKIP"; break;
    case DiagStatus::Warn: status_str = "WARN"; break;
    }
    printf("[diag:phase%u] %s result=%s detail=\"%s\"\n",
           static_cast<unsigned>(phase), name, status_str,
           detail ? detail : "");
}

/* ════════════════════════════════════════════════════════════════════════════
 * Phase 1: SPI Communication Test
 *
 * Three-stage test:
 * Stage 1: SPI connectivity — read STATUS, detect warm vs cold boot
 * Stage 2: Write-verify — write known patterns, read back
 * Stage 3: Clone detection — test EN_AA/EN_CRC override
 * ════════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Phase 1: Verify SPI communication, POR defaults, and detect clones.
 *
 * Performs three sub-stages:
 * 1. POR value check — reads registers and compares against datasheet defaults.
 *    Detects warm boot (module retains state from prior session).
 * 2. Write-verify — writes known patterns and reads them back from EN_AA.
 * 3. Clone detection — tests EN_AA/EN_CRC override behaviour.
 *
 * Includes a 20 ms power-on delay at the start to satisfy the nRF24L01+
 * power-on timing requirement (T1: 1.5–10.3 ms after power-on before
 * first SPI transaction is valid, per datasheet §6.1.2).
 *
 * @param radio      Driver instance (must NOT have been configured yet).
 * @param verbosity  Output verbosity level.
 * @return           Phase result. Warn if clone detected but SPI works.
 */
DiagPhaseResult verify_spi_comm(Driver &radio, DiagVerbosity verbosity)
{
    DiagPhaseResult result;
    result.status = DiagStatus::Pass;
    int64_t t_start = radio.hal().timestamp_us();

    if (verbosity >= DiagVerbosity::Summary) {
        printf("[diag:phase1] SPI_COMM starting\n");
    }

    /* ── Power-on delay (T1) ─────────────────────────────────────────────
     * Per nRF24L01+ datasheet §6.1.2 Table 9, the device needs 1.5–10.3 ms
     * after VCC is stable before SPI commands are accepted.  A 20 ms delay
     * provides comfortable margin. */
    radio.hal().delay_ms(20);

    /* ── Stage 1: SPI connectivity check ────────────────────────────────── */
    if (verbosity >= DiagVerbosity::Detailed) {
        printf("[diag:phase1] Stage 1: SPI connectivity check\n");
    }

    /* STATUS — if 0xFF, MISO is stuck high (no device on bus) */
    auto status_reg = radio.read_reg(Status{});
    uint8_t raw_status = status_reg.to_byte();
    bool spi_alive = (raw_status != 0xFF);
    bool status_is_por = (raw_status == Status::RESET_VALUE);

    if (verbosity >= DiagVerbosity::Verbose) {
        printf("[diag:phase1] STATUS exp=0x%02X got=0x%02X %s\n",
               Status::RESET_VALUE, raw_status,
               status_is_por ? "PASS" :
               (spi_alive ? "OK (IRQ flags set)" : "FAIL"));
    }

    if (!spi_alive) {
        result.status = DiagStatus::Fail;
        snprintf(result.detail, sizeof(result.detail),
                 "STATUS=0xFF — MISO stuck high, no device on bus");
        result.elapsed_us = static_cast<uint32_t>(radio.hal().timestamp_us() - t_start);
        print_phase(1, "SPI_COMM", result.status, result.detail);
        return result;
    }

    /* CONFIG — detect warm boot */
    auto actual_cfg = radio.read_reg(Config{});
    bool warm_boot = (actual_cfg.to_byte() != Config::RESET_VALUE);

    bool all_por_pass = true;

    if (warm_boot) {
        if (verbosity >= DiagVerbosity::Detailed) {
            printf("[diag:phase1] CONFIG exp=0x%02X got=0x%02X [WARM BOOT]\n",
                   Config::RESET_VALUE, actual_cfg.to_byte());
            printf("[diag:phase1] Module previously configured, skipping POR comparison\n");
        }
    } else {
        /* Cold boot — check remaining POR defaults */
        if (verbosity >= DiagVerbosity::Detailed) {
            printf("[diag:phase1] CONFIG exp=0x%02X got=0x%02X PASS\n",
                   Config::RESET_VALUE, actual_cfg.to_byte());
        }

        /* EN_AA — POR 0x3F */
        {
            auto actual = radio.read_reg(EnAa{});
            bool pass = (actual.to_byte() == EnAa::RESET_VALUE);
            if (verbosity >= DiagVerbosity::Verbose) {
                printf("[diag:phase1] EN_AA exp=0x%02X got=0x%02X %s\n",
                       EnAa::RESET_VALUE, actual.to_byte(),
                       pass ? "PASS" : "FAIL");
            }
            if (!pass) all_por_pass = false;
        }

        /* EN_RXADDR — POR 0x03 */
        {
            auto actual = radio.read_reg(EnRxAddr{});
            bool pass = (actual.to_byte() == EnRxAddr::RESET_VALUE);
            if (verbosity >= DiagVerbosity::Verbose) {
                printf("[diag:phase1] EN_RXADDR exp=0x%02X got=0x%02X %s\n",
                       EnRxAddr::RESET_VALUE, actual.to_byte(),
                       pass ? "PASS" : "FAIL");
            }
            if (!pass) all_por_pass = false;
        }

        /* SETUP_AW — POR 0x03 */
        {
            auto actual = radio.read_reg(SetupAw{});
            bool pass = (actual.to_byte() == SetupAw::RESET_VALUE);
            if (verbosity >= DiagVerbosity::Verbose) {
                printf("[diag:phase1] SETUP_AW exp=0x%02X got=0x%02X %s\n",
                       SetupAw::RESET_VALUE, actual.to_byte(),
                       pass ? "PASS" : "FAIL");
            }
            if (!pass) all_por_pass = false;
        }

        /* SETUP_RETR — POR 0x03 */
        {
            auto actual = radio.read_reg(SetupRetr{});
            bool pass = (actual.to_byte() == SetupRetr::RESET_VALUE);
            if (verbosity >= DiagVerbosity::Verbose) {
                printf("[diag:phase1] SETUP_RETR exp=0x%02X got=0x%02X %s\n",
                       SetupRetr::RESET_VALUE, actual.to_byte(),
                       pass ? "PASS" : "FAIL");
            }
            if (!pass) all_por_pass = false;
        }

        /* RF_CH — POR 0x02; bit 7 reserved */
        {
            auto actual = radio.read_reg(RfCh{});
            bool pass = (actual.to_byte() & REG_MASK_RF_CH) ==
                        (RfCh::RESET_VALUE & REG_MASK_RF_CH);
            if (verbosity >= DiagVerbosity::Verbose) {
                printf("[diag:phase1] RF_CH exp=0x%02X got=0x%02X %s\n",
                       RfCh::RESET_VALUE & REG_MASK_RF_CH,
                       actual.to_byte() & REG_MASK_RF_CH,
                       pass ? "PASS" : "FAIL");
            }
            if (!pass) all_por_pass = false;
        }

        /* RF_SETUP — POR 0x0E; bits 6,0 reserved */
        {
            auto actual = radio.read_reg(RfSetup{});
            bool pass = (actual.to_byte() & REG_MASK_RF_SETUP) ==
                        (RfSetup::RESET_VALUE & REG_MASK_RF_SETUP);
            if (verbosity >= DiagVerbosity::Verbose) {
                printf("[diag:phase1] RF_SETUP exp=0x%02X got=0x%02X %s\n",
                       RfSetup::RESET_VALUE & REG_MASK_RF_SETUP,
                       actual.to_byte() & REG_MASK_RF_SETUP,
                       pass ? "PASS" : "FAIL");
            }
            if (!pass) all_por_pass = false;
        }

        /* FIFO_STATUS — POR 0x11 */
        {
            auto actual = radio.read_reg(FifoStatus{});
            bool pass = (actual.to_byte() == FifoStatus::RESET_VALUE);
            if (verbosity >= DiagVerbosity::Verbose) {
                printf("[diag:phase1] FIFO_STATUS exp=0x%02X got=0x%02X %s\n",
                       FifoStatus::RESET_VALUE, actual.to_byte(),
                       pass ? "PASS" : "FAIL");
            }
            if (!pass) all_por_pass = false;
        }

        if (!all_por_pass) {
            result.status = DiagStatus::Fail;
            snprintf(result.detail, sizeof(result.detail),
                     "POR values do not match datasheet (cold boot)");
            result.elapsed_us = static_cast<uint32_t>(radio.hal().timestamp_us() - t_start);
            print_phase(1, "SPI_COMM", result.status, result.detail);
            return result;
        }
    }

    /* ── Stage 2: Write-verify test ────────────────────────────────────── */
    if (verbosity >= DiagVerbosity::Detailed) {
        printf("[diag:phase1] Stage 2: Write-verify test (EN_AA)\n");
    }

    /* EN_AA has 6 valid bits [5:0]; bits [7:6] are reserved (§9.1). */
    static constexpr uint8_t EN_AA_VALID_MASK = 0x3F;
    static constexpr uint8_t write_patterns[] = {0x00, 0x3F, 0x55, 0x2A};

    bool writes_ok = true;
    for (uint8_t pattern : write_patterns) {
        EnAa en_aa = EnAa::from_byte(pattern);
        radio.write_reg(en_aa);
        uint8_t readback = radio.read_reg(EnAa{}).to_byte();
        /* Mask both sides to valid bits */
        bool match = (readback & EN_AA_VALID_MASK) == (pattern & EN_AA_VALID_MASK);

        if (verbosity >= DiagVerbosity::Verbose) {
            printf("[diag:phase1] write EN_AA=0x%02X read=0x%02X %s\n",
                   pattern, readback, match ? "PASS" : "FAIL");
        }
        if (!match) writes_ok = false;
    }

    if (!writes_ok) {
        result.status = DiagStatus::Fail;
        snprintf(result.detail, sizeof(result.detail),
                 "Register writes not accepted (EN_AA write-verify failed)");
        /* Restore EN_AA to POR default before returning */
        EnAa por_aa = EnAa::from_byte(EnAa::RESET_VALUE);
        radio.write_reg(por_aa);
        result.elapsed_us = static_cast<uint32_t>(radio.hal().timestamp_us() - t_start);
        print_phase(1, "SPI_COMM", result.status, result.detail);
        return result;
    }

    if (verbosity >= DiagVerbosity::Detailed) {
        printf("[diag:phase1] Stage 2: PASS — all write patterns verified\n");
    }

    /* ── Stage 3: Clone chip detection ──────────────────────────────────── */
    if (verbosity >= DiagVerbosity::Detailed) {
        printf("[diag:phase1] Stage 3: Clone chip detection (EN_AA/EN_CRC override)\n");
    }

    /* Set EN_AA = 0x00 explicitly (was 0x2A from last write test) */
    EnAa en_aa_zero;
    for (int i = 0; i < 6; ++i) en_aa_zero.pipe[i] = false;
    radio.write_reg(en_aa_zero);

    /* Write CONFIG with EN_CRC=0, PWR_UP=1, PRIM_RX=1 (value 0x03) */
    Config test_cfg;
    test_cfg.power_mode   = PowerMode::Up;
    test_cfg.primary      = PrimaryMode::RX;
    test_cfg.crc_mode     = CrcMode::Disabled;
    test_cfg.crc_encoding = CrcEncoding::Bytes1;
    test_cfg.irq_mask     = IrqMask::None;
    radio.write_reg(test_cfg);

    /* Small delay to let the register settle (some clones need this) */
    radio.hal().delay_ms(1);

    /* Read back CONFIG and check EN_CRC */
    Config readback_cfg = radio.read_reg(Config{});
    bool clone_detected = (readback_cfg.crc_mode != CrcMode::Disabled);

    if (verbosity >= DiagVerbosity::Detailed) {
        printf("[diag:phase1] EN_AA=0x00, wrote CONFIG=0x%02X (EN_CRC=0), "
               "read CONFIG=0x%02X\n",
               test_cfg.to_byte(), readback_cfg.to_byte());
        if (clone_detected) {
            printf("[diag:phase1] EN_CRC forced ON despite EN_AA=0 — CLONE CHIP\n");
        } else {
            printf("[diag:phase1] EN_CRC accepted as 0 — genuine nRF24L01+\n");
        }
    }

    /* Restore CONFIG to POR value (power-down state).
     * configure_rx() will re-write it properly. */
    Config por_cfg = Config::from_byte(Config::RESET_VALUE);
    radio.write_reg(por_cfg);

    /* Leave EN_AA = 0x00 (configure_rx() expects/needs this) */
    if (verbosity >= DiagVerbosity::Verbose) {
        printf("[diag:phase1] EN_AA left at 0x00 (compatible with configure_rx)\n");
    }

    /* Build result */
    if (clone_detected) {
        result.status = DiagStatus::Warn;
        snprintf(result.detail, sizeof(result.detail),
                 "SPI OK, clone chip detected (EN_CRC forced on)");
    } else {
        snprintf(result.detail, sizeof(result.detail),
                 "SPI OK, genuine nRF24L01+");
    }

    result.elapsed_us = static_cast<uint32_t>(radio.hal().timestamp_us() - t_start);

    if (verbosity >= DiagVerbosity::Summary) {
        print_phase(1, "SPI_COMM", result.status, result.detail);
    }

    return result;
}

/* ════════════════════════════════════════════════════════════════════════════
 * Phase 2: BLE Configuration Verification
 *
 * Calls configure_rx() and verifies all register values match the
 * expected BLE configuration. Uses INDEPENDENTLY DERIVED address
 * verification (not the ADV_ACCESS_ADDR constant).
 * ════════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Phase 2: Verify BLE RX configuration.
 *
 * Calls nrf24::ble::configure_rx() and verifies that all registers
 * were programmed correctly.  Checks CONFIG (EN_CRC must be 0 for BLE),
 * EN_AA, EN_RXADDR, SETUP_AW, SETUP_RETR, RF_CH, RF_SETUP, RX_PW_P0,
 * DYNPD, FEATURE, RX_ADDR_P0, and TX_ADDR.
 *
 * Address verification uses INDEPENDENTLY DERIVED expected bytes from
 * the BLE advertising access address 0x8E89BED6, not the ADV_ACCESS_ADDR
 * constant, to avoid self-consistent errors.
 *
 * @param radio      Driver instance (must have passed verify_spi_comm).
 * @param config     BLE RX configuration to apply and verify.
 * @param verbosity  Output verbosity level.
 * @return           Phase result. Fail if any register mismatches.
 */
DiagPhaseResult verify_ble_config(Driver &radio,
                                  const ble::RxConfig &config,
                                  DiagVerbosity verbosity)
{
    DiagPhaseResult result;
    result.status = DiagStatus::Pass;
    int64_t t_start = radio.hal().timestamp_us();

    if (verbosity >= DiagVerbosity::Summary) {
        printf("[diag:phase2] BLE_CONFIG starting\n");
    }

    /* ── Configure the device ──────────────────────────────────────────
     * configure_rx() writes EN_AA=0x00 BEFORE CONFIG (critical ordering). */
    ble::configure_rx(radio, config);

    /* ── Build expected register values ────────────────────────────────── */
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

    /* ── Independently derive expected SPI address bytes ──────────────────
     * From BLE advertising access address 0x8E89BED6, we bit-swap each
     * byte (BLE LSBit-first → nRF24 MSBit-first) and arrange in LSByte-first
     * SPI write order. This is INDEPENDENT of the ADV_ACCESS_ADDR constant
     * used for writing, so self-consistent byte-order errors are caught.
     *
     * Derivation chain:
     *   BLE AA: 0x8E89BED6 (MSByte first)
     *   On-air LSByte first: D6 BE 89 8E
     *   Per-byte bit-swap: swapbits(0xD6)=0x6B, swapbits(0xBE)=0x7D,
     *                      swapbits(0x89)=0x91, swapbits(0x8E)=0x71
     *   nRF24 SPI LSByte-first order: 71 91 7D 6B */
    static constexpr uint32_t BLE_ADV_AA = 0x8E89BED6;
    uint8_t expected_addr[4];
    expected_addr[0] = ble::swapbits(static_cast<uint8_t>(BLE_ADV_AA >> 24));  /* 0x71 */
    expected_addr[1] = ble::swapbits(static_cast<uint8_t>(BLE_ADV_AA >> 16));  /* 0x91 */
    expected_addr[2] = ble::swapbits(static_cast<uint8_t>(BLE_ADV_AA >> 8));   /* 0x7D */
    expected_addr[3] = ble::swapbits(static_cast<uint8_t>(BLE_ADV_AA));        /* 0x6B */

    /* ── Verify each register ─────────────────────────────────────────── */
    bool all_pass = true;
    char fail_detail[128] = {};
    int fail_pos = 0;

    /* CONFIG — CRITICAL: EN_CRC must be 0 for BLE */
    {
        auto actual = radio.read_reg(Config{});
        bool pass = (actual.to_byte() & REG_MASK_ALL) == (exp_config.to_byte() & REG_MASK_ALL);
        if (verbosity >= DiagVerbosity::Detailed) {
            printf("[diag:phase2] %-10s exp=0x%02X got=0x%02X %s%s\n",
                   "CONFIG", exp_config.to_byte() & REG_MASK_ALL,
                   actual.to_byte() & REG_MASK_ALL, pass ? "PASS" : "FAIL",
                   actual.crc_mode != CrcMode::Disabled ?
                       " *** EN_CRC is ON — BLE packets rejected!" : "");
        }
        if (!pass) {
            all_pass = false;
            fail_pos += snprintf(fail_detail + fail_pos,
                                  sizeof(fail_detail) - fail_pos,
                                  "CONFIG mismatch (exp=0x%02X got=0x%02X); ",
                                  exp_config.to_byte(), actual.to_byte());
        }
    }

    /* EN_AA — must be 0x00 or EN_CRC is forced on */
    {
        auto actual = radio.read_reg(EnAa{});
        bool pass = (actual.to_byte() & REG_MASK_ALL) == (exp_en_aa.to_byte() & REG_MASK_ALL);
        if (verbosity >= DiagVerbosity::Detailed) {
            printf("[diag:phase2] %-10s exp=0x%02X got=0x%02X %s%s\n",
                   "EN_AA", exp_en_aa.to_byte() & REG_MASK_ALL,
                   actual.to_byte() & REG_MASK_ALL, pass ? "PASS" : "FAIL",
                   actual.to_byte() != 0x00 ?
                       " *** non-zero EN_AA forces EN_CRC on!" : "");
        }
        if (!pass) {
            all_pass = false;
            fail_pos += snprintf(fail_detail + fail_pos,
                                  sizeof(fail_detail) - fail_pos,
                                  "EN_AA=0x%02X (expected 0x00); ",
                                  actual.to_byte());
        }
    }

    /* EN_RXADDR */
    {
        auto actual = radio.read_reg(EnRxAddr{});
        bool pass = (actual.to_byte() & REG_MASK_ALL) == (exp_en_rx.to_byte() & REG_MASK_ALL);
        if (verbosity >= DiagVerbosity::Detailed) {
            printf("[diag:phase2] %-10s exp=0x%02X got=0x%02X %s\n",
                   "EN_RXADDR", exp_en_rx.to_byte() & REG_MASK_ALL,
                   actual.to_byte() & REG_MASK_ALL, pass ? "PASS" : "FAIL");
        }
        if (!pass) {
            all_pass = false;
            fail_pos += snprintf(fail_detail + fail_pos,
                                  sizeof(fail_detail) - fail_pos,
                                  "EN_RXADDR mismatch; ");
        }
    }

    /* SETUP_AW */
    {
        auto actual = radio.read_reg(SetupAw{});
        bool pass = (actual.to_byte() & REG_MASK_ALL) == (exp_aw.to_byte() & REG_MASK_ALL);
        if (verbosity >= DiagVerbosity::Detailed) {
            printf("[diag:phase2] %-10s exp=0x%02X got=0x%02X %s\n",
                   "SETUP_AW", exp_aw.to_byte() & REG_MASK_ALL,
                   actual.to_byte() & REG_MASK_ALL, pass ? "PASS" : "FAIL");
        }
        if (!pass) {
            all_pass = false;
            fail_pos += snprintf(fail_detail + fail_pos,
                                  sizeof(fail_detail) - fail_pos,
                                  "SETUP_AW mismatch; ");
        }
    }

    /* SETUP_RETR */
    {
        auto actual = radio.read_reg(SetupRetr{});
        bool pass = (actual.to_byte() & REG_MASK_ALL) == (exp_retr.to_byte() & REG_MASK_ALL);
        if (verbosity >= DiagVerbosity::Detailed) {
            printf("[diag:phase2] %-10s exp=0x%02X got=0x%02X %s\n",
                   "SETUP_RETR", exp_retr.to_byte() & REG_MASK_ALL,
                   actual.to_byte() & REG_MASK_ALL, pass ? "PASS" : "FAIL");
        }
        if (!pass) {
            all_pass = false;
            fail_pos += snprintf(fail_detail + fail_pos,
                                  sizeof(fail_detail) - fail_pos,
                                  "SETUP_RETR mismatch; ");
        }
    }

    /* RF_CH — bit 7 reserved */
    {
        auto actual = radio.read_reg(RfCh{});
        bool pass = (actual.to_byte() & REG_MASK_RF_CH) == (exp_rf_ch.to_byte() & REG_MASK_RF_CH);
        if (verbosity >= DiagVerbosity::Detailed) {
            printf("[diag:phase2] %-10s exp=0x%02X got=0x%02X %s\n",
                   "RF_CH", exp_rf_ch.to_byte() & REG_MASK_RF_CH,
                   actual.to_byte() & REG_MASK_RF_CH, pass ? "PASS" : "FAIL");
        }
        if (!pass) {
            all_pass = false;
            fail_pos += snprintf(fail_detail + fail_pos,
                                  sizeof(fail_detail) - fail_pos,
                                  "RF_CH mismatch; ");
        }
    }

    /* RF_SETUP — bits 6,0 reserved */
    {
        auto actual = radio.read_reg(RfSetup{});
        bool pass = (actual.to_byte() & REG_MASK_RF_SETUP) == (exp_rf.to_byte() & REG_MASK_RF_SETUP);
        if (verbosity >= DiagVerbosity::Detailed) {
            printf("[diag:phase2] %-10s exp=0x%02X got=0x%02X %s%s\n",
                   "RF_SETUP", exp_rf.to_byte() & REG_MASK_RF_SETUP,
                   actual.to_byte() & REG_MASK_RF_SETUP, pass ? "PASS" : "FAIL",
                   actual.data_rate != DataRate::Mbps1 ? " *** not 1 Mbps!" : "");
        }
        if (!pass) {
            all_pass = false;
            fail_pos += snprintf(fail_detail + fail_pos,
                                  sizeof(fail_detail) - fail_pos,
                                  "RF_SETUP mismatch; ");
        }
    }

    /* RX_PW_P0 — bits 7:6 reserved */
    {
        RxPwP0 pw0_tag;
        auto actual = radio.read_reg(pw0_tag);
        bool pass = (actual.to_byte() & REG_MASK_RX_PW) == (exp_pw.to_byte() & REG_MASK_RX_PW);
        if (verbosity >= DiagVerbosity::Detailed) {
            printf("[diag:phase2] %-10s exp=0x%02X got=0x%02X %s\n",
                   "RX_PW_P0", exp_pw.to_byte() & REG_MASK_RX_PW,
                   actual.to_byte() & REG_MASK_RX_PW, pass ? "PASS" : "FAIL");
        }
        if (!pass) {
            all_pass = false;
            fail_pos += snprintf(fail_detail + fail_pos,
                                  sizeof(fail_detail) - fail_pos,
                                  "RX_PW_P0 mismatch; ");
        }
    }

    /* DYNPD — must be 0x00 */
    {
        auto actual = radio.read_reg(Dynpd{});
        bool pass = actual.to_byte() == 0x00;
        if (verbosity >= DiagVerbosity::Detailed) {
            printf("[diag:phase2] %-10s exp=0x00 got=0x%02X %s\n",
                   "DYNPD", actual.to_byte(), pass ? "PASS" : "FAIL");
        }
        if (!pass) {
            all_pass = false;
            fail_pos += snprintf(fail_detail + fail_pos,
                                  sizeof(fail_detail) - fail_pos,
                                  "DYNPD=0x%02X (expected 0x00); ",
                                  actual.to_byte());
        }
    }

    /* FEATURE — must be 0x00 */
    {
        auto actual = radio.read_reg(Feature{});
        bool pass = actual.to_byte() == 0x00;
        if (verbosity >= DiagVerbosity::Detailed) {
            printf("[diag:phase2] %-10s exp=0x00 got=0x%02X %s\n",
                   "FEATURE", actual.to_byte(), pass ? "PASS" : "FAIL");
        }
        if (!pass) {
            all_pass = false;
            fail_pos += snprintf(fail_detail + fail_pos,
                                  sizeof(fail_detail) - fail_pos,
                                  "FEATURE=0x%02X (expected 0x00); ",
                                  actual.to_byte());
        }
    }

    /* FIFO_STATUS — read-only, display for info */
    if (verbosity >= DiagVerbosity::Verbose) {
        auto actual = radio.read_reg(FifoStatus{});
        printf("[diag:phase2] %-10s rx_empty=%u rx_full=%u (raw 0x%02X)\n",
               "FIFO_STATUS",
               static_cast<unsigned>(actual.rx_empty),
               static_cast<unsigned>(actual.rx_full),
               actual.to_byte());
    }

    /* RX_ADDR_P0 — INDEPENDENTLY verified against derived BLE AA */
    {
        uint8_t addr[4];
        radio.read_reg_multi(reg::RX_ADDR_P0, addr, 4);
        bool rx_ok = (memcmp(addr, expected_addr, 4) == 0);
        if (verbosity >= DiagVerbosity::Detailed) {
            printf("[diag:phase2] %-10s exp=%02X:%02X:%02X:%02X got=%02X:%02X:%02X:%02X %s\n",
                   "RX_ADDR_P0",
                   expected_addr[0], expected_addr[1],
                   expected_addr[2], expected_addr[3],
                   addr[0], addr[1], addr[2], addr[3],
                   rx_ok ? "PASS" : "FAIL");
        }
        if (!rx_ok) {
            all_pass = false;
            fail_pos += snprintf(fail_detail + fail_pos,
                                  sizeof(fail_detail) - fail_pos,
                                  "RX_ADDR_P0 mismatch "
                                  "(exp=%02X:%02X:%02X:%02X got=%02X:%02X:%02X:%02X); ",
                                  expected_addr[0], expected_addr[1],
                                  expected_addr[2], expected_addr[3],
                                  addr[0], addr[1], addr[2], addr[3]);
        }
    }

    /* TX_ADDR — INDEPENDENTLY verified against derived BLE AA */
    {
        uint8_t addr[4];
        radio.read_reg_multi(reg::TX_ADDR, addr, 4);
        bool tx_ok = (memcmp(addr, expected_addr, 4) == 0);
        if (verbosity >= DiagVerbosity::Detailed) {
            printf("[diag:phase2] %-10s exp=%02X:%02X:%02X:%02X got=%02X:%02X:%02X:%02X %s\n",
                   "TX_ADDR",
                   expected_addr[0], expected_addr[1],
                   expected_addr[2], expected_addr[3],
                   addr[0], addr[1], addr[2], addr[3],
                   tx_ok ? "PASS" : "FAIL");
        }
        if (!tx_ok) {
            all_pass = false;
            fail_pos += snprintf(fail_detail + fail_pos,
                                  sizeof(fail_detail) - fail_pos,
                                  "TX_ADDR mismatch "
                                  "(exp=%02X:%02X:%02X:%02X got=%02X:%02X:%02X:%02X); ",
                                  expected_addr[0], expected_addr[1],
                                  expected_addr[2], expected_addr[3],
                                  addr[0], addr[1], addr[2], addr[3]);
        }
    }

    /* Build result */
    if (all_pass) {
        snprintf(result.detail, sizeof(result.detail),
                 "All BLE registers verified");
    } else {
        result.status = DiagStatus::Fail;
        snprintf(result.detail, sizeof(result.detail), "%s", fail_detail);
    }

    result.elapsed_us = static_cast<uint32_t>(radio.hal().timestamp_us() - t_start);

    if (verbosity >= DiagVerbosity::Summary) {
        print_phase(2, "BLE_CONFIG", result.status, result.detail);
    }

    return result;
}

/* ════════════════════════════════════════════════════════════════════════════
 * Phase 3: Post-Configuration Verification
 *
 * Reads back a subset of critical registers after configuration and
 * performs cross-checks to catch inconsistencies that per-register checks
 * might miss.
 * ════════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Phase 3: Post-configuration register cross-check.
 *
 * Reads critical registers after BLE configuration and verifies
 * cross-invariants (e.g. EN_AA must be 0 and CONFIG.EN_CRC must be 0
 * at the same time; FIFO_STATUS must show empty TX FIFO).
 *
 * @param radio      Driver instance (must be configured for BLE RX).
 * @param verbosity  Output verbosity level.
 * @return           Phase result. Fail if cross-check invariants violated.
 */
static DiagPhaseResult verify_post_config(Driver &radio, DiagVerbosity verbosity)
{
    DiagPhaseResult result;
    result.status = DiagStatus::Pass;
    int64_t t_start = radio.hal().timestamp_us();

    if (verbosity >= DiagVerbosity::Summary) {
        printf("[diag:phase3] POST_CONFIG starting\n");
    }

    /* Cross-check 1: EN_AA=0 AND EN_CRC=0 (critical for BLE) */
    auto cfg = radio.read_reg(Config{});
    auto en_aa = radio.read_reg(EnAa{});

    bool crc_ok = (cfg.crc_mode == CrcMode::Disabled);
    bool enaa_ok = (en_aa.to_byte() == 0x00);
    bool cross_ok = crc_ok && enaa_ok;

    if (verbosity >= DiagVerbosity::Detailed) {
        printf("[diag:phase3] EN_CRC=%u (expected 0), EN_AA=0x%02X (expected 0x00) %s\n",
               static_cast<unsigned>(cfg.crc_mode),
               en_aa.to_byte(),
               cross_ok ? "PASS" : "FAIL");
    }

    if (!cross_ok) {
        result.status = DiagStatus::Fail;
        if (!crc_ok && !enaa_ok) {
            snprintf(result.detail, sizeof(result.detail),
                     "EN_CRC=1 AND EN_AA=0x%02X — CRC forced on by EN_AA",
                     en_aa.to_byte());
        } else if (!crc_ok) {
            snprintf(result.detail, sizeof(result.detail),
                     "EN_CRC=1 despite EN_AA=0x00 — possible clone chip or write failure");
        } else {
            snprintf(result.detail, sizeof(result.detail),
                     "EN_AA=0x%02X (expected 0x00) — will force EN_CRC on",
                     en_aa.to_byte());
        }
    } else {
        snprintf(result.detail, sizeof(result.detail),
                 "Post-config cross-checks passed (EN_CRC=0, EN_AA=0)");
    }

    /* Cross-check 2: FIFO STATUS — TX FIFO should be empty */
    auto fifo = radio.read_reg(FifoStatus{});
    if (!fifo.tx_empty) {
        if (verbosity >= DiagVerbosity::Detailed) {
            printf("[diag:phase3] WARNING: TX FIFO not empty after configuration\n");
        }
        /* Warning, not failure — TX FIFO may have stale data */
    }

    result.elapsed_us = static_cast<uint32_t>(radio.hal().timestamp_us() - t_start);

    if (verbosity >= DiagVerbosity::Summary) {
        print_phase(3, "POST_CONFIG", result.status, result.detail);
    }

    return result;
}

/* ════════════════════════════════════════════════════════════════════════════
 * Full Boot Diagnostic Orchestrator
 *
 * Runs all six phases in order.  If a phase returns Fail, subsequent
 * phases are marked Skip.  If a phase returns Fail and retry_count > 0,
 * the orchestrator sleeps for retry_delay_ms and re-runs from HalInit.
 * After max retries exhausted, returns the DiagFullResult — the caller
 * decides what to do (deep sleep, restart, etc.).
 * ════════════════════════════════════════════════════════════════════════════ */

DiagFullResult full_boot_diagnostic(Driver &radio,
                                     const ble::RxConfig &config,
                                     const DiagOpts &opts)
{
    DiagFullResult result;
    uint8_t attempts = 0;
    const uint8_t max_attempts = opts.retry_count + 1;

    while (attempts < max_attempts) {
        attempts++;

        if (opts.verbosity >= DiagVerbosity::Summary) {
            printf("[diag] === Boot diagnostic attempt %u/%u ===\n",
                   static_cast<unsigned>(attempts),
                   static_cast<unsigned>(max_attempts));
        }

        bool any_fail = false;

        /* Phase 0: HalInit — CE GPIO readback */
        result.phases[static_cast<uint8_t>(DiagPhase::HalInit)] =
            verify_hal_ce(radio, opts.verbosity);
        if (!result.phases[static_cast<uint8_t>(DiagPhase::HalInit)].ok()) {
            any_fail = true;
        }

        /* Phase 1: SpiComm — SPI communication, POR, write-verify, clone detect */
        if (!any_fail) {
            result.phases[static_cast<uint8_t>(DiagPhase::SpiComm)] =
                verify_spi_comm(radio, opts.verbosity);
            any_fail = !result.phases[static_cast<uint8_t>(DiagPhase::SpiComm)].ok();
        } else {
            result.phases[static_cast<uint8_t>(DiagPhase::SpiComm)].status = DiagStatus::Skip;
            snprintf(result.phases[static_cast<uint8_t>(DiagPhase::SpiComm)].detail,
                     sizeof(result.phases[static_cast<uint8_t>(DiagPhase::SpiComm)].detail),
                     "Skipped: prerequisite failed");
        }

        /* Track clone detection from SpiComm phase */
        if (result.phases[static_cast<uint8_t>(DiagPhase::SpiComm)].status == DiagStatus::Warn) {
            /* Check if the warn was about clone detection */
            auto &detail = result.phases[static_cast<uint8_t>(DiagPhase::SpiComm)].detail;
            if (strstr(detail, "clone") != nullptr) {
                result.clone_detected = true;
            }
        }

        /* Track warm boot from SpiComm phase */
        if (result.phases[static_cast<uint8_t>(DiagPhase::SpiComm)].status == DiagStatus::Pass ||
            result.phases[static_cast<uint8_t>(DiagPhase::SpiComm)].status == DiagStatus::Warn) {
            /* Check CONFIG to detect warm boot */
            auto cfg = radio.read_reg(Config{});
            if (cfg.to_byte() != Config::RESET_VALUE) {
                result.warm_boot = true;
            }
        }

        /* Phase 2: BleConfig — configure RX and verify registers */
        if (!any_fail) {
            result.phases[static_cast<uint8_t>(DiagPhase::BleConfig)] =
                verify_ble_config(radio, config, opts.verbosity);
            any_fail = !result.phases[static_cast<uint8_t>(DiagPhase::BleConfig)].ok();
        } else {
            result.phases[static_cast<uint8_t>(DiagPhase::BleConfig)].status = DiagStatus::Skip;
            snprintf(result.phases[static_cast<uint8_t>(DiagPhase::BleConfig)].detail,
                     sizeof(result.phases[static_cast<uint8_t>(DiagPhase::BleConfig)].detail),
                     "Skipped: prerequisite failed");
        }

        /* Phase 3: PostConfig — register cross-checks */
        if (!any_fail) {
            result.phases[static_cast<uint8_t>(DiagPhase::PostConfig)] =
                verify_post_config(radio, opts.verbosity);
            any_fail = !result.phases[static_cast<uint8_t>(DiagPhase::PostConfig)].ok();
        } else {
            result.phases[static_cast<uint8_t>(DiagPhase::PostConfig)].status = DiagStatus::Skip;
            snprintf(result.phases[static_cast<uint8_t>(DiagPhase::PostConfig)].detail,
                     sizeof(result.phases[static_cast<uint8_t>(DiagPhase::PostConfig)].detail),
                     "Skipped: prerequisite failed");
        }

        /* Phase 4: CeState — CE/CONFIG/FIFO cross-check */
        if (!any_fail) {
            result.phases[static_cast<uint8_t>(DiagPhase::CeState)] =
                verify_ce_state(radio, opts.verbosity);
            any_fail = !result.phases[static_cast<uint8_t>(DiagPhase::CeState)].ok();
        } else {
            result.phases[static_cast<uint8_t>(DiagPhase::CeState)].status = DiagStatus::Skip;
            snprintf(result.phases[static_cast<uint8_t>(DiagPhase::CeState)].detail,
                     sizeof(result.phases[static_cast<uint8_t>(DiagPhase::CeState)].detail),
                     "Skipped: prerequisite failed");
        }

        /* Phase 5: Extended — CE-high RPD/FIFO poll test */
        if (!any_fail) {
            result.phases[static_cast<uint8_t>(DiagPhase::Extended)] =
                verify_extended_ce(radio,
                                    opts.extended_test_ch,
                                    opts.extended_poll_ms,
                                    opts.extended_duration_s,
                                    opts.verbosity);
            any_fail = !result.phases[static_cast<uint8_t>(DiagPhase::Extended)].ok();
        } else {
            result.phases[static_cast<uint8_t>(DiagPhase::Extended)].status = DiagStatus::Skip;
            snprintf(result.phases[static_cast<uint8_t>(DiagPhase::Extended)].detail,
                     sizeof(result.phases[static_cast<uint8_t>(DiagPhase::Extended)].detail),
                     "Skipped: prerequisite failed");
        }

        /* If all phases passed (including Warn), we're done */
        if (!any_fail) {
            if (opts.verbosity >= DiagVerbosity::Summary) {
                printf("[diag] === Boot diagnostic PASSED (attempt %u/%u) ===\n",
                       static_cast<unsigned>(attempts),
                       static_cast<unsigned>(max_attempts));
            }
            return result;
        }

        /* If we have retries left, sleep and retry */
        if (attempts < max_attempts) {
            if (opts.verbosity >= DiagVerbosity::Summary) {
                printf("[diag] Attempt %u failed, retrying in %u ms...\n",
                       static_cast<unsigned>(attempts),
                       static_cast<unsigned>(opts.retry_delay_ms));
            }
            radio.hal().sleep_ms(opts.retry_delay_ms);
            /* Reset all phases for the next attempt */
            for (uint8_t i = 0; i < DIAG_PHASE_COUNT; ++i) {
                result.phases[i] = DiagPhaseResult{};
            }
        }
    }

    /* All retries exhausted */
    if (opts.verbosity >= DiagVerbosity::Summary) {
        printf("[diag] === Boot diagnostic FAILED after %u attempts ===\n",
               static_cast<unsigned>(max_attempts));
    }

    return result;
}

} // namespace diag
} // namespace nrf24