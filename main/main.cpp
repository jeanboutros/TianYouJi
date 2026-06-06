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

    nrf24::ble::configure_rx(radio);
    printf("NRF24L01 configured for BLE passive RX\n");

    nrf24::diag::verify_ble_rx(radio, rx_config);
    print_register_diagnostics(radio);

    xTaskCreatePinnedToCore(ble_sniffer_task, "ble_sniffer", 4096, NULL, 5, NULL, 1);
}
