/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "nrf24_espidf/hal_espidf.h"
#include <nrf24l01plus/driver.h>
#include <nrf24l01plus/ble.h>
#include <nrf24l01plus/ble_config.h>
#include <nrf24l01plus/diag.h>
#include <nrf24l01plus/diag_boot.h>
#include <nrf24l01plus/registers.h>

/* --- Pin definitions ------------------------------------------------ */
static constexpr gpio_num_t PIN_MISO = GPIO_NUM_19;
static constexpr gpio_num_t PIN_MOSI = GPIO_NUM_23;
static constexpr gpio_num_t PIN_SCLK = GPIO_NUM_18;
static constexpr gpio_num_t PIN_CSN  = GPIO_NUM_17;
static constexpr gpio_num_t PIN_CE   = GPIO_NUM_4;

/** @brief Maximum nRF24L01+ payload size in bytes (FIFO width, not BLE PDU limit). */
static constexpr uint8_t NRF24_MAX_PAYLOAD = 32;

static nrf24::EspIdfHal hal;
static nrf24::Driver radio(hal);

/* Print heartbeat every 500 loop iterations (reduces serial noise) */
static constexpr uint32_t HB_LOOP_INTERVAL = 500;

/** @brief Runtime diagnostic flags for the sniffer loop.
 *
 * Replaces the compile-time BLE_DIAG macro. Each flag controls an
 * independent diagnostic feature that can be toggled at runtime.
 */
struct SnifferDiagFlags {
    bool rpd_monitor     = true;   ///< Print RPD value each heartbeat
    bool fifo_crosscheck = true;   ///< Print FIFO_STATUS before payload read
    bool raw_dump        = false;  ///< Print raw whitened bytes before dewhitening
};

static SnifferDiagFlags diag_flags;

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
            if (diag_flags.rpd_monitor) {
                auto rpd = radio.read_reg(nrf24::Rpd{});
                printf("[diag] RPD: ch%u signal=%u (%s)\n",
                       ble_ch,
                       static_cast<unsigned>(rpd.received_power),
                       rpd.received_power ? "> -64 dBm" : "< -64 dBm");
            }
        }
        loop_count++;

        if (diag_flags.rpd_monitor) {
            /* Read RPD before switch_channel() — switch_channel() does ce_low/ce_high
             * which re-enters RX mode and resets the RPD latch (datasheet §9). */
            auto rpd = radio.read_reg(nrf24::Rpd{});
            printf("[diag] RPD before switch: ch%u signal=%u (%s)\n",
                   ble_ch,
                   static_cast<unsigned>(rpd.received_power),
                   rpd.received_power ? "> -64 dBm" : "< -64 dBm");
        }

        nrf24::ble::switch_channel(radio, ble_ch);

        TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(cfg.scan_duration_ms);
        while (xTaskGetTickCount() < deadline)
        {
            if (nrf24::ble::rx_fifo_not_empty(radio))
            {
                if (diag_flags.fifo_crosscheck) {
                    /* Verify FIFO_STATUS before reading payload.
                     * Cross-checks that RX_EMPTY=0, confirming the FIFO
                     * genuinely has data and we're not reading garbage
                     * from an empty FIFO (which returns 0xFE). */
                    auto fifo = radio.read_reg(nrf24::FifoStatus{});
                    auto st = radio.read_reg(nrf24::Status{});
                    printf("[diag] FIFO_STATUS: rx_empty=%u rx_full=%u  STATUS: rx_dr=%u rx_p_no=%u\n",
                           static_cast<unsigned>(fifo.rx_empty),
                           static_cast<unsigned>(fifo.rx_full),
                           static_cast<unsigned>(st.rx_dr),
                           static_cast<unsigned>(st.rx_p_no));
                }

                pkt_count++;
                uint8_t buf[NRF24_MAX_PAYLOAD];
                radio.read_payload(buf, NRF24_MAX_PAYLOAD);
                nrf24::ble::clear_irq_flags(radio);
                radio.flush_rx();

                if (diag_flags.raw_dump) {
                    /* Print raw whitened bytes before dewhitening for pattern analysis.
                     * Shows the data exactly as received from the nRF24L01+ RX FIFO,
                     * still in MSbit-first byte order with BLE whitening applied. */
                    printf("[diag] raw:");
                    for (uint8_t i = 0; i < NRF24_MAX_PAYLOAD; i++) {
                        printf(" %02X", buf[i]);
                    }
                    printf("\n");
                }

                nrf24::ble::dewhiten(buf, NRF24_MAX_PAYLOAD, ble_ch);

                auto pdu_type = static_cast<nrf24::ble::BleAdvPduType>(
                    buf[0] & nrf24::ble::PDU_TYPE_MASK);
                uint8_t pdu_len = buf[1] & nrf24::ble::PDU_LENGTH_MASK;
                const char *type_name = nrf24::ble::pdu_type_name(pdu_type);

                uint8_t adv_addr[6];
                if (nrf24::ble::adv_address(buf, adv_addr)) {
                    printf("[ch%u] %-17s  %s  len=%u\n",
                           ble_ch, type_name,
                           nrf24::ble::format_address(adv_addr),
                           pdu_len);
                } else {
                    /* PDU type without a fixed-offset AdvA (SCAN_REQ, CONNECT_IND, ADV_EXT_IND) */
                    printf("[ch%u] %-17s  len=%u\n", ble_ch, type_name, pdu_len);
                }

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

    /* Run structured boot diagnostics (SPI, BLE config, CE state, extended test).
     * Replaces the former ad-hoc spi_comm_test + verify_ble_rx + print_register_diagnostics
     * sequence, plus the BLE_DIAG-gated CE tests.  If diagnostics fail after max retries,
     * enter deep sleep for 60 seconds and restart (decision D-1). */
    nrf24::diag::DiagOpts opts;
    opts.verbosity         = nrf24::diag::DiagVerbosity::Detailed;
    opts.retry_count       = 3;
    opts.retry_delay_ms    = 5000;
    opts.extended_test_ch  = 37;

    auto result = nrf24::diag::full_boot_diagnostic(radio, rx_config, opts);
    if (!result.overall_pass()) {
        printf("Boot diagnostics FAILED after %u attempts\n",
               static_cast<unsigned>(opts.retry_count) + 1);
        printf("First failure: phase %d\n",
               static_cast<int>(result.first_fail_phase()));
        printf("Entering deep sleep for 60 seconds, then restarting...\n");
        esp_sleep_enable_timer_wakeup(60 * 1000000);  /* 60 seconds in microseconds */
        esp_deep_sleep_start();
        /* Does not return — device restarts after deep sleep */
    }

    xTaskCreatePinnedToCore(ble_sniffer_task, "ble_sniffer", 4096, NULL, 5, NULL, 1);
}
