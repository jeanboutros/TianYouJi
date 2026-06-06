/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "nrf24l01plus/registers/status.h"
#include "nrf24l01plus/registers/config.h"
#include "nrf24l01plus/registers/en_aa.h"
#include "nrf24l01plus/registers/en_rxaddr.h"
#include "nrf24l01plus/registers/setup_aw.h"
#include "nrf24l01plus/registers/setup_retr.h"
#include "nrf24l01plus/registers/rf_ch.h"
#include "nrf24l01plus/registers/rf_setup.h"
#include "nrf24l01plus/registers/rx_pw.h"
#include "nrf24l01plus/registers/fifo_status.h"
#include "nrf24l01plus/registers/feature.h"
#include "nrf24l01plus/registers/dynpd.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "nrf24_espidf/hal_espidf.h"
#include <nrf24l01plus/driver.h>
#include <nrf24l01plus/ble.h>
#include <nrf24l01plus/ble_config.h>
#include <nrf24l01plus/diag.h>
#include <nrf24l01plus/registers.h>

/* --- Pin definitions ------------------------------------------------ */
static constexpr gpio_num_t PIN_MISO = GPIO_NUM_19;
static constexpr gpio_num_t PIN_MOSI = GPIO_NUM_23;
static constexpr gpio_num_t PIN_SCLK = GPIO_NUM_18;
static constexpr gpio_num_t PIN_CSN  = GPIO_NUM_17;
static constexpr gpio_num_t PIN_CE   = GPIO_NUM_5;

/** @brief Maximum nRF24L01+ payload size in bytes (FIFO width, not BLE PDU limit). */
static constexpr uint8_t NRF24_MAX_PAYLOAD = 32;

static nrf24::EspIdfHal hal;
static nrf24::Driver radio(hal);

/* Print heartbeat every 500 loop iterations (reduces serial noise) */
static constexpr uint32_t HB_LOOP_INTERVAL = 500;

/** @brief Enable BLE diagnostic output (RPD monitoring, raw FIFO dump).
 *
 * Set to 1 to enable, 0 to disable. When disabled, no additional
 * serial output is produced and runtime behaviour is unchanged.
 */
#define BLE_DIAG 1

/* --- Startup diagnostic: read back key registers ------------------------ */

/**
 * @brief Print a one-time diagnostic summary of nRF24L01+ register state.
 *
 * Reads back ALL registers relevant to BLE passive RX after
 * nrf24::ble::configure_rx() to verify the device was programmed correctly.
 * Must be called AFTER nrf24::ble::configure_rx() and BEFORE the
 * sniffer task starts.
 *
 * @param radio  Driver instance (already configured for BLE RX).
 */
