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
 *   uint8_t buf[nrf24::MAX_PAYLOAD];
 *   radio.read_payload(buf, nrf24::MAX_PAYLOAD);
 *   // dewhiten internally: bit-swap each byte, then Galois LFSR XOR
 *   nrf24::ble::dewhiten(buf, nrf24::MAX_PAYLOAD, 37);
 *   // buf[] now holds the original BLE PDU bytes (LSbit-first format)
 * @endcode
 *
 * @param data         Pointer to the data buffer (modified in-place).
 * @param len          Number of bytes to process.
 * @param channel_idx  BLE channel index (0–39).  Advertising: 37, 38, 39.
 */
void dewhiten(uint8_t *data, uint8_t len, uint8_t channel_idx);

/**
 * @brief BLE advertising physical channel PDU type codes.
 *
 * Per Bluetooth Core Spec Vol 6 Part B §2.3 Table 2.2.
 * Values 0–7 are defined by the spec; values 8–15 are reserved
 * and should not appear on the advertising physical channel.
 * The underlying type is uint8_t so that raw PDU header values
 * (4 bits wide) can be cast without narrowing.
 *
 * @code
 *   uint8_t raw = buf[0] & 0x0F;
 *   auto pdu_type = static_cast<nrf24::ble::BleAdvPduType>(raw);
 *   if (pdu_type == nrf24::ble::BleAdvPduType::AdvExtInd) {
 *       // BLE 5.0+ extended advertising — decode extended header
 *   }
 * @endcode
 */
enum class BleAdvPduType : uint8_t {
    AdvInd        = 0,  ///< Connectable undirected advertising
    AdvDirectInd  = 1,  ///< Connectable directed advertising
    AdvNonconnInd = 2,  ///< Non-connectable undirected advertising
    ScanReq       = 3,  ///< Scan request
    ScanRsp       = 4,  ///< Scan response
    ConnectInd    = 5,  ///< Connection indication
    AdvScanInd    = 6,  ///< Scannable undirected advertising
    AdvExtInd     = 7,  ///< Extended advertising (BLE 5.0+)
    /* Values 8–15 are reserved per Bluetooth Core Spec Vol 6 Part B §2.3 */
};

/**
 * @brief BLE PDU header bit masks (Bluetooth Core Spec Vol 6 Part B §2.1).
 *
 * @code
 *   uint8_t pdu_type = buf[0] & nrf24::ble::PDU_TYPE_MASK;   // bits [3:0]
 *   uint8_t pdu_len  = buf[1] & nrf24::ble::PDU_LENGTH_MASK; // bits [5:0]
 * @endcode
 */
static constexpr uint8_t PDU_TYPE_MASK   = 0x0F; ///< Bits [3:0] of PDU header byte 0
static constexpr uint8_t PDU_LENGTH_MASK = 0x3F; ///< Bits [5:0] of PDU header byte 1

/**
 * @brief Return the spec-defined name for a BLE advertising PDU type.
 *
 * For known types (0–7), returns the spec name (e.g. "ADV_EXT_IND").
 * For reserved types (8–15), returns "UNKNOWN(type=N)" where N is
 * the numeric type code.
 *
 * @code
 *   nrf24::ble::pdu_type_name(nrf24::ble::BleAdvPduType::AdvInd)     // "ADV_IND"
 *   nrf24::ble::pdu_type_name(nrf24::ble::BleAdvPduType::AdvExtInd)  // "ADV_EXT_IND"
 *   nrf24::ble::pdu_type_name(static_cast<nrf24::ble::BleAdvPduType>(9))  // "UNKNOWN(type=9)"
 * @endcode
 *
 * @param t  PDU type code (typically from bits [3:0] of the PDU header).
 * @return   Null-terminated spec name or "UNKNOWN(type=N)" string.
 */
const char *pdu_type_name(BleAdvPduType t);

/**
 * @brief Extract the advertiser address (AdvA) from a dewhitten BLE PDU.
 *
 * For ADV_IND, ADV_DIRECT_IND, ADV_NONCONN_IND, SCAN_RSP, and ADV_SCAN_IND
 * PDU types, the AdvA occupies the first 6 bytes of the PDU body
 * (buf[2] through buf[7]).  BLE transmits AdvA LSByte-first on air
 * (Bluetooth Core Spec Vol 6 Part B §1.2), so buf[2] is the LSByte
 * and buf[7] is the MSByte.
 *
 * This function copies the 6-byte AdvA into standard MAC notation order
 * (MSByte first in out[0]), which is the conventional representation
 * for Bluetooth addresses (e.g. "AA:BB:CC:DD:EE:FF").
 *
 * For ADV_EXT_IND (type 7), the AdvA position varies based on the
 * extended header flags (Bluetooth Core Spec Vol 6 Part B §2.3.3).
 * This function returns false for ADV_EXT_IND since parsing the
 * extended header is required to locate AdvA — that is out of scope.
 *
 * For SCAN_REQ (type 3) and CONNECT_IND (type 5), there is no AdvA
 * field in the PDU body at the fixed offset; the ScanA/InitA field
 * comes first. This function returns false for these types.
 *
 * @code
 *   // After dewhitening a received ADV_IND PDU:
 *   uint8_t adv_addr[6];
 *   if (nrf24::ble::adv_address(buf, adv_addr)) {
 *       printf("AdvA: %s\n", nrf24::ble::format_address(adv_addr));
 *   }
 * @endcode
 *
 * @param buf   Dewhitten PDU buffer (buf[0]=type, buf[1]=length, buf[2+]=body).
 *              Must point to at least 8 bytes for PDU types that have AdvA.
 * @param out   Output array of 6 bytes, MSByte first (standard MAC notation).
 *              out[0] receives the MSByte (buf[7]), out[5] receives the LSByte
 *              (buf[2]).
 * @return      true if PDU type has an AdvA at a fixed offset and it was
 *              extracted successfully; false for types without a fixed-offset
 *              AdvA (SCAN_REQ, CONNECT_IND, ADV_EXT_IND).
 */
bool adv_address(const uint8_t *buf, uint8_t out[6]);

/**
 * @brief Format a 6-byte BLE address as a standard MAC string.
 *
 * Produces a string in standard MAC notation (MSByte first,
 * colon-separated uppercase hex).  The caller must ensure the
 * returned pointer is used before the next call to this function
 * (static buffer, not thread-safe).
 *
 * @code
 *   uint8_t addr[6] = {0xCA, 0xFE, 0xBA, 0xBE, 0x00, 0x01};
 *   nrf24::ble::format_address(addr);  // "CA:FE:BA:BE:00:01"
 *
 *   uint8_t adv_addr[6];
 *   nrf24::ble::adv_address(buf, adv_addr);
 *   printf("AdvA: %s\n", nrf24::ble::format_address(adv_addr));
 * @endcode
 *
 * @param addr  6-byte address in standard notation (addr[0]=MSByte).
 * @return      Static string like "CA:FE:BA:BE:00:01" (uppercase hex,
 *              colon-separated).  Valid until the next call to this function.
 */
const char *format_address(const uint8_t addr[6]);

} // namespace ble
} // namespace nrf24
