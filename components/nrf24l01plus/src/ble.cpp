#include "nrf24l01plus/ble.h"

#include <cstdio>

namespace nrf24 {
namespace ble {

/* Galois LFSR seed bit: bit 1 forced high per Bluetooth Core Spec Vol 6 Part B §3.2.
 * Same as the `| 2` in Dmitry Grinberg's btLeWhiten — places channel index
 * at bits [6:0] with bit 1 always set. */
static constexpr uint8_t GALOIS_LFSR_SEED_BIT = 0x02;

/* Galois LFSR polynomial mask for x^7 + x^4 + 1.
 * When bit 7 is set after shift, XOR feedback taps at bits 4 and 0. */
static constexpr uint8_t GALOIS_LFSR_POLY = 0x11;

void dewhiten(uint8_t *data, uint8_t len, uint8_t channel_idx)
{
    /* Step 1: bit-swap each byte (nRF24L01+ MSbit-first → BLE LSbit-first) */
    for (uint8_t i = 0; i < len; i++)
        data[i] = swapbits(data[i]);

    /* Step 2: Galois LFSR dewhitening (Dmitry Grinberg's algorithm).
     * Polynomial x^7+x^4+1 — see GALOIS_LFSR_POLY.
     * Seed: swapbits(channel_idx) | GALOIS_LFSR_SEED_BIT. */
    uint8_t lfsr = swapbits(channel_idx) | GALOIS_LFSR_SEED_BIT;
    for (uint8_t i = 0; i < len; i++) {
        for (uint8_t m = 1; m; m <<= 1) {
            if (lfsr & 0x80) {
                lfsr ^= GALOIS_LFSR_POLY;
                data[i] ^= m;
            }
            lfsr <<= 1;
        }
    }
}

/* Spec-defined PDU type names indexed by BleAdvPduType value.
 * Per Bluetooth Core Spec Vol 6 Part B §2.3 Table 2.2.
 * Only indices 0–7 are valid; callers handle 8–15 as UNKNOWN. */
static const char *PDU_TYPE_NAMES[] = {
    "ADV_IND",        // 0
    "ADV_DIRECT_IND", // 1
    "ADV_NONCONN_IND",// 2
    "SCAN_REQ",       // 3
    "SCAN_RSP",       // 4
    "CONNECT_IND",    // 5
    "ADV_SCAN_IND",   // 6
    "ADV_EXT_IND",    // 7 — BLE 5.0+ extended advertising
};

/* Static buffer for sprintf'd UNKNOWN type names.
 * Worst case: "UNKNOWN(type=15)" = 19 chars + NUL = 20 bytes.
 * Not thread-safe — acceptable for single-threaded sniffer output. */
static char unknown_buf[20];

const char *pdu_type_name(BleAdvPduType t)
{
    auto val = static_cast<uint8_t>(t);
    if (val <= 7) return PDU_TYPE_NAMES[val];
    std::snprintf(unknown_buf, sizeof(unknown_buf),
                  "UNKNOWN(type=%u)", static_cast<unsigned>(val));
    return unknown_buf;
}

/**
 * @brief Check whether a BLE advertising PDU type carries a fixed-offset AdvA.
 *
 * Per Bluetooth Core Spec Vol 6 Part B §2.3:
 * - ADV_IND (0), ADV_DIRECT_IND (1), ADV_NONCONN_IND (2),
 *   SCAN_RSP (4), ADV_SCAN_IND (6) all have AdvA in the first 6 bytes
 *   of the PDU body.
 * - SCAN_REQ (3) and CONNECT_IND (5) have ScanA/InitA first, not AdvA.
 * - ADV_EXT_IND (7) has AdvA at a variable offset in the extended header,
 *   so it cannot be extracted without parsing the extended header flags.
 *
 * @param t  PDU type code.
 * @return   true if the PDU type has AdvA at the fixed buf[2..7] offset.
 */
static bool has_fixed_offset_adva(BleAdvPduType t)
{
    switch (t) {
    case BleAdvPduType::AdvInd:
    case BleAdvPduType::AdvDirectInd:
    case BleAdvPduType::AdvNonconnInd:
    case BleAdvPduType::ScanRsp:
    case BleAdvPduType::AdvScanInd:
        return true;
    case BleAdvPduType::ScanReq:
    case BleAdvPduType::ConnectInd:
    case BleAdvPduType::AdvExtInd:
        return false;
    }
    /* Unreachable for valid enum values, but compilers may warn without this. */
    return false;
}

bool adv_address(const uint8_t *buf, uint8_t out[6])
{
    auto pdu_type = static_cast<BleAdvPduType>(buf[0] & PDU_TYPE_MASK);

    if (!has_fixed_offset_adva(pdu_type)) {
        return false;
    }

    /* BLE transmits AdvA LSByte-first on air (Vol 6 Part B §1.2).
     * After dewhitening, buf[2] = first on-air byte = LSByte,
     * buf[7] = last on-air byte = MSByte.
     * Standard MAC notation writes MSByte first:
     *   out[0] = buf[7] (MSByte), out[5] = buf[2] (LSByte). */
    out[0] = buf[7];
    out[1] = buf[6];
    out[2] = buf[5];
    out[3] = buf[4];
    out[4] = buf[3];
    out[5] = buf[2];

    return true;
}

/* Static buffer for format_address().
 * "XX:XX:XX:XX:XX:XX\0" = 17 chars + NUL = 18 bytes.
 * Not thread-safe — acceptable for single-threaded sniffer output. */
static char addr_buf[18];

const char *format_address(const uint8_t addr[6])
{
    std::snprintf(addr_buf, sizeof(addr_buf),
                  "%02X:%02X:%02X:%02X:%02X:%02X",
                  static_cast<unsigned>(addr[0]),
                  static_cast<unsigned>(addr[1]),
                  static_cast<unsigned>(addr[2]),
                  static_cast<unsigned>(addr[3]),
                  static_cast<unsigned>(addr[4]),
                  static_cast<unsigned>(addr[5]));
    return addr_buf;
}

} // namespace ble
} // namespace nrf24