static void print_register_diagnostics(nrf24::Driver &radio)
{
    printf("\n=== Comprehensive register read-back ===\n");

    /* CONFIG — expect PWR_UP=1, PRIM_RX=1, EN_CRC=0 (raw 0x03).
     * CRITICAL: if EN_CRC=1 here, the nRF24 will reject every BLE packet
     * because it expects a CRC that BLE packets don't include in the
     * format nRF24 understands. */
    auto cfg = radio.read_reg(nrf24::Config{});
    printf("  CONFIG      EN_CRC=%u CRCO=%u PWR_UP=%u PRIM_RX=%u irq=0x%02X  (raw 0x%02X)  %s\n",
           static_cast<unsigned>(cfg.crc_mode),
           static_cast<unsigned>(cfg.crc_encoding),
           static_cast<unsigned>(cfg.power_mode),
           static_cast<unsigned>(cfg.primary),
           static_cast<unsigned>(cfg.irq_mask),
           cfg.to_byte(),
           cfg.crc_mode == nrf24::CrcMode::Disabled ? "[OK: CRC off]"
                                                      : "[FAIL: CRC ON - BLE pkts rejected!]");

    /* EN_AA — expect 0x00 (all disabled).
     * If any bit is 1, EN_CRC is forced on per datasheet §CONFIG. */
    auto en_aa = radio.read_reg(nrf24::EnAa{});
    printf("  EN_AA       P0=%d P1=%d P2=%d P3=%d P4=%d P5=%d  (raw 0x%02X)  %s\n",
           en_aa.pipe[0], en_aa.pipe[1], en_aa.pipe[2],
           en_aa.pipe[3], en_aa.pipe[4], en_aa.pipe[5],
           en_aa.to_byte(),
           en_aa.to_byte() == 0x00 ? "[OK: all off]"
                                    : "[FAIL: EN_CRC will be forced on!]");

    /* EN_RXADDR — expect pipe 0 only (0x01) */
    auto en_rx = radio.read_reg(nrf24::EnRxAddr{});
    printf("  EN_RXADDR   P0=%d P1=%d P2=%d P3=%d P4=%d P5=%d  (raw 0x%02X)\n",
           en_rx.pipe[0], en_rx.pipe[1], en_rx.pipe[2],
           en_rx.pipe[3], en_rx.pipe[4], en_rx.pipe[5],
           en_rx.to_byte());

    /* SETUP_AW — expect 4 bytes (0x02) */
    auto aw = radio.read_reg(nrf24::SetupAw{});
    printf("  SETUP_AW    width=%u  (raw 0x%02X)  %s\n",
           static_cast<unsigned>(aw.address_width),
           aw.to_byte(),
           aw.address_width == nrf24::AddressWidth::Bytes4 ? "[OK]"
                                                             : "[FAIL: expected 4 bytes]");

    /* SETUP_RETR — expect ARC=0 (0x00) */
    auto retr = radio.read_reg(nrf24::SetupRetr{});
    printf("  SETUP_RETR  delay=%u count=%u  (raw 0x%02X)  %s\n",
           static_cast<unsigned>(retr.delay),
           static_cast<unsigned>(retr.count),
           retr.to_byte(),
           retr.count == nrf24::AutoRetransmitCount::Disabled ? "[OK: no retransmit]"
                                                                : "[FAIL: retransmit enabled!]");

    /* RF_CH — should match the initial BLE channel frequency */
    auto rf_ch = radio.read_reg(nrf24::RfCh{});
    printf("  RF_CH       channel=%u  freq=%u MHz  (raw 0x%02X)\n",
           rf_ch.channel, 2400 + rf_ch.channel,
           rf_ch.to_byte());

    /* RF_SETUP — expect 1 Mbps, 0 dBm (0x06) */
    auto rf_setup = radio.read_reg(nrf24::RfSetup{});
    printf("  RF_SETUP    rate=%u pwr=%u cw=%u pll=%u  (raw 0x%02X)  %s\n",
           static_cast<unsigned>(rf_setup.data_rate),
           static_cast<unsigned>(rf_setup.tx_power),
           static_cast<unsigned>(rf_setup.cont_wave),
           static_cast<unsigned>(rf_setup.pll_lock),
           rf_setup.to_byte(),
           rf_setup.data_rate == nrf24::DataRate::Mbps1 ? "[OK: 1 Mbps]"
                                                        : "[FAIL: not 1 Mbps!]");

    /* RX_PW_P0 — expect 32 bytes (0x20) */
    auto pw0 = radio.read_reg(nrf24::RxPwP0{});
    printf("  RX_PW_P0    payload_width=%u  (raw 0x%02X)\n",
           pw0.payload_width,
           pw0.to_byte());

    /* DYNPD — expect 0x00 (no dynamic payload) */
    auto dynpd = radio.read_reg(nrf24::Dynpd{});
    printf("  DYNPD       P0=%d P1=%d P2=%d P3=%d P4=%d P5=%d  (raw 0x%02X)\n",
           dynpd.pipe[0], dynpd.pipe[1], dynpd.pipe[2],
           dynpd.pipe[3], dynpd.pipe[4], dynpd.pipe[5],
           dynpd.to_byte());

    /* FEATURE — expect 0x00 (no dynamic features) */
    auto feat = radio.read_reg(nrf24::Feature{});
    printf("  FEATURE     en_dpl=%u en_ack_pay=%u en_dyn_ack=%u  (raw 0x%02X)\n",
           static_cast<unsigned>(feat.en_dpl),
           static_cast<unsigned>(feat.en_ack_pay),
           static_cast<unsigned>(feat.en_dyn_ack),
           feat.to_byte());

    /* FIFO_STATUS — check initial state */
    auto fifo = radio.read_reg(nrf24::FifoStatus{});
    printf("  FIFO_STATUS tx_empty=%u tx_full=%u rx_empty=%u rx_full=%u tx_reuse=%u  (raw 0x%02X)\n",
           static_cast<unsigned>(fifo.tx_empty),
           static_cast<unsigned>(fifo.tx_full),
           static_cast<unsigned>(fifo.rx_empty),
           static_cast<unsigned>(fifo.rx_full),
           static_cast<unsigned>(fifo.tx_reuse),
           fifo.to_byte());

    /* RX_ADDR_P0 — expect {0x6B, 0x7D, 0x91, 0x71} (BLE adv access addr) */
    uint8_t addr[4];
    radio.read_reg_multi(nrf24::reg::RX_ADDR_P0, addr, 4);
    printf("  RX_ADDR_P0  %02X:%02X:%02X:%02X  (exp 6B:7D:91:71)  %s\n",
           addr[0], addr[1], addr[2], addr[3],
           (addr[0]==0x6B && addr[1]==0x7D && addr[2]==0x91 && addr[3]==0x71)
               ? "[OK]" : "[FAIL: address mismatch!]");

    /* TX_ADDR — expect same as RX_ADDR_P0 for promiscuous sniffing */
    radio.read_reg_multi(nrf24::reg::TX_ADDR, addr, 4);
    printf("  TX_ADDR     %02X:%02X:%02X:%02X\n",
           addr[0], addr[1], addr[2], addr[3]);

    printf("=== End register read-back ===\n\n");
}

