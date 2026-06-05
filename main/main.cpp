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
#include "driver/gpio.h"
#include "nrf24_espidf/hal_espidf.h"
#include <nrf24l01plus/driver.h>
#include <nrf24l01plus/ble.h>
#include <nrf24l01plus/ble_config.h>
#include <nrf24l01plus/registers.h>

/* --- Pin definitions ------------------------------------------------ */
static constexpr gpio_num_t PIN_MISO = GPIO_NUM_19;
static constexpr gpio_num_t PIN_MOSI = GPIO_NUM_23;
static constexpr gpio_num_t PIN_SCLK = GPIO_NUM_18;
static constexpr gpio_num_t PIN_CSN  = GPIO_NUM_17;
static constexpr gpio_num_t PIN_CE   = GPIO_NUM_5;

/* PDU type names (lower 4 bits of PDU header byte 0)                 */
static const char *pdu_type_names[] = {
    "ADV_IND", "ADV_DIRECT_IND", "ADV_NONCONN_IND", "SCAN_REQ",
    "SCAN_RSP", "CONNECT_IND", "ADV_SCAN_IND", "UNKNOWN"};

static nrf24::EspIdfHal hal;
static nrf24::Driver radio(hal);

/* --- NRF24 register diagnostic -------------------------------------- */

static bool nrf24_diag(void)
{
    printf("\n=== NRF24L01+ Diagnostic ===\n");
    bool all_pass = true;

    uint8_t raw_status = radio.read_reg(nrf24::reg::STATUS);
    bool spi_ok = (raw_status != 0xFF);
    printf("  SPI comms           STATUS=0x%02X  [%s]\n",
           raw_status, spi_ok ? "PASS" : "FAIL -- check wiring / power");
    if (!spi_ok) {
        printf("  Cannot reach device -- aborting further checks.\n\n");
        return false;
    }

    /* Build expected register values using typed structs */
    nrf24::Config exp_config;
    exp_config.power_mode   = nrf24::PowerMode::Up;
    exp_config.primary      = nrf24::PrimaryMode::RX;
    exp_config.crc_mode     = nrf24::CrcMode::Disabled;
    exp_config.crc_encoding = nrf24::CrcEncoding::Bytes1;
    exp_config.irq_mask     = nrf24::IrqMask::None;

    nrf24::EnAa exp_en_aa;
    for (int i = 0; i < 6; ++i) exp_en_aa.pipe[i] = false;

    nrf24::EnRxAddr exp_en_rx;
    for (int i = 0; i < 6; ++i) exp_en_rx.pipe[i] = false;
    exp_en_rx.pipe[0] = true;

    nrf24::SetupAw exp_aw;
    exp_aw.address_width = nrf24::AddressWidth::Bytes4;

    nrf24::SetupRetr exp_retr;
    exp_retr.delay = nrf24::AutoRetransmitDelay::Us250;
    exp_retr.count = nrf24::AutoRetransmitCount::Disabled;

    nrf24::RfCh exp_rf_ch;
    exp_rf_ch.channel = nrf24::ble::ADV_CHANNELS[0].rf_ch;

    nrf24::RfSetup exp_rf;
    exp_rf.data_rate = nrf24::DataRate::Mbps1;
    exp_rf.tx_power  = nrf24::TxPower::dBm0;
    exp_rf.cont_wave = nrf24::ContWave::Disabled;
    exp_rf.pll_lock  = nrf24::PllLock::Disabled;

    nrf24::RxPw exp_pw{32};

    const struct {
        const char *name;
        uint8_t     reg;
        uint8_t     expected;
        uint8_t     mask;
    } checks[] = {
        { "CONFIG",     nrf24::reg::CONFIG,     exp_config.to_byte(),  0xFF },
        { "EN_AA",      nrf24::reg::EN_AA,      exp_en_aa.to_byte(),   0xFF },
        { "EN_RXADDR",  nrf24::reg::EN_RXADDR,  exp_en_rx.to_byte(),   0xFF },
        { "SETUP_AW",   nrf24::reg::SETUP_AW,   exp_aw.to_byte(),      0xFF },
        { "SETUP_RETR", nrf24::reg::SETUP_RETR, exp_retr.to_byte(),    0xFF },
        { "RF_CH",      nrf24::reg::RF_CH,      exp_rf_ch.to_byte(),   0x7F },
        { "RF_SETUP",   nrf24::reg::RF_SETUP,   exp_rf.to_byte(),      0xFF },
        { "RX_PW_P0",   nrf24::reg::RX_PW_P0,  exp_pw.to_byte(),      0x3F },
    };

    for (int i = 0; i < (int)(sizeof(checks) / sizeof(checks[0])); i++) {
        uint8_t val  = radio.read_reg(checks[i].reg);
        uint8_t got  = val  & checks[i].mask;
        uint8_t want = checks[i].expected & checks[i].mask;
        bool    pass = (got == want);
        printf("  %-10s  reg=0x%02X  exp=0x%02X  got=0x%02X  [%s]\n",
               checks[i].name, checks[i].reg, want, got,
               pass ? "PASS" : "FAIL");
        if (!pass) all_pass = false;
    }

    uint8_t addr[4];

    radio.read_reg_multi(nrf24::reg::RX_ADDR_P0, addr, 4);
    bool rx_ok = (memcmp(addr, nrf24::ble::ADV_ACCESS_ADDR, 4) == 0);
    printf("  RX_ADDR_P0  exp=%02X:%02X:%02X:%02X  got=%02X:%02X:%02X:%02X  [%s]\n",
           nrf24::ble::ADV_ACCESS_ADDR[0], nrf24::ble::ADV_ACCESS_ADDR[1],
           nrf24::ble::ADV_ACCESS_ADDR[2], nrf24::ble::ADV_ACCESS_ADDR[3],
           addr[0], addr[1], addr[2], addr[3], rx_ok ? "PASS" : "FAIL");
    if (!rx_ok) all_pass = false;

    radio.read_reg_multi(nrf24::reg::TX_ADDR, addr, 4);
    bool tx_ok = (memcmp(addr, nrf24::ble::ADV_ACCESS_ADDR, 4) == 0);
    printf("  TX_ADDR     exp=%02X:%02X:%02X:%02X  got=%02X:%02X:%02X:%02X  [%s]\n",
           nrf24::ble::ADV_ACCESS_ADDR[0], nrf24::ble::ADV_ACCESS_ADDR[1],
           nrf24::ble::ADV_ACCESS_ADDR[2], nrf24::ble::ADV_ACCESS_ADDR[3],
           addr[0], addr[1], addr[2], addr[3], tx_ok ? "PASS" : "FAIL");
    if (!tx_ok) all_pass = false;

    printf("\nDiagnostic: %s\n\n",
           all_pass ? "PASS -- device ready" : "FAIL -- check connections / power");
    return all_pass;
}

