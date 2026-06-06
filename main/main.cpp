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
#include "nrf24l01plus/registers/rf_ch.h"
#include "nrf24l01plus/registers/en_rxaddr.h"
#include "nrf24l01plus/registers/rx_pw.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
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

/* BLE PDU header masks (Bluetooth Core Spec Vol 6 Part B §2.1) */
static constexpr uint8_t BLE_PDU_TYPE_MASK   = 0x0F; ///< Bits [3:0] of PDU header byte 0
static constexpr uint8_t BLE_PDU_LENGTH_MASK = 0x3F; ///< Bits [5:0] of PDU header byte 1

/** @brief Maximum nRF24L01+ payload size in bytes (FIFO width, not BLE PDU limit). */
static constexpr uint8_t NRF24_MAX_PAYLOAD = 32;

/* PDU type names (lower 4 bits of PDU header byte 0)                 */
static const char *pdu_type_names[] = {
    "ADV_IND", "ADV_DIRECT_IND", "ADV_NONCONN_IND", "SCAN_REQ",
    "SCAN_RSP", "CONNECT_IND", "ADV_SCAN_IND", "UNKNOWN"};

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
 * Reads back CONFIG, RF_CH, EN_RXADDR, RX_PW_P0, and RX_ADDR_P0 to
 * confirm the device was programmed correctly after configure_rx().
 * Must be called AFTER nrf24::ble::configure_rx() and BEFORE the
 * sniffer task starts.
 *
 * @param radio  Driver instance (already configured for BLE RX).
 */
static void print_register_diagnostics(nrf24::Driver &radio)
{
    printf("\n=== Register read-back ===\n");

    /* CONFIG — expect PWR_UP=1, PRIM_RX=1, CRC disabled (raw 0x03) */
    auto cfg = radio.read_reg(nrf24::Config{});
    printf("  CONFIG      PWR_UP=%u  PRIM_RX=%u  EN_CRC=%u  CRCO=%u  (raw 0x%02X)\n",
           static_cast<unsigned>(cfg.power_mode),
           static_cast<unsigned>(cfg.primary),
           static_cast<unsigned>(cfg.crc_mode),
           static_cast<unsigned>(cfg.crc_encoding),
           cfg.to_byte());

    /* RF_CH — should match the initial BLE channel frequency */
    auto rf_ch = radio.read_reg(nrf24::RfCh{});
    printf("  RF_CH       channel=%u  freq=%u MHz  (raw 0x%02X)\n",
           rf_ch.channel, 2400 + rf_ch.channel,
           rf_ch.to_byte());

    /* EN_RXADDR — expect only pipe 0 enabled */
    auto en_rx = radio.read_reg(nrf24::EnRxAddr{});
    printf("  EN_RXADDR  P0=%d P1=%d P2=%d P3=%d P4=%d P5=%d  (raw 0x%02X)\n",
           en_rx.pipe[0], en_rx.pipe[1], en_rx.pipe[2],
           en_rx.pipe[3], en_rx.pipe[4], en_rx.pipe[5],
           en_rx.to_byte());

    /* RX_PW_P0 — expect 32 bytes (0x20) */
    auto pw0 = radio.read_reg(nrf24::RxPwP0{});
    printf("  RX_PW_P0    payload_width=%u  (raw 0x%02X)\n",
           pw0.payload_width,
           pw0.to_byte());

    /* RX_ADDR_P0 — expect {0x6B, 0x7D, 0x91, 0x71} (BLE adv access addr) */
    uint8_t addr[4];
    radio.read_reg_multi(nrf24::reg::RX_ADDR_P0, addr, 4);
    printf("  RX_ADDR_P0  %02X:%02X:%02X:%02X  (exp 6B:7D:91:71)\n",
           addr[0], addr[1], addr[2], addr[3]);

    printf("=== End register read-back ===\n\n");
}

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
            if (nrf24::ble::rx_available(radio))
            {
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

                uint8_t pdu_type = buf[0] & BLE_PDU_TYPE_MASK;
                uint8_t pdu_len = buf[1] & BLE_PDU_LENGTH_MASK;
                const char *type = pdu_type_names[pdu_type < 7 ? pdu_type : 7];

                printf("[ch%u] %-17s  %02X:%02X:%02X:%02X:%02X:%02X  len=%u\n",
                       ble_ch, type,
                       buf[7], buf[6], buf[5], buf[4], buf[3], buf[2],
                       pdu_len);

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

    nrf24::ble::configure_rx(radio);
    printf("NRF24L01 configured for BLE passive RX\n");

    nrf24::diag::verify_ble_rx(radio, rx_config);
    print_register_diagnostics(radio);

    xTaskCreatePinnedToCore(ble_sniffer_task, "ble_sniffer", 4096, NULL, 5, NULL, 1);
}