/* --- CE diagnostic functions (under BLE_DIAG guard) ------------------- */

#if BLE_DIAG

/**
 * @brief Verify CE GPIO readback and measure CE transition timing.
 *
 * Performs a complete CE LOW→HIGH→LOW cycle, reading the GPIO level
 * back after each transition using gpio_get_level().  This confirms
 * the ESP32 GPIO hardware is actually driving the CE pin.  Also
 * measures microsecond-precision transition timing using
 * esp_timer_get_time().
 *
 * The test starts by forcing CE LOW (known baseline), then:
 * 1. Reads GPIO level → should be 0
 * 2. Sets CE HIGH, reads back → should be 1, captures timestamp
 * 3. Waits 1 ms (minimum CE high for RX mode entry)
 * 4. Sets CE LOW, reads back → should be 0, captures timestamp
 *
 * Finally restores CE HIGH so the nRF24 remains in RX mode.
 *
 * @param radio  Driver instance (used for ce_high/ce_low).
 * @param ce_pin GPIO number for CE (used for gpio_get_level readback).
 */
static void diag_ce_readback(nrf24::Driver &radio, gpio_num_t ce_pin)
{
    printf("\n=== CE GPIO readback & timing test ===\n");

    /* Force CE LOW as baseline */
    radio.ce_low();
    vTaskDelay(1);

    int level_before = gpio_get_level(ce_pin);
    printf("  CE initial (forced LOW): gpio_get_level=%d  %s\n",
           level_before,
           level_before == 0 ? "[OK]" : "[FAIL: expected 0]");

    /* CE HIGH transition with timestamp */
    int64_t t_low_to_high_start = esp_timer_get_time();
    radio.ce_high();
    int64_t t_low_to_high_end = esp_timer_get_time();

    int level_high = gpio_get_level(ce_pin);
    printf("  CE HIGH: gpio_get_level=%d  %s  transition=%lld us\n",
           level_high,
           level_high == 1 ? "[OK]" : "[FAIL: expected 1]",
           static_cast<long long>(t_low_to_high_end - t_low_to_high_start));

    /* Hold CE high for 1 ms to satisfy nRF24 RX mode entry
     * (datasheet §6.1 Table 6: CE=1 required for RX mode). */
    vTaskDelay(pdMS_TO_TICKS(1));

    /* CE LOW transition with timestamp */
    int64_t t_high_to_low_start = esp_timer_get_time();
    radio.ce_low();
    int64_t t_high_to_low_end = esp_timer_get_time();

    int level_low = gpio_get_level(ce_pin);
    printf("  CE LOW:  gpio_get_level=%d  %s  transition=%lld us\n",
           level_low,
           level_low == 0 ? "[OK]" : "[FAIL: expected 0]",
           static_cast<long long>(t_high_to_low_end - t_high_to_low_start));

    /* Compute actual CE-high duration (includes vTaskDelay) */
    int64_t ce_high_dur_us = t_high_to_low_start - t_low_to_high_end;
    printf("  CE high duration: %lld us (%lld ms)\n",
           static_cast<long long>(ce_high_dur_us),
           static_cast<long long>(ce_high_dur_us / 1000));

    /* Restore CE HIGH so the nRF24 re-enters RX mode */
    radio.ce_high();
    vTaskDelay(1);

    int level_restored = gpio_get_level(ce_pin);
    printf("  CE restored HIGH: gpio_get_level=%d  %s\n",
           level_restored,
           level_restored == 1 ? "[OK]" : "[FAIL: expected 1]");

    printf("=== End CE readback test ===\n\n");
}

