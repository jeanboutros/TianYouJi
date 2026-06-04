/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_system.h"

void core2_task(void *arg)
{
    while (1) {
        printf("Hello from core 2!\n");
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        fflush(stdout);
    }
}

void app_main(void)
{
    printf("Hello world!\n");
    xTaskCreatePinnedToCore(core2_task, "core2_task", 2048, NULL, 5, NULL, 1);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    fflush(stdout);
}
