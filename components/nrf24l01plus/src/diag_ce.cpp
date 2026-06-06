#include "nrf24l01plus/diag_boot.h"

#include <cstdio>
#include <cstring>
#include "nrf24l01plus/registers.h"

namespace nrf24 {
namespace diag {

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

/**
 * @brief Phase 0: Verify HAL and CE GPIO readback.
 *
 * Tests that the HAL can communicate with the nRF24L01+ by reading the
 * STATUS register, and that CE pin state matches the driver's ce_high()
 * and ce_low() commands.  Also measures CE transition timing.
 *
 * If ce_read() always returns 0 after ce_high(), the most likely cause
 * is that the CE GPIO is configured as GPIO_MODE_OUTPUT (output-only)
 * instead of GPIO_MODE_INPUT_OUTPUT — the input buffer is disabled.
 *
 * @param radio      Driver instance (must be constructed but not yet configured).
 * @param verbosity  Output verbosity level.
 * @return           Phase result. Fail if SPI is unreachable or CE readback
 *                   doesn't match the driven state.
 */
DiagPhaseResult verify_hal_ce(Driver &radio, DiagVerbosity verbosity)
{
    DiagPhaseResult result;
    result.status = DiagStatus::Pass;

    int64_t t_start = radio.hal().timestamp_us();

    if (verbosity >= DiagVerbosity::Summary) {
        printf("[diag:phase0] HAL_INIT starting\n");
    }

    /* ── Step 1: SPI connectivity quick test ────────────────────────────────
     * Read STATUS register.  If it returns 0xFF, MISO is stuck high
     * and the device is unreachable. */
    auto status_reg = radio.read_reg(Status{});
    uint8_t raw_status = status_reg.to_byte();

    if (raw_status == 0xFF) {
        result.status = DiagStatus::Fail;
        snprintf(result.detail, sizeof(result.detail),
                 "STATUS=0xFF — MISO stuck high, no device on bus");
        result.elapsed_us = static_cast<uint32_t>(radio.hal().timestamp_us() - t_start);
        print_phase(0, "HAL_INIT", result.status, result.detail);
        return result;
    }

    if (verbosity >= DiagVerbosity::Detailed) {
        printf("[diag:phase0] STATUS=0x%02X — SPI alive\n", raw_status);
    }

    /* ── Step 2: CE GPIO readback test ──────────────────────────────────────
     * Drive CE LOW, read back, then drive CE HIGH, read back.
     * If ce_read() always returns 0, the GPIO is likely output-only. */
    radio.ce_low();
    radio.hal().delay_us(100);  /* Allow level to settle */

    int ce_low_read = radio.hal().ce_read();

    int64_t t_high_start = radio.hal().timestamp_us();
    radio.ce_high();
    int64_t t_high_end = radio.hal().timestamp_us();

    int ce_high_read = radio.hal().ce_read();

    int64_t transition_us = t_high_end - t_high_start;

    if (verbosity >= DiagVerbosity::Detailed) {
        printf("[diag:phase0] CE LOW readback=%d, CE HIGH readback=%d, "
               "transition=%lld us\n",
               ce_low_read, ce_high_read,
               static_cast<long long>(transition_us));
    }

    /* Evaluate CE readback results */
    if (ce_high_read != 1 && ce_low_read == 0) {
        /* ce_read() always returns 0 — likely GPIO_MODE_OUTPUT */
        result.status = DiagStatus::Fail;
        snprintf(result.detail, sizeof(result.detail),
                 "CE GPIO may be in OUTPUT-only mode (GPIO_MODE_OUTPUT) — "
                 "use GPIO_MODE_INPUT_OUTPUT");
    } else if (ce_high_read != 1) {
        result.status = DiagStatus::Fail;
        snprintf(result.detail, sizeof(result.detail),
                 "CE HIGH readback=%d (expected 1), transition=%lld us",
                 ce_high_read, static_cast<long long>(transition_us));
    } else if (ce_low_read != 0) {
        result.status = DiagStatus::Warn;
        snprintf(result.detail, sizeof(result.detail),
                 "CE LOW readback=%d (expected 0), CE HIGH OK, transition=%lld us",
                 ce_low_read, static_cast<long long>(transition_us));
    } else {
        snprintf(result.detail, sizeof(result.detail),
                 "CE readback OK, transition=%lld us",
                 static_cast<long long>(transition_us));
    }

    /* Restore CE to LOW for subsequent phases (they will manage CE) */
    radio.ce_low();

    result.elapsed_us = static_cast<uint32_t>(radio.hal().timestamp_us() - t_start);

    if (verbosity >= DiagVerbosity::Summary) {
        print_phase(0, "HAL_INIT", result.status, result.detail);
    }

    return result;
}

/**
 * @brief Phase 4: Verify CE state, CONFIG, and FIFO consistency.
 *
 * Checks that CE is in the expected state (HIGH for RX mode after
 * configure_rx), that CONFIG matches the expected BLE configuration
 * (PWR_UP=1, PRIM_RX=1, EN_CRC=0), and that FIFO_STATUS is consistent
 * with the operational state.
 *
 * @param radio      Driver instance (must be configured and in RX mode).
 * @param verbosity  Output verbosity level.
 * @return           Phase result. Fail if critical inconsistencies found.
 */
DiagPhaseResult verify_ce_state(Driver &radio, DiagVerbosity verbosity)
{
    DiagPhaseResult result;
    result.status = DiagStatus::Pass;

    int64_t t_start = radio.hal().timestamp_us();

    if (verbosity >= DiagVerbosity::Summary) {
        printf("[diag:phase4] CE_STATE starting\n");
    }

    auto cfg = radio.read_reg(Config{});
    auto fifo = radio.read_reg(FifoStatus{});
    int ce_level = radio.hal().ce_read();

    bool all_ok = true;
    char detail_buf[128];
    int pos = 0;

    /* CONFIG checks */
    bool pwr_up_ok = (cfg.power_mode == PowerMode::Up);
    bool prim_rx_ok = (cfg.primary == PrimaryMode::RX);
    bool crc_off_ok = (cfg.crc_mode == CrcMode::Disabled);

    /* FIFO checks */
    bool tx_empty_ok = fifo.tx_empty;
    (void)0;  /* rx_empty status varies with traffic — not a pass/fail criterion */

    /* CE state check */
    bool ce_ok = (ce_level == 1);

    if (verbosity >= DiagVerbosity::Detailed) {
        printf("[diag:phase4] CONFIG: PWR_UP=%u PRIM_RX=%u EN_CRC=%u "
               "CRCO=%u (raw 0x%02X)\n",
               static_cast<unsigned>(cfg.power_mode),
               static_cast<unsigned>(cfg.primary),
               static_cast<unsigned>(cfg.crc_mode),
               static_cast<unsigned>(cfg.crc_encoding),
               cfg.to_byte());
        printf("[diag:phase4] FIFO_STATUS: rx_empty=%u rx_full=%u "
               "tx_empty=%u (raw 0x%02X)\n",
               static_cast<unsigned>(fifo.rx_empty),
               static_cast<unsigned>(fifo.rx_full),
               static_cast<unsigned>(fifo.tx_empty),
               fifo.to_byte());
        printf("[diag:phase4] CE GPIO: level=%d\n", ce_level);
    }

    /* Build result */
    if (!pwr_up_ok) {
        pos += snprintf(detail_buf + pos, sizeof(detail_buf) - pos,
                        "PWR_UP=0! ");
        all_ok = false;
    }
    if (!prim_rx_ok) {
        pos += snprintf(detail_buf + pos, sizeof(detail_buf) - pos,
                        "PRIM_RX=0! ");
        all_ok = false;
    }
    if (!crc_off_ok) {
        pos += snprintf(detail_buf + pos, sizeof(detail_buf) - pos,
                        "EN_CRC=1 (BLE rejects CRC)! ");
        all_ok = false;
    }
    if (!ce_ok) {
        pos += snprintf(detail_buf + pos, sizeof(detail_buf) - pos,
                        "CE=0 (not in RX mode)! ");
        all_ok = false;
    }
    if (!tx_empty_ok) {
        pos += snprintf(detail_buf + pos, sizeof(detail_buf) - pos,
                        "TX FIFO not empty! ");
        /* Warning, not critical failure */
    }

    /* RX mode check: PWR_UP=1 + PRIM_RX=1 + CE=1 */
    bool rx_mode = pwr_up_ok && prim_rx_ok && ce_ok;

    if (all_ok && rx_mode) {
        snprintf(result.detail, sizeof(result.detail),
                 "RX mode active — PWR_UP=1, PRIM_RX=1, CE=1");
    } else if (!all_ok) {
        result.status = DiagStatus::Fail;
        snprintf(result.detail, sizeof(result.detail), "%s", detail_buf);
    } else {
        /* Should not reach here, but handle gracefully */
        snprintf(result.detail, sizeof(result.detail),
                 "state inconsistent");
    }

    result.elapsed_us = static_cast<uint32_t>(radio.hal().timestamp_us() - t_start);

    if (verbosity >= DiagVerbosity::Summary) {
        print_phase(4, "CE_STATE", result.status, result.detail);
    }

    return result;
}

/**
 * @brief Phase 5: Extended CE-high RPD/FIFO poll test.
 *
 * Sets CE high on the specified BLE channel and polls RPD (Received
 * Power Detector) and FIFO status for the specified duration.  This
 * tests whether the nRF24L01+ can detect RF energy and receive packets
 * in a real environment.
 *
 * After the test, restores CE HIGH and re-tunes to channel 37 for
 * normal sniffer operation.
 *
 * @param radio         Driver instance (must be configured for BLE RX).
 * @param ble_channel   BLE channel index (use values from nrf24::ble::ADV_CHANNELS,
 *                      e.g. 37, 38, 39).
 * @param poll_ms       Polling interval in milliseconds between RPD/FIFO reads.
 * @param duration_s    Total duration of the test in seconds.
 * @param verbosity     Output verbosity level.
 * @return              Phase result. Warn if no RPD hit but device is alive.
 */
DiagPhaseResult verify_extended_ce(Driver &radio,
                                    uint8_t ble_channel,
                                    uint16_t poll_ms,
                                    uint16_t duration_s,
                                    DiagVerbosity verbosity)
{
    DiagPhaseResult result;
    result.status = DiagStatus::Pass;

    int64_t t_start = radio.hal().timestamp_us();

    if (verbosity >= DiagVerbosity::Summary) {
        printf("[diag:phase5] EXTENDED starting — ch%u, %u ms poll, %u s duration\n",
               static_cast<unsigned>(ble_channel),
               static_cast<unsigned>(poll_ms),
               static_cast<unsigned>(duration_s));
    }

    /* Lower CE to reconfigure channel */
    radio.ce_low();

    /* Tune to the specified BLE channel */
    RfCh rf_ch;
    rf_ch.channel = ble::channel_to_rf_ch(ble_channel);
    radio.write_reg(rf_ch);

    if (verbosity >= DiagVerbosity::Verbose) {
        printf("[diag:phase5] Tuned to ch%u (RF_CH=%u, freq=%u MHz)\n",
               static_cast<unsigned>(ble_channel),
               static_cast<unsigned>(rf_ch.channel),
               2400 + static_cast<unsigned>(rf_ch.channel));
    }

    /* Flush RX FIFO and clear interrupt flags */
    radio.flush_rx();
    ble::clear_irq_flags(radio);

    /* Read baseline CONFIG and FIFO_STATUS before CE HIGH */
    if (verbosity >= DiagVerbosity::Verbose) {
        auto cfg_before = radio.read_reg(Config{});
        auto fifo_before = radio.read_reg(FifoStatus{});
        printf("[diag:phase5] Before CE HIGH: CONFIG=0x%02X "
               "FIFO rx_empty=%u rx_full=%u\n",
               cfg_before.to_byte(),
               static_cast<unsigned>(fifo_before.rx_empty),
               static_cast<unsigned>(fifo_before.rx_full));
    }

    /* Assert CE HIGH with timestamp */
    int64_t t_ce_high = radio.hal().timestamp_us();
    radio.ce_high();

    /* Verify CE readback */
    int ce_level = radio.hal().ce_read();
    if (ce_level != 1) {
        result.status = DiagStatus::Fail;
        snprintf(result.detail, sizeof(result.detail),
                 "CE HIGH readback=%d (expected 1)", ce_level);
        radio.ce_low();
        /* Restore channel for sniffer */
        ble::switch_channel(radio, 37);
        result.elapsed_us = static_cast<uint32_t>(radio.hal().timestamp_us() - t_start);
        print_phase(5, "EXTENDED", result.status, result.detail);
        return result;
    }

    /* Poll for the specified duration */
    uint32_t total_polls = (static_cast<uint32_t>(duration_s) * 1000) / poll_ms;
    if (total_polls == 0) total_polls = 1;
    int rpd_hit_count = 0;
    int fifo_data_count = 0;

    for (uint32_t i = 0; i < total_polls; ++i) {
        radio.hal().delay_ms(poll_ms);

        auto rpd = radio.read_reg(Rpd{});
        auto fifo = radio.read_reg(FifoStatus{});
        bool rpd_active = rpd.received_power;
        bool fifo_has_data = !fifo.rx_empty;

        if (rpd_active) rpd_hit_count++;
        if (fifo_has_data) fifo_data_count++;

        if (verbosity >= DiagVerbosity::Verbose) {
            /* Log every 10th sample or when data is present */
            if (rpd_active || fifo_has_data || (i % 10 == 0)) {
                int64_t t_now = radio.hal().timestamp_us();
                printf("[diag:phase5] [%u/%u] t=%lld ms  RPD=%u  "
                       "FIFO: rx_empty=%u rx_full=%u\n",
                       static_cast<unsigned>(i + 1),
                       static_cast<unsigned>(total_polls),
                       static_cast<long long>((t_now - t_ce_high) / 1000),
                       static_cast<unsigned>(rpd.received_power),
                       static_cast<unsigned>(fifo.rx_empty),
                       static_cast<unsigned>(fifo.rx_full));
            }
        }

        /* If FIFO has data, drain it */
        if (fifo_has_data) {
            uint8_t buf[32];
            radio.read_payload(buf, 32);
            ble::clear_irq_flags(radio);
            radio.flush_rx();

            if (verbosity >= DiagVerbosity::Detailed) {
                printf("[diag:phase5]   payload drained:");
                for (int j = 0; j < 32; j++) {
                    printf(" %02X", buf[j]);
                }
                printf("\n");
            }
        }
    }

    /* Lower CE and report */
    radio.ce_low();

    /* Build summary */
    if (rpd_hit_count == 0 && fifo_data_count == 0) {
        result.status = DiagStatus::Warn;
        snprintf(result.detail, sizeof(result.detail),
                 "RPD=0 in %u s — no RF energy detected (ch%u). "
                 "Check antenna, power, BLE traffic",
                 static_cast<unsigned>(duration_s),
                 static_cast<unsigned>(ble_channel));
    } else if (rpd_hit_count > 0 && fifo_data_count == 0) {
        result.status = DiagStatus::Warn;
        snprintf(result.detail, sizeof(result.detail),
                 "RPD hits=%d but FIFO empty=%d/%u — signal detected but "
                 "no packets captured. Check EN_CRC, RX_ADDR, payload width",
                 rpd_hit_count, fifo_data_count,
                 static_cast<unsigned>(total_polls));
    } else {
        snprintf(result.detail, sizeof(result.detail),
                 "RPD hits=%d/%u, FIFO data=%d/%u — CE and RX working",
                 rpd_hit_count, static_cast<unsigned>(total_polls),
                 fifo_data_count, static_cast<unsigned>(total_polls));
    }

    /* Restore CE HIGH and re-tune to channel 37 for sniffer task */
    ble::switch_channel(radio, 37);

    result.elapsed_us = static_cast<uint32_t>(radio.hal().timestamp_us() - t_start);

    if (verbosity >= DiagVerbosity::Summary) {
        print_phase(5, "EXTENDED", result.status, result.detail);
    }

    return result;
}

} // namespace diag
} // namespace nrf24