/**
 * @brief Dump CONFIG and FIFO_STATUS registers alongside CE GPIO state.
 *
 * Reads the CONFIG register (PRIM_RX, PWR_UP bits) and FIFO_STATUS
 * register (RX_EMPTY, RX_FULL, TX_EMPTY, TX_FULL, TX_REUSE), then
 * reads the CE GPIO level.  Useful for verifying that the nRF24 is
 * in the expected state (PWR_UP=1, PRIM_RX=1, CE=1 = RX mode) and
 * that the FIFO state is consistent with the CE state.
 *
 * @param radio   Driver instance.
 * @param ce_pin  GPIO number for CE (used for gpio_get_level).
 */
static void diag_config_and_fifo_with_ce(nrf24::Driver &radio, gpio_num_t ce_pin)
{
    printf("=== CONFIG + FIFO_STATUS with CE state ===\n");

    auto cfg = radio.read_reg(nrf24::Config{});
    auto fifo = radio.read_reg(nrf24::FifoStatus{});
    int ce_level = gpio_get_level(ce_pin);

    printf("  CONFIG:      PWR_UP=%u PRIM_RX=%u EN_CRC=%u CRCO=%u  (raw 0x%02X)\n",
           static_cast<unsigned>(cfg.power_mode),
           static_cast<unsigned>(cfg.primary),
           static_cast<unsigned>(cfg.crc_mode),
           static_cast<unsigned>(cfg.crc_encoding),
           cfg.to_byte());
    printf("  FIFO_STATUS: rx_empty=%u rx_full=%u tx_empty=%u tx_full=%u tx_reuse=%u  (raw 0x%02X)\n",
           static_cast<unsigned>(fifo.rx_empty),
           static_cast<unsigned>(fifo.rx_full),
           static_cast<unsigned>(fifo.tx_empty),
           static_cast<unsigned>(fifo.tx_full),
           static_cast<unsigned>(fifo.tx_reuse),
           fifo.to_byte());
    printf("  CE GPIO:     level=%d  %s\n",
           ce_level,
           ce_level == 1 ? "[OK: CE HIGH → RX mode active]"
                         : "[WARN: CE LOW → nRF24 NOT in RX mode!]");

    /* Cross-check: if PWR_UP=1, PRIM_RX=1, CE=1 → RX mode per datasheet §6.1 */
    bool rx_mode = (cfg.power_mode == nrf24::PowerMode::Up)
                && (cfg.primary == nrf24::PrimaryMode::RX)
                && (ce_level == 1);
    printf("  RX mode:     %s  %s\n",
           rx_mode ? "ACTIVE" : "INACTIVE",
           rx_mode ? "[OK: PWR_UP=1 + PRIM_RX=1 + CE=1]"
                   : "[FAIL: not all conditions met for RX mode]");

    printf("=== End CONFIG + FIFO_STATUS dump ===\n\n");
}

