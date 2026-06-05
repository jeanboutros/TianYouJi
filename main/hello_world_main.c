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
#include "driver/spi_master.h"
#include "driver/gpio.h"

/* ─── Pin definitions ──────────────────────────────────────────────── */
#define NRF_SPI_HOST  SPI3_HOST
#define PIN_MISO      19
#define PIN_MOSI      23
#define PIN_SCLK      18
#define PIN_CSN       17
#define PIN_CE        5

/* ─── NRF24L01+ register addresses ────────────────────────────────── */
#define REG_CONFIG     0x00
#define REG_EN_AA      0x01
#define REG_EN_RXADDR  0x02
#define REG_SETUP_AW   0x03
#define REG_SETUP_RETR 0x04
#define REG_RF_CH      0x05
#define REG_RF_SETUP   0x06
#define REG_STATUS     0x07
#define REG_RX_ADDR_P0 0x0A
#define REG_TX_ADDR    0x10
#define REG_RX_PW_P0   0x11

/* ─── BLE advertising channels → NRF24 RF_CH ──────────────────────── */
/*  Frequency = 2400 + RF_CH (MHz).  BLE adv at 2402/2426/2480 MHz.  */
#define BLE_CH37_RFCH  2
#define BLE_CH38_RFCH  26
#define BLE_CH39_RFCH  80

/* BLE advertising access address 0x8E89BED6, LSByte first            */
static const uint8_t ble_adv_addr[4] = {0xD6, 0xBE, 0x89, 0x8E};

/* PDU type names (lower 4 bits of PDU header byte 0)                 */
static const char *pdu_type_names[] = {
    "ADV_IND", "ADV_DIRECT_IND", "ADV_NONCONN_IND", "SCAN_REQ",
    "SCAN_RSP", "CONNECT_IND",   "ADV_SCAN_IND",    "UNKNOWN"
};

/* BLE advertising channel descriptor table                           */
static const struct { uint8_t rf_ch; uint8_t ch_idx; const char *name; }
ble_adv_ch[3] = {
    { BLE_CH37_RFCH, 37, "ch37" },
    { BLE_CH38_RFCH, 38, "ch38" },
    { BLE_CH39_RFCH, 39, "ch39" },
};

static spi_device_handle_t nrf_spi_handle;

/* ─── Low-level SPI helpers ────────────────────────────────────────── */

static uint8_t nrf24_read_reg(uint8_t reg)
{
    spi_transaction_t t = {
        .cmd    = 0x00 | (reg & 0x1F),   /* R_REGISTER */
        .length = 8,
        .flags  = SPI_TRANS_USE_RXDATA,
    };
    ESP_ERROR_CHECK(spi_device_polling_transmit(nrf_spi_handle, &t));
    return t.rx_data[0];
}

static void nrf24_write_reg(uint8_t reg, uint8_t value)
{
    spi_transaction_t t = {
        .cmd     = 0x20 | (reg & 0x1F),  /* W_REGISTER */
        .length  = 8,
        .flags   = SPI_TRANS_USE_TXDATA,
        .tx_data = {value, 0, 0, 0},
    };
    ESP_ERROR_CHECK(spi_device_polling_transmit(nrf_spi_handle, &t));
}

static void nrf24_write_reg_multi(uint8_t reg, const uint8_t *data, uint8_t len)
{
    spi_transaction_t t = {
        .cmd       = 0x20 | (reg & 0x1F),
        .length    = (uint32_t)len * 8,
        .tx_buffer = data,
    };
    ESP_ERROR_CHECK(spi_device_polling_transmit(nrf_spi_handle, &t));
}

static void nrf24_read_payload(uint8_t *buf, uint8_t len)
{
    spi_transaction_t t = {
        .cmd       = 0x61,                /* R_RX_PAYLOAD */
        .length    = (uint32_t)len * 8,
        .rx_buffer = buf,
    };
    ESP_ERROR_CHECK(spi_device_polling_transmit(nrf_spi_handle, &t));
}

static void nrf24_flush_rx(void)
{
    spi_transaction_t t = { .cmd = 0xE2, .length = 0 }; /* FLUSH_RX */
    ESP_ERROR_CHECK(spi_device_polling_transmit(nrf_spi_handle, &t));
}

/* ─── CE pin control ───────────────────────────────────────────────── */

static void nrf24_ce_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << PIN_CE),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
    gpio_set_level(PIN_CE, 0);
}

static inline void nrf24_ce_high(void) { gpio_set_level(PIN_CE, 1); }
static inline void nrf24_ce_low(void)  { gpio_set_level(PIN_CE, 0); }

/* ─── BLE data whitening ───────────────────────────────────────────── */
/*
 * BLE whitens data with a 7-bit LFSR (poly x^7+x^4+1, Core Spec §3.2).
 * Init: bit6 = 1, bits5:0 = channel_index[5:0].
 * XOR is self-inverse: same function whitens and de-whitens.
 */
