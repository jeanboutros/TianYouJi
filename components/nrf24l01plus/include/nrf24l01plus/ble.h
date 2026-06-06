#pragma once

#include <cstdint>

namespace nrf24 {
namespace ble {

/**
 * @brief Reverse the bit order within a single byte.
 *
 * The nRF24L01+ SPI interface delivers each byte MSbit-first, while
 * BLE transmits each byte LSbit-first (Bluetooth Core Spec Vol 6
 * Part B §1.3.1).  Every byte read from the nRF24 RX FIFO must be
 * bit-swapped before any BLE protocol processing (dewhitening, CRC,
 * PDU decoding) and every byte written for TX must be bit-swapped
 * after BLE processing.
 *
 * Uses the if-else cascade form (Dmitry Grinberg's original nRF24 BLE
 * reference) because it is the most portable across embedded
 * toolchains — no table lookups, no architecture-specific intrinsics.
 *
 * @code
 *   nrf24::ble::swapbits(0x00)  // → 0x00  (identity: all zeros)
 *   nrf24::ble::swapbits(0xFF)  // → 0xFF  (identity: all ones)
 *   nrf24::ble::swapbits(0x42)  // → 0x42  (0b01000010 is palindromic)
 *   nrf24::ble::swapbits(0x01)  // → 0x80  (bit 0 moves to bit 7)
 * @endcode
 *
 * @param v  Byte whose bits are to be reversed.
 * @return   Byte with bits in reverse order.
 */
inline uint8_t swapbits(uint8_t v)
{
    uint8_t r = 0;
    if (v & 0x80) r |= 0x01;
    if (v & 0x40) r |= 0x02;
    if (v & 0x20) r |= 0x04;
    if (v & 0x10) r |= 0x08;
    if (v & 0x08) r |= 0x10;
    if (v & 0x04) r |= 0x20;
    if (v & 0x02) r |= 0x40;
    if (v & 0x01) r |= 0x80;
    return r;
}

/**
 * @brief Apply BLE data de-whitening in-place on nRF24L01+ RX data.
 *
 * Performs two steps in one call:
 * 1. **Bit-swap** each byte (nRF24L01+ delivers MSbit-first per byte;
 *    BLE uses LSbit-first per byte — Bluetooth Core Spec Vol 6 Part B
 *    §1.3.1).  The swap is performed internally so callers never need
 *    to remember it.
 * 2. **Galois LFSR de-whitening** with polynomial x^7 + x^4 + 1
 *    (Bluetooth Core Spec Vol 6 Part B §3.2).
 *
 * LFSR seed: `swapbits(channel_idx) | 2`.  The `| 2` sets bit 1 in the
 * left-shifted 7-bit LFSR, equivalent to position 1 in the Galois-form
 * register (Dmitry Grinberg, "Bit-banging Bluetooth Low Energy").
 *
 * The Galois LFSR left-shifts; when bit 7 is set it XORs the
 * polynomial taps (`0x11` = bits 4 and 0 of the 7-bit state) before
 * the next shift.  XOR whitening is symmetric, so the same algorithm
 * whitens and de-whitens.
 *
 * @code
 *   // Full RX pipeline for a packet on BLE advertising channel 37:
 *   uint8_t buf[32];
 *   radio.read_payload(buf, 32);
 *   // dewhiten internally: bit-swap each byte, then Galois LFSR XOR
 *   nrf24::ble::dewhiten(buf, 32, 37);
 *   // buf[] now holds the original BLE PDU bytes (LSbit-first format)
 * @endcode
 *
 * @param data         Pointer to the data buffer (modified in-place).
 * @param len          Number of bytes to process.
 * @param channel_idx  BLE channel index (0–39).  Advertising: 37, 38, 39.
 */
void dewhiten(uint8_t *data, uint8_t len, uint8_t channel_idx);

} // namespace ble
} // namespace nrf24