/**
 * @brief Extended CE-high test: hold CE HIGH for 5 seconds on a single channel.
 *
 * Tunes to BLE advertising channel 37 (2402 MHz), flushes the RX FIFO,
 * then holds CE HIGH for 5 full seconds while continuously polling
 * RPD and FIFO_STATUS every 100 ms.  This test answers the question:
 * "Does the nRF24 EVER receive anything when CE is held high continuously?"
 *
 * If RPD remains 0 for the entire 5 seconds, the issue is likely:
 * - CE not actually HIGH (GPIO readback will show this)
 * - Wrong frequency or data rate
 * - No BLE traffic in range
 * - Hardware fault (module, wiring, power supply)
 *
 * If RPD shows signal but FIFO stays empty, the issue is likely:
 * - CRC rejection (EN_CRC forced on by EN_AA)
 * - Address mismatch
 * - Wrong payload width
 *
 * @param radio   Driver instance.
 * @param ce_pin  GPIO number for CE (used for gpio_get_level).
 */
static void diag_extended_ce_test(nrf24::Driver &radio, gpio_num_t ce_pin)
{
    printf("\n=== Extended CE-high test (5 s on ch37) ===\n");

    /* Step 1: Lower CE to reconfigure channel */
    radio.ce_low();

    /* Step 2: Tune to BLE advertising channel 37 (RF_CH=2, 2402 MHz) */
    nrf24::RfCh rf_ch;
    rf_ch.channel = nrf24::ble::channel_to_rf_ch(37);
    radio.write_reg(rf_ch);
    printf("  Tuned to ch37 (RF_CH=%u, freq=%u MHz)\n",
           rf_ch.channel, 2400 + rf_ch.channel);

    /* Step 3: Flush RX FIFO and clear interrupt flags */
    radio.flush_rx();
    nrf24::ble::clear_irq_flags(radio);

    /* Step 4: Read baseline CONFIG and FIFO_STATUS before CE HIGH */
    auto cfg_before = radio.read_reg(nrf24::Config{});
    auto fifo_before = radio.read_reg(nrf24::FifoStatus{});
    printf("  Before CE HIGH:\n");
    printf("    CONFIG: PWR_UP=%u PRIM_RX=%u EN_CRC=%u  (raw 0x%02X)\n",
           static_cast<unsigned>(cfg_before.power_mode),
           static_cast<unsigned>(cfg_before.primary),
           static_cast<unsigned>(cfg_before.crc_mode),
           cfg_before.to_byte());
    printf("    FIFO:  rx_empty=%u rx_full=%u  (raw 0x%02X)\n",
           static_cast<unsigned>(fifo_before.rx_empty),
           static_cast<unsigned>(fifo_before.rx_full),
           fifo_before.to_byte());

    /* Step 5: Assert CE HIGH with timestamp */
    int64_t t_ce_high = esp_timer_get_time();
    radio.ce_high();

    /* Verify GPIO readback immediately after CE HIGH */
    int ce_level = gpio_get_level(ce_pin);
    printf("  CE HIGH at t=%lld us, gpio_get_level=%d  %s\n",
           static_cast<long long>(t_ce_high),
           ce_level,
           ce_level == 1 ? "[OK]" : "[FAIL: CE not actually HIGH!]");

    /* Step 6: Poll for 5 seconds, sampling every 100 ms */
    static constexpr int POLL_INTERVAL_MS = 100;
    static constexpr int POLL_COUNT = 50;  /* 5 s / 100 ms */
    int rpd_hit_count = 0;
    int fifo_data_count = 0;

    for (int i = 0; i < POLL_COUNT; i++)
    {
        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));

        auto rpd = radio.read_reg(nrf24::Rpd{});
        auto fifo = radio.read_reg(nrf24::FifoStatus{});
        auto st = radio.read_reg(nrf24::Status{});

        bool rpd_active = rpd.received_power;
        bool fifo_has_data = !fifo.rx_empty;

        if (rpd_active) rpd_hit_count++;
        if (fifo_has_data) fifo_data_count++;

        /* Only log if something changed or every 10th sample to reduce noise */
        if (rpd_active || fifo_has_data || (i % 10 == 0))
        {
            int64_t t_now = esp_timer_get_time();
            printf("  [%d/%d] t=%lld ms  RPD=%u  FIFO: rx_empty=%u rx_full=%u  "
                   "STATUS: rx_dr=%u rx_p_no=%u  CE_gpio=%d\n",
                   i + 1, POLL_COUNT,
                   static_cast<long long>((t_now - t_ce_high) / 1000),
                   static_cast<unsigned>(rpd.received_power),
                   static_cast<unsigned>(fifo.rx_empty),
                   static_cast<unsigned>(fifo.rx_full),
                   static_cast<unsigned>(st.rx_dr),
                   static_cast<unsigned>(st.rx_p_no),
                   gpio_get_level(ce_pin));

            /* If FIFO has data, read and dump the payload for analysis */
            if (fifo_has_data)
            {
                uint8_t buf[NRF24_MAX_PAYLOAD];
                radio.read_payload(buf, NRF24_MAX_PAYLOAD);
                nrf24::ble::clear_irq_flags(radio);
                radio.flush_rx();

                printf("    payload:");
                for (uint8_t j = 0; j < NRF24_MAX_PAYLOAD; j++)
                {
                    printf(" %02X", buf[j]);
                }
                printf("\n");
            }
        }
    }

    /* Step 7: Summary */
    int64_t t_ce_low = esp_timer_get_time();
    radio.ce_low();
    int ce_level_after = gpio_get_level(ce_pin);

    printf("  --- Summary ---\n");
    printf("  CE HIGH duration: %lld ms\n",
           static_cast<long long>((t_ce_low - t_ce_high) / 1000));
    printf("  RPD hits: %d/%d samples (%.1f%%)\n",
           rpd_hit_count, POLL_COUNT,
           100.0 * rpd_hit_count / POLL_COUNT);
    printf("  FIFO data: %d/%d samples (%.1f%%)\n",
           fifo_data_count, POLL_COUNT,
           100.0 * fifo_data_count / POLL_COUNT);
    printf("  Final CE LOW: gpio_get_level=%d  %s\n",
           ce_level_after,
           ce_level_after == 0 ? "[OK]" : "[FAIL: expected 0]");

    /* Diagnostic interpretation guidance */
    if (rpd_hit_count == 0)
    {
        printf("  DIAG: RPD never triggered in 5 s — check:\n");
        printf("    - Is CE actually HIGH? (GPIO readback above)\n");
        printf("    - Is there BLE traffic in range?\n");
        printf("    - Is the nRF24 module powered and functional?\n");
    }
    else if (fifo_data_count == 0)
    {
        printf("  DIAG: RPD triggered but FIFO always empty — check:\n");
        printf("    - EN_CRC may be forced on (EN_AA override)\n");
        printf("    - RX_ADDR_P0 may not match BLE access address\n");
        printf("    - Payload width may be wrong\n");
    }
    else
    {
        printf("  DIAG: Data received successfully — CE and RX working\n");
    }

    /* Restore CE HIGH and re-tune to initial channel for sniffer task */
    nrf24::ble::switch_channel(radio, 37);

    printf("=== End extended CE-high test ===\n\n");
}