/* --- BLE sniffer task ----------------------------------------------- */

static void ble_sniffer_task(void *arg)
{
    uint8_t  ch_i        = 0;
    uint32_t loop_count  = 0;
    uint32_t pkt_count   = 0;
    TickType_t last_hb   = xTaskGetTickCount();
    printf("BLE sniffer started -- scanning ch37 / ch38 / ch39\n");

    while (1)
    {
        if ((xTaskGetTickCount() - last_hb) >= pdMS_TO_TICKS(5000))
        {
            uint8_t st = radio.read_reg(nrf24::reg::STATUS);
            printf("[hb] loops=%" PRIu32 "  pkts=%" PRIu32
                   "  STATUS=0x%02X  ch=%s\n",
                   loop_count, pkt_count, st,
                   nrf24::ble::ADV_CHANNELS[ch_i].name);
            last_hb = xTaskGetTickCount();
        }
        loop_count++;

        /* Switch to next advertising channel */
        nrf24::ble::switch_channel(radio, ch_i);

        /* Listen for 50 ms then rotate */
        TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(50);
        while (xTaskGetTickCount() < deadline)
        {
            if (nrf24::ble::rx_available(radio))
            {
                pkt_count++;
                uint8_t buf[32];
                radio.read_payload(buf, 32);
                nrf24::ble::clear_irq_flags(radio);
                radio.flush_rx();

                nrf24::ble::dewhiten(buf, 32,
                                     nrf24::ble::ADV_CHANNELS[ch_i].ch_idx);

                uint8_t pdu_type = buf[0] & 0x0F;
                uint8_t pdu_len = buf[1] & 0x3F;
                const char *type = pdu_type_names[pdu_type < 7 ? pdu_type : 7];

                printf("[%s] %-17s  %02X:%02X:%02X:%02X:%02X:%02X  len=%u\n",
                       nrf24::ble::ADV_CHANNELS[ch_i].name, type,
                       buf[7], buf[6], buf[5], buf[4], buf[3], buf[2],
                       pdu_len);

                uint8_t print_len = (pdu_len + 2 <= 32) ? pdu_len + 2 : 32;
                printf("  pdu:");
                for (uint8_t i = 0; i < print_len; i++)
                {
                    printf(" %02X", buf[i]);
                }
                printf("\n");
            }

            vTaskDelay(1);
        }

        ch_i = (ch_i + 1) % 3;
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

    nrf24_diag();

    /* Release MOSI to reduce 2.4 GHz noise during RX-only operation */
    gpio_set_direction(hal.mosi_pin(), GPIO_MODE_INPUT);
    printf("MOSI (GPIO%d) released to input -- RF noise reduced\n", hal.mosi_pin());

    xTaskCreatePinnedToCore(ble_sniffer_task, "ble_sniffer", 4096, NULL, 5, NULL, 1);
}
