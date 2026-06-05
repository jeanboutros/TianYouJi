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
#include "hal_espidf.h"
#include <nrf24l01plus/driver.h>
#include <nrf24l01plus/ble.h>
#include <nrf24l01plus/registers.h>

/* --- Pin definitions ------------------------------------------------ */
#define PIN_MISO GPIO_NUM_19
#define PIN_MOSI GPIO_NUM_23
#define PIN_SCLK GPIO_NUM_18
#define PIN_CSN  GPIO_NUM_17
#define PIN_CE   GPIO_NUM_5

/* --- NRF24L01+ register addresses ---------------------------------- */
#define REG_CONFIG 0x00
#define REG_EN_AA 0x01
#define REG_EN_RXADDR 0x02
#define REG_SETUP_AW 0x03
#define REG_SETUP_RETR 0x04
#define REG_RF_CH 0x05
#define REG_RF_SETUP 0x06
#define REG_STATUS 0x07
#define REG_RX_ADDR_P0 0x0A
#define REG_TX_ADDR 0x10
#define REG_RX_PW_P0 0x11

/* --- BLE advertising channels -> NRF24 RF_CH ------------------------ */
#define BLE_CH37_RFCH 2
#define BLE_CH38_RFCH 26
#define BLE_CH39_RFCH 80

/* BLE advertising access address 0x8E89BED6, each byte bit-reversed   */
static const uint8_t ble_adv_addr[4] = {0x6B, 0x7D, 0x91, 0x71};

/* PDU type names (lower 4 bits of PDU header byte 0)                 */
static const char *pdu_type_names[] = {
    "ADV_IND", "ADV_DIRECT_IND", "ADV_NONCONN_IND", "SCAN_REQ",
    "SCAN_RSP", "CONNECT_IND", "ADV_SCAN_IND", "UNKNOWN"};

/* BLE advertising channel descriptor table                           */
static const struct
{
    uint8_t rf_ch;
    uint8_t ch_idx;
    const char *name;
} ble_adv_ch[3] = {
    {BLE_CH37_RFCH, 37, "ch37"},
    {BLE_CH38_RFCH, 38, "ch38"},
    {BLE_CH39_RFCH, 39, "ch39"},
};

static nrf24::EspIdfHal hal;
static nrf24::Driver radio(hal);

/* --- NRF24 BLE RX configuration ------------------------------------- */

static void nrf24_config_ble_rx(void)
{
    radio.write_reg(REG_CONFIG, 0x03);     /* PWR_UP=1, PRIM_RX=1, CRC off      */
    radio.write_reg(REG_EN_AA, 0x00);      /* disable auto-ACK                   */
    radio.write_reg(REG_EN_RXADDR, 0x01);  /* pipe 0 only                        */
    radio.write_reg(REG_SETUP_AW, 0x02);   /* 4-byte address width               */
    radio.write_reg(REG_SETUP_RETR, 0x00); /* no retransmit                      */
    radio.write_reg(REG_RF_CH, BLE_CH37_RFCH);
    {
        nrf24::RfSetup rf;
        rf.data_rate = nrf24::DataRate::Mbps1;
        rf.tx_power  = nrf24::TxPower::dBm0;
        rf.cont_wave = nrf24::ContWave::Disabled;
        rf.pll_lock  = nrf24::PllLock::Disabled;
        radio.write_reg(REG_RF_SETUP, rf.to_byte());
    }
    radio.write_reg(REG_RX_PW_P0, 32);   /* fixed 32-byte payload width        */
    radio.write_reg_multi(REG_RX_ADDR_P0, ble_adv_addr, 4);
    radio.write_reg_multi(REG_TX_ADDR, ble_adv_addr, 4);
    radio.flush_rx();
    radio.write_reg(REG_STATUS, 0x70); /* clear interrupt flags              */
    vTaskDelay(pdMS_TO_TICKS(2));      /* wait >= 1.5 ms for power-up        */
    printf("NRF24L01 configured for BLE passive RX\n");
}

/* --- NRF24 register diagnostic -------------------------------------- */