#endif /* BLE_DIAG */

/* --- BLE sniffer task ----------------------------------------------- */

static const nrf24::ble::RxConfig rx_config = {
    .initial_channel_idx  = 0,
    .payload_width        = NRF24_MAX_PAYLOAD,
    .scan_duration_ms     = 50,
    .extra_channels       = nullptr,
};

static void ble_sniffer_task(void *arg)
{
    const auto &cfg = rx_config;
    uint8_t  seq_i       = 0;
    uint32_t loop_count  = 0;
    uint32_t pkt_count   = 0;
    printf("BLE sniffer started -- scanning %u channels, %u ms/ch\n",
           cfg.total_channels(), cfg.scan_duration_ms);

    while (1)
    {
        uint8_t ble_ch = cfg.channel_at(seq_i);

        if ((loop_count % HB_LOOP_INTERVAL) == 0)
        {
            auto st = radio.read_reg(nrf24::Status{});
            char st_buf[96];
            st.format(st_buf, sizeof(st_buf));
            printf("[hb] loops=%" PRIu32 "  pkts=%" PRIu32
                   "  STATUS={%s}  ble_ch=%u\n",
                   loop_count, pkt_count, st_buf, ble_ch);
#if BLE_DIAG
            auto rpd = radio.read_reg(nrf24::Rpd{});
            printf("[diag] RPD: ch%u signal=%u (%s)\n",
                   ble_ch,
                   static_cast<unsigned>(rpd.received_power),
                   rpd.received_power ? "> -64 dBm" : "< -64 dBm");
#endif
        }
        loop_count++;

#if BLE_DIAG
        /* Read RPD *before* switch_channel() — switch_channel() does ce_low/ce_high
         * which re-enters RX mode and resets the RPD latch (datasheet §9). */
        {
            auto rpd = radio.read_reg(nrf24::Rpd{});
            printf("[diag] RPD before switch: ch%u signal=%u (%s)\n",
                   ble_ch,
                   static_cast<unsigned>(rpd.received_power),
                   rpd.received_power ? "> -64 dBm" : "< -64 dBm");
        }
#endif

        nrf24::ble::switch_channel(radio, ble_ch);

        TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(cfg.scan_duration_ms);
        while (xTaskGetTickCount() < deadline)
        {
            if (nrf24::ble::rx_fifo_not_empty(radio))
            {
#if BLE_DIAG
                /* Verify FIFO_STATUS before reading payload.
                 * This diagnostic cross-checks that RX_EMPTY=0, confirming
                 * the FIFO genuinely has data and we're not reading
                 * garbage from an empty FIFO (which returns 0xFE). */
                auto fifo = radio.read_reg(nrf24::FifoStatus{});
                auto st = radio.read_reg(nrf24::Status{});
                printf("[diag] FIFO_STATUS: rx_empty=%u rx_full=%u  STATUS: rx_dr=%u rx_p_no=%u\n",
                       static_cast<unsigned>(fifo.rx_empty),
                       static_cast<unsigned>(fifo.rx_full),
                       static_cast<unsigned>(st.rx_dr),
                       static_cast<unsigned>(st.rx_p_no));
#endif

                pkt_count++;
                uint8_t buf[NRF24_MAX_PAYLOAD];
                radio.read_payload(buf, NRF24_MAX_PAYLOAD);
                nrf24::ble::clear_irq_flags(radio);
                radio.flush_rx();

#if BLE_DIAG
                /* Print raw whitened bytes before dewhitening for pattern analysis.
                 * This shows the data exactly as received from the nRF24L01+ RX FIFO,
                 * still in MSbit-first byte order with BLE whitening applied. */
                printf("[diag] raw:");
                for (uint8_t i = 0; i < NRF24_MAX_PAYLOAD; i++)
                {
                    printf(" %02X", buf[i]);
                }
                printf("\n");
#endif

                nrf24::ble::dewhiten(buf, NRF24_MAX_PAYLOAD, ble_ch);

                auto pdu_type = static_cast<nrf24::ble::BleAdvPduType>(
                    buf[0] & nrf24::ble::PDU_TYPE_MASK);
                uint8_t pdu_len = buf[1] & nrf24::ble::PDU_LENGTH_MASK;
                const char *type_name = nrf24::ble::pdu_type_name(pdu_type);

                printf("[ch%u] %-17s  %02X:%02X:%02X:%02X:%02X:%02X  len=%u\n",
                       ble_ch, type_name,
                       buf[7], buf[6], buf[5], buf[4], buf[3], buf[2],
                       pdu_len);

                /* For ADV_EXT_IND (BLE 5.0+), print extended header info.
                 * Per Bluetooth Core Spec Vol 6 Part B §2.3.3:
                 *   PDU body starts at buf[2], first byte is Extended Header
                 *   Length, second byte is Extended Header flags.
                 * The nRF24's 32-byte payload may only capture the start
                 * of a long extended advertising PDU — print what we have. */
                if (pdu_type == nrf24::ble::BleAdvPduType::AdvExtInd)
                {
                    if (pdu_len >= 2 && pdu_len + 2 <= NRF24_MAX_PAYLOAD)
                    {
                        uint8_t ext_hdr_len = buf[2];
                        uint8_t ext_hdr_flags = buf[3];
                        printf("  ext_hdr: len=%u flags=0x%02X  (AdvMode=%u)\n",
                               static_cast<unsigned>(ext_hdr_len),
                               static_cast<unsigned>(ext_hdr_flags),
                               static_cast<unsigned>((buf[0] >> 6) & 0x03));
                    }
                    else
                    {
                        printf("  ext_hdr: [truncated — pdu_len=%u, capture=%u]\n",
                               pdu_len, NRF24_MAX_PAYLOAD);
                    }
                }

                /* For unknown PDU types, include the type code and length
                 * in the output to aid identification per spec lookup. */
                if (static_cast<uint8_t>(pdu_type) > 7)
                {
                    printf("  note: reserved PDU type code %u, len=%u\n",
                           static_cast<unsigned>(pdu_type),
                           static_cast<unsigned>(pdu_len));
                }

                uint8_t print_len = (pdu_len + 2 <= NRF24_MAX_PAYLOAD) ? pdu_len + 2 : NRF24_MAX_PAYLOAD;
                printf("  pdu:");
                for (uint8_t i = 0; i < print_len; i++)
                {
                    printf(" %02X", buf[i]);
                }
                printf("\n");
            }

            vTaskDelay(1);
        }

        seq_i = (seq_i + 1) % cfg.total_channels();
    }
}

