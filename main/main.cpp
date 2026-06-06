/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_sleep.h"
#include "driver/gpio.h"
#include "nrf24_espidf/hal_espidf.h"
#include <nrf24l01plus/driver.h>
#include <nrf24l01plus/diag_boot.h>

#include "ble_sniffer.h"

/* --- Pin definitions ------------------------------------------------ */
static constexpr gpio_num_t PIN_MISO = GPIO_NUM_19;
static constexpr gpio_num_t PIN_MOSI = GPIO_NUM_23;
static constexpr gpio_num_t PIN_SCLK = GPIO_NUM_18;
static constexpr gpio_num_t PIN_CSN  = GPIO_NUM_17;
static constexpr gpio_num_t PIN_CE   = GPIO_NUM_4;

static nrf24::EspIdfHal hal;
static nrf24::Driver radio(hal);

/* --- BLE sniffer configuration -------------------------------------- */

static const nrf24::ble::RxConfig rx_config = {
    .initial_channel_idx  = 0,
    .payload_width        = nrf24::MAX_PAYLOAD,
    .scan_duration_ms     = 50,
    .extra_channels       = nullptr,
};

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
     * If diagnostics fail after max retries, enter deep sleep for 60 seconds
     * and restart. */
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

    /* Launch the BLE sniffer task on core 1.
     * SnifferArgs carries pointers to the radio driver and RX config,
     * both of which are static and outlive the task. */
    static SnifferArgs sniffer_args = { &radio, &rx_config, {} };
    xTaskCreatePinnedToCore(ble_sniffer_task, "ble_sniffer", 4096,
                            &sniffer_args, 5, NULL, 1);
}