static bool nrf24_diag(void)
{
    printf("\n=== NRF24L01+ Diagnostic ===\n");
    bool all_pass = true;

    uint8_t status = radio.read_reg(REG_STATUS);
    bool spi_ok = (status != 0xFF);
    printf("  SPI comms           STATUS=0x%02X  [%s]\n",
           status, spi_ok ? "PASS" : "FAIL -- check wiring / power");
    if (!spi_ok) {
        printf("  Cannot reach device -- aborting further checks.\n\n");
        return false;
    }

    const struct {
        const char *name;
        uint8_t     reg;
        uint8_t     expected;
        uint8_t     mask;
    } checks[] = {
        { "CONFIG",     REG_CONFIG,     0x03,          0xFF },
        { "EN_AA",      REG_EN_AA,      0x00,          0xFF },
        { "EN_RXADDR",  REG_EN_RXADDR,  0x01,          0xFF },
        { "SETUP_AW",   REG_SETUP_AW,   0x02,          0xFF },
        { "SETUP_RETR", REG_SETUP_RETR, 0x00,          0xFF },
        { "RF_CH",      REG_RF_CH,      BLE_CH37_RFCH, 0x7F },
        { "RF_SETUP",   REG_RF_SETUP,   nrf24::to_reg(nrf24::DataRate::Mbps1)
                                      | nrf24::to_reg(nrf24::TxPower::dBm0)
                                      | nrf24::to_reg(nrf24::ContWave::Disabled)
                                      | nrf24::to_reg(nrf24::PllLock::Disabled), 0xFF },
        { "RX_PW_P0",   REG_RX_PW_P0,  32,            0x3F },
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

    radio.read_reg_multi(REG_RX_ADDR_P0, addr, 4);
    bool rx_ok = (memcmp(addr, ble_adv_addr, 4) == 0);
    printf("  RX_ADDR_P0  exp=%02X:%02X:%02X:%02X  got=%02X:%02X:%02X:%02X  [%s]\n",
           ble_adv_addr[0], ble_adv_addr[1], ble_adv_addr[2], ble_adv_addr[3],
           addr[0], addr[1], addr[2], addr[3], rx_ok ? "PASS" : "FAIL");
    if (!rx_ok) all_pass = false;

    radio.read_reg_multi(REG_TX_ADDR, addr, 4);
    bool tx_ok = (memcmp(addr, ble_adv_addr, 4) == 0);
    printf("  TX_ADDR     exp=%02X:%02X:%02X:%02X  got=%02X:%02X:%02X:%02X  [%s]\n",
           ble_adv_addr[0], ble_adv_addr[1], ble_adv_addr[2], ble_adv_addr[3],
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
            uint8_t st = radio.read_reg(REG_STATUS);
            printf("[hb] loops=%" PRIu32 "  pkts=%" PRIu32
                   "  STATUS=0x%02X  ch=%s\n",
                   loop_count, pkt_count, st, ble_adv_ch[ch_i].name);
            last_hb = xTaskGetTickCount();
        }
        loop_count++;

        radio.ce_low();
        radio.write_reg(REG_RF_CH, ble_adv_ch[ch_i].rf_ch);
        radio.write_reg(REG_STATUS, 0x70);
        radio.flush_rx();
        radio.ce_high();

        TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(50);
        while (xTaskGetTickCount() < deadline)
        {
            uint8_t st = radio.read_reg(REG_STATUS);

            if (st & (1 << 6))
            {
                pkt_count++;
                uint8_t buf[32];
                radio.read_payload(buf, 32);
                radio.write_reg(REG_STATUS, 0x70);
                radio.flush_rx();

                nrf24::ble::dewhiten(buf, 32, ble_adv_ch[ch_i].ch_idx);

                uint8_t pdu_type = buf[0] & 0x0F;
                uint8_t pdu_len = buf[1] & 0x3F;
                const char *type = pdu_type_names[pdu_type < 7 ? pdu_type : 7];

                printf("[%s] %-17s  %02X:%02X:%02X:%02X:%02X:%02X  len=%u\n",
                       ble_adv_ch[ch_i].name, type,
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

    nrf24_config_ble_rx();
    nrf24_diag();

    /* Release MOSI to reduce 2.4 GHz noise during RX-only operation */
    gpio_set_direction(PIN_MOSI, GPIO_MODE_INPUT);
    printf("MOSI (GPIO%d) released to input -- RF noise reduced\n", PIN_MOSI);

    xTaskCreatePinnedToCore(ble_sniffer_task, "ble_sniffer", 4096, NULL, 5, NULL, 1);
}