/* --- Entry point ---------------------------------------------------- */

extern "C" void app_main(void)
{
    hal.init({
        .miso     = PIN_MISO,
        .mosi     = PIN_MOSI,
        .sclk     = PIN_SCLK,
        .csn      = PIN_CSN,
        .ce       = PIN_CE,
        .spi_host = SPI3_HOST,
    });
    printf("NRF24L01 driver initialized\n");

    /* Test SPI communication before any register configuration.
     * This reads power-on reset values and performs write-verify tests
     * to confirm the SPI bus is working.  Also detects clone chips. */
    bool spi_ok = nrf24::diag::spi_comm_test(radio);
    if (!spi_ok) {
        printf("SPI communication test FAILED — halting.\n");
        printf("Check wiring, power, and module compatibility.\n");
        return;
    }

    nrf24::ble::configure_rx(radio);
    printf("NRF24L01 configured for BLE passive RX\n");

    nrf24::diag::verify_ble_rx(radio, rx_config);
    print_register_diagnostics(radio);

#if BLE_DIAG
    /* CE GPIO readback verification and transition timing.
     * Confirms the ESP32 GPIO hardware is actually driving CE HIGH/LOW. */
    diag_ce_readback(radio, PIN_CE);

    /* CONFIG + FIFO_STATUS dump with CE state.
     * Cross-checks that PWR_UP=1, PRIM_RX=1, CE=1 → RX mode is active. */
    diag_config_and_fifo_with_ce(radio, PIN_CE);

    /* Extended CE-high test: 5 seconds on ch37 with continuous polling.
     * Determines whether CE duration or signal detection is the issue
     * when RPD=0 on most reads during normal scanning. */
    diag_extended_ce_test(radio, PIN_CE);
#endif

    xTaskCreatePinnedToCore(ble_sniffer_task, "ble_sniffer", 4096, NULL, 5, NULL, 1);
}
