/**
 * @file ble_sniffer.cpp
 * @brief Implementation of the BLE advertising channel sniffer task.
 *
 * See ble_sniffer.h for the architectural overview.
 */

#include "ble_sniffer.h"

#include <cstdio>
#include <cstring>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <nrf24l01plus/ble.h>
#include <nrf24l01plus/registers.h>

/**
 * @brief Heartbeat print interval in loop iterations.
 *
 * Print a summary line every 500 iterations to reduce serial output
 * overhead while still providing periodic confirmation that the
 * sniffer loop is alive.
 */
static constexpr uint32_t HB_LOOP_INTERVAL = 500;

/*
 * RxConfig and SnifferDiagFlags instances are owned by the caller
 * (main.cpp) and accessed through the SnifferArgs pointer.  No
 * module-level globals needed here — all state comes from the task
 * parameter.
 */

void ble_sniffer_task(void *arg)
{
    auto *sa = static_cast<SnifferArgs *>(arg);
    nrf24::Driver &radio = *sa->radio;
    const nrf24::ble::RxConfig &cfg = *sa->config;
    const SnifferDiagFlags &diag = sa->diag_flags;

    uint8_t  seq_i       = 0;
    uint32_t loop_count  = 0;
    uint32_t pkt_count   = 0;

    printf("BLE sniffer started -- scanning %u channels, %u ms/ch\n",
           cfg.total_channels(), cfg.scan_duration_ms);

    while (1)
    {
        /* Select the next BLE channel in the scan rotation.
         * channel_at() returns BLE channel indices (37, 38, 39 for
         * the default advertising-only configuration). */
        uint8_t ble_ch = cfg.channel_at(seq_i);

        /* Periodic heartbeat: every HB_LOOP_INTERVAL iterations, print
         * a summary with the STATUS register and total packet count.
         * This provides a "still alive" indicator without flooding the
         * serial console. */
        if ((loop_count % HB_LOOP_INTERVAL) == 0)
        {
            auto st = radio.read_reg(nrf24::Status{});
            char st_buf[96];
            st.format(st_buf, sizeof(st_buf));
            printf("[hb] loops=%" PRIu32 "  pkts=%" PRIu32
                   "  STATUS={%s}  ble_ch=%u\n",
                   loop_count, pkt_count, st_buf, ble_ch);

            /* RPD heartbeat monitoring: print the Received Power
             * Detector value as part of the heartbeat.  The RPD latch
             * indicates whether the received signal strength exceeded
             * -64 dBm at any point since the last channel switch.
             * This is useful for confirming that RF energy is present
             * on the current channel, even if no valid BLE packet is
             * received. */
            if (diag.rpd_monitor) {
                auto rpd = radio.read_reg(nrf24::Rpd{});
                printf("[diag] RPD: ch%u signal=%u (%s)\n",
                       ble_ch,
                       static_cast<unsigned>(rpd.received_power),
                       rpd.received_power ? "> -64 dBm" : "< -64 dBm");
            }
        }
        loop_count++;

        /* Pre-switch RPD monitoring: read the RPD latch before
         * switch_channel() resets it.  The nRF24L01+ clears the RPD
         * latch when CE transitions LOW→HIGH (entering RX mode), so
         * this is the last chance to see whether RF energy above -64 dBm
         * was present during the just-completed dwell period. */
        if (diag.rpd_monitor) {
            auto rpd = radio.read_reg(nrf24::Rpd{});
            printf("[diag] RPD before switch: ch%u signal=%u (%s)\n",
                   ble_ch,
                   static_cast<unsigned>(rpd.received_power),
                   rpd.received_power ? "> -64 dBm" : "< -64 dBm");
        }

        /* Switch radio to the next BLE channel.
         * switch_channel() performs: CE LOW → write RF_CH → clear IRQ
         * flags → flush RX FIFO → CE HIGH → 200 µs settling delay.
         * The 200 µs covers both Tstby2a (130 µs max) and the RPD
         * validity threshold (170 µs). */
        nrf24::ble::switch_channel(radio, ble_ch);

        /* Dwell on this channel for scan_duration_ms, polling the RX
         * FIFO for received packets.  vTaskDelay(1) yields to the
         * scheduler between polls (≈1 tick per iteration). */
        TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(cfg.scan_duration_ms);
        while (xTaskGetTickCount() < deadline)
        {
            if (nrf24::ble::rx_fifo_not_empty(radio))
            {
                /* FIFO cross-check: verify FIFO_STATUS before reading
                 * the payload.  Confirms that RX_EMPTY=0 (real data)
                 * and prints STATUS.rx_p_no to identify the pipe.
                 * Reading from an apparently-empty FIFO returns 0xFE on
                 * the nRF24L01+, so this diagnostic helps distinguish
                 * genuine packets from FIFO glitches. */
                if (diag.fifo_crosscheck) {
                    auto fifo = radio.read_reg(nrf24::FifoStatus{});
                    auto st = radio.read_reg(nrf24::Status{});
                    printf("[diag] FIFO_STATUS: rx_empty=%u rx_full=%u  STATUS: rx_dr=%u rx_p_no=%u\n",
                           static_cast<unsigned>(fifo.rx_empty),
                           static_cast<unsigned>(fifo.rx_full),
                           static_cast<unsigned>(st.rx_dr),
                           static_cast<unsigned>(st.rx_p_no));
                }

                /* Read the received payload from the FIFO and clear
                 * the RX_DR interrupt flag so the nRF24L01+ can latch
                 * the next packet.  Flush the RX FIFO in case of
                 * overflow (a second packet arriving before the first
                 * is read causes FIFO_FULL). */
                pkt_count++;
                uint8_t buf[nrf24::MAX_PAYLOAD];
                radio.read_payload(buf, nrf24::MAX_PAYLOAD);
                nrf24::ble::clear_irq_flags(radio);
                radio.flush_rx();

                /* Raw dump: print whitened bytes before dewhitening for
                 * pattern analysis and debugging.  Shows the data
                 * exactly as received from the nRF24L01+ RX FIFO, in
                 * MSbit-first byte order with BLE whitening applied. */
                if (diag.raw_dump) {
                    printf("[diag] raw:");
                    for (uint8_t i = 0; i < nrf24::MAX_PAYLOAD; i++) {
                        printf(" %02X", buf[i]);
                    }
                    printf("\n");
                }

                /* Dewhitening pipeline: bit-swap each byte (nRF24 is
                 * MSbit-first, BLE is LSbit-first) then apply Galois
                 * LFSR de-whitening with the current channel index as
                 * the seed.  After this, buf[] holds the original BLE
                 * PDU bytes in LSbit-first byte order. */
                nrf24::ble::dewhiten(buf, nrf24::MAX_PAYLOAD, ble_ch);

                /* PDU type extraction: the PDU header is two bytes.
                 * Byte 0 bits [3:0] contain the PDU type code.
                 * Byte 1 bits [5:0] contain the PDU body length. */
                auto pdu_type = static_cast<nrf24::ble::BleAdvPduType>(
                    buf[0] & nrf24::ble::PDU_TYPE_MASK);
                uint8_t pdu_len = buf[1] & nrf24::ble::PDU_LENGTH_MASK;
                const char *type_name = nrf24::ble::pdu_type_name(pdu_type);

                /* Advertiser address extraction: for most PDU types the
                 * 6-byte AdvA occupies buf[2]–buf[7].  Types without a
                 * fixed-offset AdvA (SCAN_REQ, CONNECT_IND, ADV_EXT_IND)
                 * are handled by returning false from adv_address(). */
                uint8_t adv_addr[6];
                if (nrf24::ble::adv_address(buf, adv_addr)) {
                    printf("[ch%u] %-17s  %s  len=%u\n",
                           ble_ch, type_name,
                           nrf24::ble::format_address(adv_addr),
                           pdu_len);
                } else {
                    printf("[ch%u] %-17s  len=%u\n",
                           ble_ch, type_name, pdu_len);
                }

                /* ADV_EXT_IND extended header parsing (BLE 5.0+).
                 * Per Bluetooth Core Spec Vol 6 Part B §2.3.3:
                 *   PDU body starts at buf[2]; byte 0 is Extended Header
                 *   Length, byte 1 is Extended Header flags.
                 * The nRF24L01+'s 32-byte payload may capture only the
                 * start of a long extended advertising PDU, so we print
                 * whatever header bytes are present. */
                if (pdu_type == nrf24::ble::BleAdvPduType::AdvExtInd)
                {
                    if (pdu_len >= 2 && pdu_len + 2 <= nrf24::MAX_PAYLOAD)
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
                        printf("  ext_hdr: [truncated -- pdu_len=%u, capture=%u]\n",
                               pdu_len, nrf24::MAX_PAYLOAD);
                    }
                }

                /* Reserved PDU types (codes 8–15) should not appear on
                 * the advertising physical channel per the Bluetooth
                 * Core Spec.  Print the numeric code for identification. */
                if (static_cast<uint8_t>(pdu_type) > 7)
                {
                    printf("  note: reserved PDU type code %u, len=%u\n",
                           static_cast<unsigned>(pdu_type),
                           static_cast<unsigned>(pdu_len));
                }

                /* Print the full PDU hex dump, clamped to the declared
                 * PDU length (+2 for the header bytes) or the captured
                 * payload size, whichever is smaller. */
                uint8_t print_len = (pdu_len + 2 <= nrf24::MAX_PAYLOAD)
                                    ? static_cast<uint8_t>(pdu_len + 2)
                                    : nrf24::MAX_PAYLOAD;
                printf("  pdu:");
                for (uint8_t i = 0; i < print_len; i++)
                {
                    printf(" %02X", buf[i]);
                }
                printf("\n");
            }

            vTaskDelay(1);
        }

        /* Advance to the next channel in the scan sequence, wrapping
         * around after a full cycle. */
        seq_i = (seq_i + 1) % cfg.total_channels();
    }
}