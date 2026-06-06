/**
 * @file ble_sniffer.h
 * @brief BLE advertising channel sniffer task for the nRF24L01+.
 *
 * This module implements a FreeRTOS task that continuously scans the
 * three BLE advertising channels (37, 38, 39) using an nRF24L01+ radio
 * configured in BLE passive RX mode, and prints decoded PDU information
 * to the serial console.
 *
 * ## BLE advertising sniffing with the nRF24L01+
 *
 * Bluetooth Low Energy devices broadcast **advertising PDUs** on three
 * fixed physical channels (37/2402 MHz, 38/2426 MHz, 39/2480 MHz) to
 * announce their presence.  The nRF24L01+ can receive these PDUs because
 * they use the same 1 Mbps GFSK modulation as Enhanced ShockBurst,
 * provided the radio is configured with the BLE advertising access
 * address (0x8E89BED6), CRC disabled (BLE carries its own CRC), and
 * auto-acknowledgement off.
 *
 * The sniffer configures the nRF24L01+ in PRX mode via
 * nrf24::ble::configure_rx() once at startup, then enters a scanning
 * loop that rotates through the advertising channels.
 *
 * ## Scanning loop architecture
 *
 * The task runs an infinite loop with the following structure:
 *
 * 1. **Channel selection** — pick the next BLE channel index from the
 *    RxConfig scan sequence (default: 37 → 38 → 39 → repeat).
 * 2. **Heartbeat** — every HB_LOOP_INTERVAL iterations, print a summary
 *    line with STATUS register contents and total packet count.
 * 3. **RPD monitoring** (optional) — read the Received Power Detector
 *    register before and after channel switching to measure RF energy.
 * 4. **Channel switch** — CE LOW → write RF_CH → clear IRQ → flush RX
 *    FIFO → CE HIGH → 200 µs settling delay.
 * 5. **Dwell** — wait for scan_duration_ms, checking RX FIFO for data.
 * 6. **Payload read** — read up to MAX_PAYLOAD bytes from the FIFO.
 * 7. **Dewhitening** — bit-swap + Galois LFSR de-whitening using the
 *    current BLE channel index as the LFSR seed.
 * 8. **PDU decode** — extract PDU type, length, advertiser address,
 *    and (for ADV_EXT_IND) the extended header flags.
 *
 * ## What the sniffer can capture
 *
 * - **ADV_IND** — connectable undirected advertising (most common)
 * - **ADV_DIRECT_IND** — connectable directed advertising
 * - **ADV_NONCONN_IND** — non-connectable undirected advertising
 * - **ADV_SCAN_IND** — scannable undirected advertising
 * - **SCAN_RSP** — scan response data
 * - **ADV_EXT_IND** — BLE 5.0+ extended advertising (partial header)
 *
 * ## What the sniffer cannot capture
 *
 * - **Data channel PDUs** — these use a different access address per
 *   connection and hop across 37 data channels; the nRF24L01+ cannot
 *   follow a connection.
 * - **CRC verification** — the nRF24L01+ is configured with CRC off
 *   (BLE CRC is 24-bit; nRF24 supports only 8/16-bit).  Corrupted
 *   packets may appear valid.
 * - **Full ADV_EXT_IND payloads** — BLE 5.0 extended advertising can
 *   span multiple on-air packets ( AuxPtr chaining). The nRF24L01+
 *   captures only the first 32 bytes.
 * - **Directed advertising targets** — SCAN_REQ and CONNECT_IND are
 *   captured, but the advertiser address is at a variable offset.
 *
 * ## Configuration
 *
 * The sniffer is parameterised through two structures:
 *
 * - **nrf24::ble::RxConfig** — controls channel sequence, dwell time,
 *   and payload width.  See `nrf24l01plus/ble_config.h`.
 * - **SnifferDiagFlags** — toggles runtime diagnostic prints (RPD
 *   monitoring, FIFO cross-check, raw byte dump).
 *
 * @code
 *   // In main.cpp — create and launch the sniffer task:
 *   static nrf24::ble::RxConfig rx_config = { .initial_channel_idx = 0 };
 *   static SnifferArgs sniffer_args = { &radio, &rx_config, {} };
 *   xTaskCreatePinnedToCore(ble_sniffer_task, "ble_sniffer",
 *                           4096, &sniffer_args, 5, nullptr, 1);
 * @endcode
 */

#pragma once

#include <cstdint>
#include <nrf24l01plus/driver.h>
#include <nrf24l01plus/ble_config.h>

/**
 * @brief Runtime diagnostic flags for the sniffer loop.
 *
 * Each flag controls an independent diagnostic feature that can be
 * toggled at runtime.  Default values enable RPD monitoring and FIFO
 * cross-checking while leaving raw-byte dumping off (it generates a
 * lot of output).
 */
struct SnifferDiagFlags {
    bool rpd_monitor     = true;   ///< Print RPD value each heartbeat and before channel switch
    bool fifo_crosscheck = true;   ///< Print FIFO_STATUS before payload read to verify data is real
    bool raw_dump        = false;  ///< Print raw whitened bytes before dewhitening for pattern analysis
};

/**
 * @brief Parameter block passed to the sniffer task via FreeRTOS xTaskCreate.
 *
 * The sniffer task receives a pointer to this struct through the
 * pvParameters callback argument.  The caller (typically app_main)
 * must ensure that the Driver, RxConfig, and SnifferDiagFlags objects
 * outlive the task — they are typically static or global.
 *
 * @code
 *   static nrf24::Driver radio(hal);
 *   static nrf24::ble::RxConfig rx_config;
 *   static SnifferDiagFlags diag_flags;
 *   SnifferArgs args = { &radio, &rx_config, diag_flags };
 *   xTaskCreatePinnedToCore(ble_sniffer_task, "ble_sniffer",
 *                           4096, &args, 5, nullptr, 1);
 * @endcode
 */
struct SnifferArgs {
    nrf24::Driver *radio;                ///< Pointer to the nRF24L01+ driver instance
    const nrf24::ble::RxConfig *config;  ///< Pointer to the BLE RX configuration
    SnifferDiagFlags diag_flags;         ///< Runtime diagnostic toggle flags
};

/**
 * @brief FreeRTOS task entry point for the BLE advertising sniffer.
 *
 * Runs an infinite scanning loop on the BLE advertising channels,
 * printing decoded PDU information to the serial console.
 *
 * The task reads the SnifferArgs from pvParameters, then enters the
 * channel-rotation loop: for each BLE channel in the RxConfig scan
 * sequence, dwell for scan_duration_ms while polling the RX FIFO,
 * decode and print any received PDUs, then advance to the next channel.
 *
 * @param arg  Pointer to a SnifferArgs struct (must remain valid for
 *             the lifetime of the task).
 */
void ble_sniffer_task(void *arg);