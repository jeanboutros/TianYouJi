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

/** @brief Mask for relevant bits of RF_SETUP (bit 6 and bit 0 are reserved/obsolete). */
static constexpr uint8_t REG_MASK_RF_SETUP = 0xBE;

bool spi_comm_test(Driver &radio)
{
    printf("\n=== SPI Communication Test ===\n");
    bool all_pass = true;

    /* ── Stage 1: Power-on reset (POR) value check ─────────────────────
     *
     * Read registers in their default state and compare against datasheet
     * POR values.  If these don't match, the SPI bus is not working.
     * Per nRF24L01+ Product Specification v1.0, Table 28 — Register Map.
     *
     * We read STATUS first because it is the most reliable indicator:
     * the nRF24L01+ returns STATUS as the first byte of EVERY SPI
     * transaction (MISO during the command phase).  If we see 0xFF,
     * MISO is stuck high (no device, or wiring broken).  If we see
     * the expected 0x0E, SPI is definitely working. */
    printf("Stage 1: Power-on reset register reads\n");

    /* STATUS — POR value 0x0E (RX_P_NO=111=empty, no IRQ flags, TX not full).
     * This is the single most definitive test: if STATUS reads correctly,
     * the SPI bus is working and the device is alive. */
    {
        auto actual = radio.read_reg(Status{});
        uint8_t raw = actual.to_byte();
        bool pass = (raw == Status::RESET_VALUE);
        printf("  STATUS     exp=0x%02X  got=0x%02X  [%s]%s\n",
               Status::RESET_VALUE, raw, pass ? "PASS" : "FAIL",
               (raw == 0xFF) ? "  *** MISO stuck high — no device or bad wiring" : "");
        if (!pass) {
            printf("  SPI communication FAILED — cannot proceed.\n");
            printf("  Possible causes:\n");
            printf("    - MISO/MOSI/SCLK/CSN wiring incorrect\n");
            printf("    - nRF24 module not powered (3.3 V)\n");
            printf("    - SPI clock too fast (reduced to 1 MHz in this build)\n");
            printf("    - Module is dead or not an nRF24-compatible device\n\n");
            return false;
        }
    }

    /* CONFIG — POR value 0x08 (EN_CRC=1, CRCO=0, PWR_UP=0, PRIM_RX=0) */
    {
        auto actual = radio.read_reg(Config{});
        bool pass = (actual.to_byte() == Config::RESET_VALUE);
        printf("  CONFIG     exp=0x%02X  got=0x%02X  [%s]\n",
               Config::RESET_VALUE, actual.to_byte(), pass ? "PASS" : "FAIL");
        if (!pass) all_pass = false;
    }

    /* EN_AA — POR value 0x3F (all pipes auto-ACK enabled) */
    {
        auto actual = radio.read_reg(EnAa{});
        bool pass = (actual.to_byte() == EnAa::RESET_VALUE);
        printf("  EN_AA      exp=0x%02X  got=0x%02X  [%s]\n",
               EnAa::RESET_VALUE, actual.to_byte(), pass ? "PASS" : "FAIL");
        if (!pass) all_pass = false;
    }

    /* EN_RXADDR — POR value 0x03 (pipes 0 and 1 enabled) */
    {
        auto actual = radio.read_reg(EnRxAddr{});
        bool pass = (actual.to_byte() == EnRxAddr::RESET_VALUE);
        printf("  EN_RXADDR  exp=0x%02X  got=0x%02X  [%s]\n",
               EnRxAddr::RESET_VALUE, actual.to_byte(), pass ? "PASS" : "FAIL");
        if (!pass) all_pass = false;
    }

    /* SETUP_AW — POR value 0x03 (5-byte address width) */
    {
        auto actual = radio.read_reg(SetupAw{});
        bool pass = (actual.to_byte() == SetupAw::RESET_VALUE);
        printf("  SETUP_AW   exp=0x%02X  got=0x%02X  [%s]\n",
               SetupAw::RESET_VALUE, actual.to_byte(), pass ? "PASS" : "FAIL");
        if (!pass) all_pass = false;
    }

    /* SETUP_RETR — POR value 0x03 (250 µs delay, 3 retransmits) */
    {
        auto actual = radio.read_reg(SetupRetr{});
        bool pass = (actual.to_byte() == SetupRetr::RESET_VALUE);
        printf("  SETUP_RETR exp=0x%02X  got=0x%02X  [%s]\n",
               SetupRetr::RESET_VALUE, actual.to_byte(), pass ? "PASS" : "FAIL");
        if (!pass) all_pass = false;
    }

    /* RF_CH — POR value 0x00 (channel 0 = 2400 MHz); bit 7 is reserved */
    {
        auto actual = radio.read_reg(RfCh{});
        bool pass = (actual.to_byte() & REG_MASK_RF_CH) == 0x00;
        printf("  RF_CH      exp=0x00  got=0x%02X  [%s]\n",
               actual.to_byte() & REG_MASK_RF_CH, pass ? "PASS" : "FAIL");
        if (!pass) all_pass = false;
    }

    /* RF_SETUP — POR value 0x0E (2 Mbps, 0 dBm); bits 6 and 0 reserved */
    {
        auto actual = radio.read_reg(RfSetup{});
        bool pass = (actual.to_byte() & REG_MASK_RF_SETUP) == (RfSetup::RESET_VALUE & REG_MASK_RF_SETUP);
        printf("  RF_SETUP   exp=0x%02X  got=0x%02X  [%s]\n",
               RfSetup::RESET_VALUE & REG_MASK_RF_SETUP,
               actual.to_byte() & REG_MASK_RF_SETUP, pass ? "PASS" : "FAIL");
        if (!pass) all_pass = false;
    }

    /* FIFO_STATUS — POR value 0x11 (TX_EMPTY=1, RX_EMPTY=1); read-only but
     * value should be consistent at power-on. */
    {
        auto actual = radio.read_reg(FifoStatus{});
        bool pass = (actual.to_byte() == FifoStatus::RESET_VALUE);
        printf("  FIFO_STAT  exp=0x%02X  got=0x%02X  [%s]\n",
               FifoStatus::RESET_VALUE, actual.to_byte(), pass ? "PASS" : "FAIL");
        if (!pass) all_pass = false;
    }

    if (!all_pass) {
        printf("\n  Stage 1 FAILED — POR values do not match.\n");
        printf("  SPI reads are unreliable; cannot proceed to write test.\n\n");
        return false;
    }

    printf("  Stage 1 PASS — all POR values match datasheet.\n");

    /* ── Stage 2: Write-verify test ──────────────────────────────────────
     *
     * Write known patterns to EN_AA and read each back.  EN_AA is chosen
     * because it is a simple 6-bit register with no side effects and no
     * dependency on other register state.
     *
     * We test four patterns:
     *   0x00 — all pipes disabled (important for BLE CRC=0)
     *   0x3F — all pipes enabled (POR default)
     *   0x55 — alternating 0/1 pattern
     *   0x2A — complementary alternating pattern
     *
     * If all four writes verify, SPI writes are confirmed working. */
    printf("\nStage 2: Write-verify test (EN_AA register)\n");

    static constexpr uint8_t write_patterns[] = {0x00, 0x3F, 0x55, 0x2A};
    bool writes_ok = true;
    for (uint8_t pattern : write_patterns) {
        EnAa en_aa = EnAa::from_byte(pattern);
        radio.write_reg(en_aa);
        uint8_t readback = radio.read_reg(EnAa{}).to_byte();
        bool match = (readback == pattern);
        printf("  write EN_AA=0x%02X  read=0x%02X  [%s]\n",
               pattern, readback, match ? "PASS" : "FAIL");
        if (!match) writes_ok = false;
    }

    if (!writes_ok) {
        printf("\n  Stage 2 FAILED — register writes not accepted.\n");
        printf("  Possible causes:\n");
        printf("    - SPI clock too fast for this module (already at 1 MHz)\n");
        printf("    - SPI mode incorrect (nRF24 requires mode 0)\n");
        printf("    - Module is a clone with non-standard SPI timing\n");
        printf("    - Module is defective\n\n");
        /* Restore EN_AA to POR default before returning */
        EnAa por_aa = EnAa::from_byte(EnAa::RESET_VALUE);
        radio.write_reg(por_aa);
        return false;
    }

    printf("  Stage 2 PASS — all write patterns verified.\n");

    /* ── Stage 3: Clone chip detection ──────────────────────────────────
     *
     * Test the EN_AA → EN_CRC forcing behaviour documented in the
     * nRF24L01+ Product Specification §CONFIG:
     *
     *   "If the EN_AA is set for any pipe, the EN_CRC bit in the
     *    CONFIG register is forced high."
     *
     * Procedure:
     * 1. Set EN_AA = 0x00 (all pipes disabled)
     * 2. Write CONFIG with EN_CRC = 0
     * 3. Read back CONFIG
     *
     * Expected on genuine nRF24L01+:
     *   CONFIG read-back has EN_CRC = 0 (because no EN_AA forcing condition)
     *
     * On Si24R1 clones:
     *   CONFIG read-back may still have EN_CRC = 1
     *   (the forcing persists even after EN_AA is cleared)
     *
     * En_AA is already at 0x00 from the write test above. */
    printf("\nStage 3: Clone chip detection (EN_AA / EN_CRC override)\n");

    /* Set EN_AA = 0x00 explicitly for clarity (was 0x2A from last write
     * test pattern) */
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

    printf("  EN_AA=0x00, wrote CONFIG=0x%02X (EN_CRC=0), read CONFIG=0x%02X\n",
           test_cfg.to_byte(), readback_cfg.to_byte());

    if (clone_detected) {
        printf("  EN_CRC is FORCED ON despite EN_AA=0 → CLONE CHIP DETECTED\n");
        printf("  Likely: Si24R1 or BK2425 clone (not genuine Nordic nRF24L01+)\n");
        printf("  Impact: BLE reception requires EN_CRC=0; clone may not support this\n");
    } else {
        printf("  EN_CRC accepted as 0 → genuine nRF24L01+ behaviour\n");
    }

    /* Restore CONFIG to POR value (power-down state) for safety.
     * configure_rx() will re-write it properly. */
    Config por_cfg = Config::from_byte(Config::RESET_VALUE);
    radio.write_reg(por_cfg);

    /* Leave EN_AA = 0x00 (configure_rx() expects/needs this).
     * configure_rx() also writes EN_AA = 0x00 as its first step,
     * so this is consistent either way. */
    printf("  EN_AA left at 0x00 (compatible with configure_rx)\n");

    printf("\nSPI comm test: %s%s\n\n",
           "PASS",
           clone_detected ? " (clone chip detected)" : "");

    /* Return true even if clone is detected — the SPI bus IS working,
     * the caller just needs to know about the clone for BLE purposes. */
    return true;
}

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

    /* Verify each register using typed reads and comparisons */
    {   /* CONFIG — CRITICAL: EN_CRC must be 0 for BLE reception */
        auto actual = radio.read_reg(Config{});
        bool pass = (actual.to_byte() & REG_MASK_ALL) == (exp_config.to_byte() & REG_MASK_ALL);
        printf("  %-10s  exp=0x%02X  got=0x%02X  [%s]%s\n",
               "CONFIG", exp_config.to_byte() & REG_MASK_ALL,
               actual.to_byte() & REG_MASK_ALL, pass ? "PASS" : "FAIL",
               actual.crc_mode != CrcMode::Disabled ? "  *** EN_CRC is ON - BLE pkts rejected!" : "");
        if (!pass) all_pass = false;
    }
    {   /* EN_AA — must be 0x00 or EN_CRC is forced on */
        auto actual = radio.read_reg(EnAa{});
        bool pass = (actual.to_byte() & REG_MASK_ALL) == (exp_en_aa.to_byte() & REG_MASK_ALL);
        printf("  %-10s  exp=0x%02X  got=0x%02X  [%s]%s\n",
               "EN_AA", exp_en_aa.to_byte() & REG_MASK_ALL,
               actual.to_byte() & REG_MASK_ALL, pass ? "PASS" : "FAIL",
               actual.to_byte() != 0x00 ? "  *** non-zero EN_AA forces EN_CRC on!" : "");
        if (!pass) all_pass = false;
    }
    {   /* EN_RXADDR */
        auto actual = radio.read_reg(EnRxAddr{});
        bool pass = (actual.to_byte() & REG_MASK_ALL) == (exp_en_rx.to_byte() & REG_MASK_ALL);
        printf("  %-10s  exp=0x%02X  got=0x%02X  [%s]\n",
               "EN_RXADDR", exp_en_rx.to_byte() & REG_MASK_ALL,
               actual.to_byte() & REG_MASK_ALL, pass ? "PASS" : "FAIL");
        if (!pass) all_pass = false;
    }
    {   /* SETUP_AW */
        auto actual = radio.read_reg(SetupAw{});
        bool pass = (actual.to_byte() & REG_MASK_ALL) == (exp_aw.to_byte() & REG_MASK_ALL);
        printf("  %-10s  exp=0x%02X  got=0x%02X  [%s]\n",
               "SETUP_AW", exp_aw.to_byte() & REG_MASK_ALL,
               actual.to_byte() & REG_MASK_ALL, pass ? "PASS" : "FAIL");
        if (!pass) all_pass = false;
    }
    {   /* SETUP_RETR */
        auto actual = radio.read_reg(SetupRetr{});
        bool pass = (actual.to_byte() & REG_MASK_ALL) == (exp_retr.to_byte() & REG_MASK_ALL);
        printf("  %-10s  exp=0x%02X  got=0x%02X  [%s]\n",
               "SETUP_RETR", exp_retr.to_byte() & REG_MASK_ALL,
               actual.to_byte() & REG_MASK_ALL, pass ? "PASS" : "FAIL");
        if (!pass) all_pass = false;
    }
    {   /* RF_CH — bit 7 is reserved, mask it out */
        auto actual = radio.read_reg(RfCh{});
        bool pass = (actual.to_byte() & REG_MASK_RF_CH) == (exp_rf_ch.to_byte() & REG_MASK_RF_CH);
        printf("  %-10s  exp=0x%02X  got=0x%02X  [%s]\n",
               "RF_CH", exp_rf_ch.to_byte() & REG_MASK_RF_CH,
               actual.to_byte() & REG_MASK_RF_CH, pass ? "PASS" : "FAIL");
        if (!pass) all_pass = false;
    }
    {   /* RF_SETUP — mask reserved bits 6 and 0 */
        auto actual = radio.read_reg(RfSetup{});
        bool pass = (actual.to_byte() & REG_MASK_RF_SETUP) == (exp_rf.to_byte() & REG_MASK_RF_SETUP);
        printf("  %-10s  exp=0x%02X  got=0x%02X  [%s]%s\n",
               "RF_SETUP", exp_rf.to_byte() & REG_MASK_RF_SETUP,
               actual.to_byte() & REG_MASK_RF_SETUP, pass ? "PASS" : "FAIL",
               actual.data_rate != DataRate::Mbps1 ? "  *** not 1 Mbps!" : "");
        if (!pass) all_pass = false;
    }
    {   /* RX_PW_P0 — bits 7:6 are reserved, mask them out */
        RxPwP0 pw0_tag;
        auto actual = radio.read_reg(pw0_tag);
        bool pass = (actual.to_byte() & REG_MASK_RX_PW) == (exp_pw.to_byte() & REG_MASK_RX_PW);
        printf("  %-10s  exp=0x%02X  got=0x%02X  [%s]\n",
               "RX_PW_P0", exp_pw.to_byte() & REG_MASK_RX_PW,
               actual.to_byte() & REG_MASK_RX_PW, pass ? "PASS" : "FAIL");
        if (!pass) all_pass = false;
    }
    {   /* DYNPD — must be 0x00 */
        auto actual = radio.read_reg(Dynpd{});
        bool pass = actual.to_byte() == 0x00;
        printf("  %-10s  exp=0x00  got=0x%02X  [%s]\n",
               "DYNPD", actual.to_byte(), pass ? "PASS" : "FAIL");
        if (!pass) all_pass = false;
    }
    {   /* FEATURE — must be 0x00 */
        auto actual = radio.read_reg(Feature{});
        bool pass = actual.to_byte() == 0x00;
        printf("  %-10s  exp=0x00  got=0x%02X  [%s]\n",
               "FEATURE", actual.to_byte(), pass ? "PASS" : "FAIL");
        if (!pass) all_pass = false;
    }
    {   /* FIFO_STATUS — read-only, display for info */
        auto actual = radio.read_reg(FifoStatus{});
        printf("  %-10s  rx_empty=%u  rx_full=%u  (raw 0x%02X)\n",
               "FIFO_STATUS",
               static_cast<unsigned>(actual.rx_empty),
               static_cast<unsigned>(actual.rx_full),
               actual.to_byte());
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