static void ble_dewhiten(uint8_t *data, uint8_t len, uint8_t channel_idx)
{
    uint8_t lfsr = (channel_idx & 0x3F) | 0x40;
    for (uint8_t i = 0; i < len; i++) {
        uint8_t w = 0;
        for (int b = 0; b < 8; b++) {
            if (lfsr & 0x01) {
                w |= (1 << b);
                lfsr ^= 0x48;  /* feedback: x^7(bit6) and x^4(bit3) after right-shift */
            }
            lfsr >>= 1;
        }
        data[i] ^= w;
    }
}

/* ─── SPI bus and device init ──────────────────────────────────────── */

static void init_spi_bus(void)
{
    spi_bus_config_t bus_cfg = {
        .miso_io_num     = PIN_MISO,
        .mosi_io_num     = PIN_MOSI,
        .sclk_io_num     = PIN_SCLK,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = 33,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(NRF_SPI_HOST, &bus_cfg, SPI_DMA_DISABLED));
    printf("SPI bus initialized on SPI3_HOST\n");
}

static void init_nrf24l01(void)
{
    spi_device_interface_config_t dev_cfg = {
        .command_bits   = 8,
        .address_bits   = 0,
        .queue_size     = 1,
        .clock_speed_hz = 8000000,
        .mode           = 0,
        .spics_io_num   = PIN_CSN,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(NRF_SPI_HOST, &dev_cfg, &nrf_spi_handle));
    printf("NRF24L01 SPI device added\n");
}

/* ─── NRF24 BLE RX configuration ──────────────────────────────────── */

static void nrf24_config_ble_rx(void)
{
    nrf24_write_reg(REG_CONFIG,     0x03); /* PWR_UP=1, PRIM_RX=1, CRC off      */
    nrf24_write_reg(REG_EN_AA,      0x00); /* disable auto-ACK                   */
    nrf24_write_reg(REG_EN_RXADDR,  0x01); /* pipe 0 only                        */
    nrf24_write_reg(REG_SETUP_AW,   0x02); /* 4-byte address width               */
    nrf24_write_reg(REG_SETUP_RETR, 0x00); /* no retransmit                      */
    nrf24_write_reg(REG_RF_CH,      BLE_CH37_RFCH);
    nrf24_write_reg(REG_RF_SETUP,   0x06); /* 1 Mbps, 0 dBm                      */
    nrf24_write_reg(REG_RX_PW_P0,   32);   /* fixed 32-byte payload width        */
    nrf24_write_reg_multi(REG_RX_ADDR_P0, ble_adv_addr, 4);
    nrf24_write_reg_multi(REG_TX_ADDR,    ble_adv_addr, 4);
    nrf24_flush_rx();
    nrf24_write_reg(REG_STATUS, 0x70);     /* clear interrupt flags              */
    vTaskDelay(pdMS_TO_TICKS(2));          /* wait ≥1.5 ms for power-up          */
    printf("NRF24L01 configured for BLE passive RX\n");
}

/* ─── BLE sniffer task ──────────────────────────────────────────────── */

static void ble_sniffer_task(void *arg)
{
    uint8_t ch_i = 0;
    printf("BLE sniffer started — scanning ch37 / ch38 / ch39\n");

    while (1) {
        /* Switch to next advertising channel */
        nrf24_ce_low();
        nrf24_write_reg(REG_RF_CH,  ble_adv_ch[ch_i].rf_ch);
        nrf24_write_reg(REG_STATUS, 0x70);
        nrf24_flush_rx();
        nrf24_ce_high();

        /* Listen for 10 ms then rotate */
        TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(10);
        while (xTaskGetTickCount() < deadline) {
            uint8_t status = nrf24_read_reg(REG_STATUS);

            if (status & (1 << 6)) {    /* RX_DR: packet in FIFO */
                uint8_t buf[32];
                nrf24_read_payload(buf, 32);
                nrf24_write_reg(REG_STATUS, 0x70);
                nrf24_flush_rx();

                ble_dewhiten(buf, 32, ble_adv_ch[ch_i].ch_idx);

                /* Decode BLE PDU header (bytes 0-1) */
                uint8_t pdu_type = buf[0] & 0x0F;
                uint8_t pdu_len  = buf[1] & 0x3F;
                const char *type = pdu_type_names[pdu_type < 7 ? pdu_type : 7];

                /* Bytes 2-7: advertiser address, LSByte first → print MSByte first */
                printf("[%s] %-17s  %02X:%02X:%02X:%02X:%02X:%02X  len=%u\n",
                       ble_adv_ch[ch_i].name, type,
                       buf[7], buf[6], buf[5], buf[4], buf[3], buf[2],
                       pdu_len);

                /* Raw PDU bytes (header + payload, capped at 32) */
                uint8_t print_len = (pdu_len + 2 <= 32) ? pdu_len + 2 : 32;
                printf("  pdu:");
                for (uint8_t i = 0; i < print_len; i++) {
                    printf(" %02X", buf[i]);
                }
                printf("\n");
            }

            vTaskDelay(1);
        }

        ch_i = (ch_i + 1) % 3;
    }
}

/* ─── Entry point ───────────────────────────────────────────────────── */

void app_main(void)
{
    init_spi_bus();
    init_nrf24l01();
    nrf24_ce_init();
    nrf24_config_ble_rx();

    xTaskCreatePinnedToCore(ble_sniffer_task, "ble_sniffer", 4096, NULL, 5, NULL, 1);
}
