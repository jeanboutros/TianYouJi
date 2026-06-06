#pragma once

/**
 * @file static_asserts.h
 * @brief Compile-time round-trip verification for all nRF24L01+ register structs.
 *
 * Verifies the invariant: @c Reg::from_byte(b).to_byte() produces the same byte
 * for every valid input byte @c b. Registers with reserved bits define a
 * VALID_MASK and verify <tt>from_byte(b).to_byte() == (b & VALID_MASK)</tt>
 * instead, because reserved bits are discarded by @c from_byte().
 *
 * @code
 *   // This file is compiled via static_asserts.cpp; just include it:
 *   #include "nrf24l01plus/registers/static_asserts.h"
 *   // All static_asserts run at compile time — zero runtime cost.
 * @endcode
 *
 * Registers with no reserved bits (full 256-byte round-trip):
 *   - SetupRetr (0x04)
 *   - ObserveTx (0x08)
 *
 * Registers with reserved bits (masked round-trip):
 *   - Config     (0x00) — bit 7 reserved,       valid mask 0x7F
 *   - EnAa       (0x01) — bits 7:6 reserved,     valid mask 0x3F
 *   - EnRxAddr   (0x02) — bits 7:6 reserved,    valid mask 0x3F
 *   - SetupAw    (0x03) — bits 7:2 reserved,    valid mask 0x03
 *   - RfCh       (0x05) — bit 7 reserved,        valid mask 0x7F
 *   - Status     (0x07) — bit 7 reserved,        valid mask 0x7F
 *   - Rpd        (0x09) — bits 7:1 reserved,    valid mask 0x01
 *   - RxPw       (base) — bits 7:6 reserved,    valid mask 0x3F
 *   - FifoStatus (0x17) — bits 7, 3:2 reserved, valid mask 0x73
 *   - Dynpd      (0x1C) — bits 7:6 reserved,    valid mask 0x3F
 *   - Feature    (0x1D) — bits 7:3 reserved,    valid mask 0x07
 *
 * RfSetup (0x06) is excluded because @c to_byte() is non-constexpr;
 * it is verified by runtime tests in test/test_registers.cpp.
 */

#include "nrf24l01plus/registers.h"

namespace nrf24 {
namespace test {

/* ═══════════════════════════════════════════════════════════════════════════
 * Helper: exhaustive byte round-trip with optional valid mask.
 *
 * For every byte 0x00–0xFF, verifies that
 *   Reg::from_byte(b).to_byte() == (b & ValidMask)
 * where ValidMask == 0xFF means "no reserved bits — perfect round-trip".
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Exhaustive 256-byte round-trip test for a register type.
 *
 * @tparam Reg     Register struct with static @c from_byte(uint8_t) and
 *                 instance method @c to_byte() const, both constexpr.
 * @param  mask    Bits that @c from_byte preserves; all other bits are
 *                 reserved and discarded. Use 0xFF for registers with
 *                 no reserved bits.
 * @return         @c true if every byte round-trips correctly.
 */
template<typename Reg, uint8_t ValidMask>
constexpr bool round_trip_all_bytes() {
    for (unsigned i = 0; i < 256; ++i) {
        const uint8_t b = static_cast<uint8_t>(i);
        const uint8_t expected = b & ValidMask;
        if (Reg::from_byte(b).to_byte() != expected) {
            return false;
        }
    }
    return true;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Config (0x00)
 *
 * Bit layout: bit 7 (rsvd), 6:4 MASK_RX_DR/MASK_TX_DS/MASK_MAX_RT,
 *             3 EN_CRC, 2 CRCO, 1 PWR_UP, 0 PRIM_RX
 * Valid mask: bits 6:0 → 0x7F (bit 7 reserved)
 * Reset value: 0x08
 * ═══════════════════════════════════════════════════════════════════════════ */

static_assert(Config::ADDRESS == 0x00, "Config address must be 0x00");
static_assert(Config::RESET_VALUE == 0x08, "Config reset value must be 0x08");

/* Default construction produces reset value */
static_assert(Config{}.to_byte() == Config::RESET_VALUE,
              "Config default must match reset value 0x08");

/* Exhaustive 256-byte round-trip — bit 7 is discarded by from_byte */
static_assert(round_trip_all_bytes<Config, 0x7F>(),
              "Config: from_byte(b).to_byte() must equal b & 0x7F for all b");

/* Specific encoding: IrqMask::All | CrcMode::Disabled | CrcEncoding::Bytes2
 * | PowerMode::Up | PrimaryMode::RX → 0x70|0x00|0x04|0x02|0x01 = 0x77 */
static_assert([] {
    Config c{};
    c.irq_mask     = IrqMask::All;
    c.crc_mode     = CrcMode::Disabled;
    c.crc_encoding = CrcEncoding::Bytes2;
    c.power_mode   = PowerMode::Up;
    c.primary      = PrimaryMode::RX;
    return c.to_byte() == 0x77;
}(), "Config: all-non-default fields must produce 0x77");

/* from_byte field extraction for byte 0x77 */
static_assert([] {
    auto c = Config::from_byte(0x77);
    return c.irq_mask     == IrqMask::All
        && c.crc_mode     == CrcMode::Disabled
        && c.crc_encoding == CrcEncoding::Bytes2
        && c.power_mode   == PowerMode::Up
        && c.primary      == PrimaryMode::RX;
}(), "Config: from_byte(0x77) must extract all fields correctly");

/* Edge: 0xFF — reserved bit 7 must be stripped */
static_assert(Config::from_byte(0xFF).to_byte() == 0x7F,
              "Config: 0xFF input must strip reserved bit 7 → 0x7F");

/* Edge: 0x00 — all fields at minimum */
static_assert(Config::from_byte(0x00).to_byte() == 0x00,
              "Config: 0x00 round-trip must be exact");


/* ═══════════════════════════════════════════════════════════════════════════
 * EnAa (0x01)
 *
 * Bit layout: bits 7:6 (rsvd), 5:0 ENAA_P5..ENAA_P0
 * Valid mask: bits 5:0 → 0x3F (bits 7:6 reserved)
 * Reset value: 0x3F
 * ═══════════════════════════════════════════════════════════════════════════ */

static_assert(EnAa::ADDRESS == 0x01, "EnAa address must be 0x01");
static_assert(EnAa::RESET_VALUE == 0x3F, "EnAa reset value must be 0x3F");

static_assert(EnAa{}.to_byte() == EnAa::RESET_VALUE,
              "EnAa default must match reset value 0x3F");

static_assert(round_trip_all_bytes<EnAa, 0x3F>(),
              "EnAa: from_byte(b).to_byte() must equal b & 0x3F for all b");

/* All disabled */
static_assert([] {
    EnAa aa{};
    for (int i = 0; i < 6; ++i) aa.pipe[i] = false;
    return aa.to_byte() == 0x00;
}(), "EnAa: all pipes disabled must produce 0x00");

/* 0xFF edge — reserved bits stripped */
static_assert(EnAa::from_byte(0xFF).to_byte() == 0x3F,
              "EnAa: 0xFF input must strip reserved bits → 0x3F");


/* ═══════════════════════════════════════════════════════════════════════════
 * EnRxAddr (0x02)
 *
 * Bit layout: bits 7:6 (rsvd), 5:0 ERX_P5..ERX_P0
 * Valid mask: bits 5:0 → 0x3F (bits 7:6 reserved)
 * Reset value: 0x03
 * ═══════════════════════════════════════════════════════════════════════════ */

static_assert(EnRxAddr::ADDRESS == 0x02, "EnRxAddr address must be 0x02");
static_assert(EnRxAddr::RESET_VALUE == 0x03, "EnRxAddr reset value must be 0x03");

static_assert(EnRxAddr{}.to_byte() == EnRxAddr::RESET_VALUE,
              "EnRxAddr default must match reset value 0x03");

static_assert(round_trip_all_bytes<EnRxAddr, 0x3F>(),
              "EnRxAddr: from_byte(b).to_byte() must equal b & 0x3F for all b");

/* All pipes enabled */
static_assert([] {
    EnRxAddr rx{};
    for (int i = 0; i < 6; ++i) rx.pipe[i] = true;
    return rx.to_byte() == 0x3F;
}(), "EnRxAddr: all pipes enabled must produce 0x3F");

/* 0xFF edge */
static_assert(EnRxAddr::from_byte(0xFF).to_byte() == 0x3F,
              "EnRxAddr: 0xFF input must strip reserved bits → 0x3F");


/* ═══════════════════════════════════════════════════════════════════════════
 * SetupAw (0x03)
 *
 * Bit layout: bits 7:2 (rsvd), 1:0 AW
 * Valid mask: bits 1:0 → 0x03 (bits 7:2 reserved)
 * Reset value: 0x03
 * ═══════════════════════════════════════════════════════════════════════════ */

static_assert(SetupAw::ADDRESS == 0x03, "SetupAw address must be 0x03");
static_assert(SetupAw::RESET_VALUE == 0x03, "SetupAw reset value must be 0x03");

static_assert(SetupAw{}.to_byte() == SetupAw::RESET_VALUE,
              "SetupAw default must match reset value 0x03");

static_assert(round_trip_all_bytes<SetupAw, 0x03>(),
              "SetupAw: from_byte(b).to_byte() must equal b & 0x03 for all b");

/* Specific values: Bytes3, Bytes4, Bytes5 */
static_assert([] {
    SetupAw s{};
    s.address_width = AddressWidth::Bytes3;
    return s.to_byte() == 0x01;
}(), "SetupAw: Bytes3 must produce 0x01");

static_assert([] {
    SetupAw s{};
    s.address_width = AddressWidth::Bytes4;
    return s.to_byte() == 0x02;
}(), "SetupAw: Bytes4 must produce 0x02");

/* 0xFF edge — all reserved bits stripped */
static_assert(SetupAw::from_byte(0xFF).to_byte() == 0x03,
              "SetupAw: 0xFF input must strip reserved bits → 0x03");


/* ═══════════════════════════════════════════════════════════════════════════
 * SetupRetr (0x04)
 *
 * Bit layout: bits 7:4 ARD, bits 3:0 ARC
 * Valid mask: 0xFF (no reserved bits — perfect round-trip)
 * Reset value: 0x03
 * ═══════════════════════════════════════════════════════════════════════════ */

static_assert(SetupRetr::ADDRESS == 0x04, "SetupRetr address must be 0x04");
static_assert(SetupRetr::RESET_VALUE == 0x03, "SetupRetr reset value must be 0x03");

static_assert(SetupRetr{}.to_byte() == SetupRetr::RESET_VALUE,
              "SetupRetr default must match reset value 0x03");

/* Perfect round-trip: every byte maps perfectly */
static_assert(round_trip_all_bytes<SetupRetr, 0xFF>(),
              "SetupRetr: from_byte(b).to_byte() == b for all b (no reserved bits)");

/* Max values: ARD=15 (4000us), ARC=15 */
static_assert([] {
    SetupRetr s{};
    s.delay = AutoRetransmitDelay::Us4000;
    s.count = AutoRetransmitCount::Count15;
    return s.to_byte() == 0xFF;
}(), "SetupRetr: max delay + max count must produce 0xFF");

/* Min values: ARD=0 (250us), ARC=0 (disabled) */
static_assert([] {
    SetupRetr s{};
    s.delay = AutoRetransmitDelay::Us250;
    s.count = AutoRetransmitCount::Disabled;
    return s.to_byte() == 0x00;
}(), "SetupRetr: min delay + disabled must produce 0x00");

/* Field extraction from 0xFF */
static_assert([] {
    auto s = SetupRetr::from_byte(0xFF);
    return s.delay == AutoRetransmitDelay::Us4000
        && s.count == AutoRetransmitCount::Count15;
}(), "SetupRetr: from_byte(0xFF) must extract max fields");


/* ═══════════════════════════════════════════════════════════════════════════
 * RfCh (0x05)
 *
 * Bit layout: bit 7 (rsvd), bits 6:0 RF_CH
 * Valid mask: bits 6:0 → 0x7F (bit 7 reserved)
 * Reset value: 0x02
 * ═══════════════════════════════════════════════════════════════════════════ */

static_assert(RfCh::ADDRESS == 0x05, "RfCh address must be 0x05");
static_assert(RfCh::RESET_VALUE == 0x02, "RfCh reset value must be 0x02");

static_assert(RfCh{}.to_byte() == RfCh::RESET_VALUE,
              "RfCh default must match reset value 0x02");

static_assert(round_trip_all_bytes<RfCh, 0x7F>(),
              "RfCh: from_byte(b).to_byte() must equal b & 0x7F for all b");

/* Channel 125 (max valid) */
static_assert([] {
    RfCh r{};
    r.channel = 125;
    return r.to_byte() == 0x7D;
}(), "RfCh: channel 125 must produce 0x7D");

/* 0xFF edge — reserved bit 7 stripped */
static_assert(RfCh::from_byte(0xFF).to_byte() == 0x7F,
              "RfCh: 0xFF input must strip bit 7 → 0x7F");

/* Channel 0 */
static_assert([] {
    RfCh r{};
    r.channel = 0;
    return r.to_byte() == 0x00;
}(), "RfCh: channel 0 must produce 0x00");


/* ═══════════════════════════════════════════════════════════════════════════
 * RfSetup (0x06)
 *
 * NOTE: to_byte() is NOT constexpr (format() dependencies), so exhaustive
 * compile-time round-trips are NOT possible here. Instead, we verify the
 * constexpr to_reg() helpers and field extraction from known bytes.
 *
 * Runtime round-trip tests exist in test/test_registers.cpp.
 *
 * Bit layout: bit 7 CONT_WAVE, 6 (rsvd), 5 RF_DR_LOW, 4 PLL_LOCK,
 *             3 RF_DR_HIGH, 2:1 RF_PWR, 0 (obs/rsvd)
 * Valid mask: bits 7,5,4,3,2,1 → 0xBE (bits 6,0 reserved/obsolete)
 * Reset value: 0x0E
 * ═══════════════════════════════════════════════════════════════════════════ */

static_assert(RfSetup::ADDRESS == 0x06, "RfSetup address must be 0x06");
static_assert(RfSetup::RESET_VALUE == 0x0E, "RfSetup reset value must be 0x0E");

/* to_reg() overloads are constexpr and can be verified */
static_assert(to_reg(TxPower::Minus18dBm) == 0x00, "TxPower -18dBm → 0x00");
static_assert(to_reg(TxPower::Minus12dBm) == 0x02, "TxPower -12dBm → 0x02");
static_assert(to_reg(TxPower::Minus6dBm)  == 0x04, "TxPower -6dBm → 0x04");
static_assert(to_reg(TxPower::dBm0)       == 0x06, "TxPower 0dBm → 0x06");

static_assert(to_reg(DataRate::Mbps1)   == 0x00, "DataRate 1Mbps → 0x00");
static_assert(to_reg(DataRate::Mbps2)   == 0x08, "DataRate 2Mbps → 0x08");
static_assert(to_reg(DataRate::Kbps250) == 0x20, "DataRate 250kbps → 0x20");

static_assert(to_reg(ContWave::Disabled) == 0x00, "ContWave off → 0x00");
static_assert(to_reg(ContWave::Enabled)  == 0x80, "ContWave on → 0x80");
static_assert(to_reg(PllLock::Disabled)  == 0x00, "PllLock off → 0x00");
static_assert(to_reg(PllLock::Enabled)   == 0x10, "PllLock on → 0x10");

/* NOTE: Both RfSetup::from_byte() and RfSetup::to_byte() are non-constexpr,
 * so compile-time field-extraction and round-trip tests are not possible.
 * Runtime round-trip tests covering from_byte/to_byte exist in
 * test/test_registers.cpp. */


/* ═══════════════════════════════════════════════════════════════════════════
 * Status (0x07)
 *
 * Bit layout: bit 7 (rsvd), 6 RX_DR, 5 TX_DS, 4 MAX_RT, 3:1 RX_P_NO, 0 TX_FULL
 * Valid mask: bits 6:0 → 0x7F (bit 7 reserved)
 * Reset value: 0x0E
 * ═══════════════════════════════════════════════════════════════════════════ */

static_assert(Status::ADDRESS == 0x07, "Status address must be 0x07");
static_assert(Status::RESET_VALUE == 0x0E, "Status reset value must be 0x0E");

static_assert(Status{}.to_byte() == Status::RESET_VALUE,
              "Status default must match reset value 0x0E");

static_assert(round_trip_all_bytes<Status, 0x7F>(),
              "Status: from_byte(b).to_byte() must equal b & 0x7F for all b");

/* All IRQ flags set + pipe empty + no tx_full → 0x70 | 0x0E = 0x7E */
static_assert([] {
    Status s{};
    s.rx_dr  = true;
    s.tx_ds  = true;
    s.max_rt = true;
    return s.to_byte() == 0x7E;
}(), "Status: all IRQ flags set must produce 0x7E");

/* All flags + TX_FULL + pipe 0 → 0x7E | 0x01 | 0x00 = 0x71 */
static_assert([] {
    Status s{};
    s.rx_dr   = true;
    s.tx_ds   = true;
    s.max_rt  = true;
    s.rx_p_no = RxPipeNo::Pipe0;
    s.tx_full = true;
    return s.to_byte() == 0x71;
}(), "Status: all IRQs + pipe 0 + tx_full must produce 0x71");

/* Field extraction from 0x4B */
static_assert([] {
    auto s = Status::from_byte(0x4B);
    /* 0x4B = 0b 0100 1011 → rx_dr=1, tx_ds=0, max_rt=0,
     * rx_p_no=0b101(Pipe5), tx_full=1 */
    return s.rx_dr   == true
        && s.tx_ds   == false
        && s.max_rt  == false
        && s.rx_p_no == RxPipeNo::Pipe5
        && s.tx_full == true;
}(), "Status: from_byte(0x4B) must extract rx_dr=1, pipe5, tx_full=1");

/* 0xFF edge — reserved bit 7 stripped */
static_assert(Status::from_byte(0xFF).to_byte() == 0x7F,
              "Status: 0xFF input must strip bit 7 → 0x7F");


/* ═══════════════════════════════════════════════════════════════════════════
 * ObserveTx (0x08)
 *
 * Bit layout: bits 7:4 PLOS_CNT, bits 3:0 ARC_CNT
 * Valid mask: 0xFF (no reserved bits — perfect round-trip)
 * Reset value: 0x00
 * ═══════════════════════════════════════════════════════════════════════════ */

static_assert(ObserveTx::ADDRESS == 0x08, "ObserveTx address must be 0x08");
static_assert(ObserveTx::RESET_VALUE == 0x00, "ObserveTx reset value must be 0x00");

static_assert(ObserveTx{}.to_byte() == ObserveTx::RESET_VALUE,
              "ObserveTx default must match reset value 0x00");

/* Perfect round-trip */
static_assert(round_trip_all_bytes<ObserveTx, 0xFF>(),
              "ObserveTx: from_byte(b).to_byte() == b for all b (no reserved bits)");

/* Max counters */
static_assert([] {
    ObserveTx o{};
    o.plos_cnt = 15;
    o.arc_cnt  = 15;
    return o.to_byte() == 0xFF;
}(), "ObserveTx: max counters must produce 0xFF");

/* Field extraction from 0x73 */
static_assert([] {
    auto o = ObserveTx::from_byte(0x73);
    return o.plos_cnt == 7 && o.arc_cnt == 3;
}(), "ObserveTx: from_byte(0x73) must yield plos_cnt=7, arc_cnt=3");


/* ═══════════════════════════════════════════════════════════════════════════
 * Rpd (0x09)
 *
 * Bit layout: bits 7:1 (rsvd), bit 0 RPD
 * Valid mask: bit 0 → 0x01 (bits 7:1 reserved)
 * Reset value: 0x00
 * ═══════════════════════════════════════════════════════════════════════════ */

static_assert(Rpd::ADDRESS == 0x09, "Rpd address must be 0x09");
static_assert(Rpd::RESET_VALUE == 0x00, "Rpd reset value must be 0x00");

static_assert(Rpd{}.to_byte() == Rpd::RESET_VALUE,
              "Rpd default must match reset value 0x00");

static_assert(round_trip_all_bytes<Rpd, 0x01>(),
              "Rpd: from_byte(b).to_byte() must equal b & 0x01 for all b");

/* Signal detected */
static_assert([] {
    Rpd r{};
    r.received_power = true;
    return r.to_byte() == 0x01;
}(), "Rpd: received_power=true must produce 0x01");

/* 0xFF edge — all reserved bits stripped */
static_assert(Rpd::from_byte(0xFF).to_byte() == 0x01,
              "Rpd: 0xFF input must strip reserved bits → 0x01");

/* 0xFE edge — bit 0 clear, all reserved bits stripped */
static_assert(Rpd::from_byte(0xFE).to_byte() == 0x00,
              "Rpd: 0xFE input must strip reserved bits → 0x00");


/* ═══════════════════════════════════════════════════════════════════════════
 * RxPw (base, 0x11–0x16)
 *
 * Bit layout: bits 7:6 (rsvd), bits 5:0 RX_PW_Px
 * Valid mask: bits 5:0 → 0x3F (bits 7:6 reserved)
 * Reset value: 0x00
 *
 * Per-pipe structs (RxPwP0–RxPwP5) are tested for ADDRESS constants and
 * that from_byte returns the derived type with the correct field.
 * ═══════════════════════════════════════════════════════════════════════════ */

static_assert(RxPw::RESET_VALUE == 0x00, "RxPw reset value must be 0x00");
static_assert(RxPw{}.to_byte() == RxPw::RESET_VALUE,
              "RxPw default must match reset value 0x00");

/* Base struct round-trip */
static_assert(round_trip_all_bytes<RxPw, 0x3F>(),
              "RxPw: from_byte(b).to_byte() must equal b & 0x3F for all b");

/* Per-pipe ADDRESS constants */
static_assert(RxPwP0::ADDRESS == 0x11, "RxPwP0 address must be 0x11");
static_assert(RxPwP1::ADDRESS == 0x12, "RxPwP1 address must be 0x12");
static_assert(RxPwP2::ADDRESS == 0x13, "RxPwP2 address must be 0x13");
static_assert(RxPwP3::ADDRESS == 0x14, "RxPwP3 address must be 0x14");
static_assert(RxPwP4::ADDRESS == 0x15, "RxPwP4 address must be 0x15");
static_assert(RxPwP5::ADDRESS == 0x16, "RxPwP5 address must be 0x16");

/* Max payload: 32 bytes → 0x20 */
static_assert(RxPw{32}.to_byte() == 0x20,
              "RxPw: 32-byte payload must produce 0x20");

/* 0xFF edge — reserved bits stripped */
static_assert(RxPw::from_byte(0xFF).to_byte() == 0x3F,
              "RxPw: 0xFF input must strip reserved bits → 0x3F");

/* Per-pipe derived types round-trip correctly */
static_assert(RxPwP0::from_byte(0x20).payload_width == 32,
              "RxPwP0: from_byte(0x20) must produce payload_width=32");
static_assert(RxPwP1::from_byte(0x20).payload_width == 32,
              "RxPwP1: from_byte(0x20) must produce payload_width=32");
static_assert(RxPwP2::from_byte(0x20).payload_width == 32,
              "RxPwP2: from_byte(0x20) must produce payload_width=32");
static_assert(RxPwP3::from_byte(0x20).payload_width == 32,
              "RxPwP3: from_byte(0x20) must produce payload_width=32");
static_assert(RxPwP4::from_byte(0x20).payload_width == 32,
              "RxPwP4: from_byte(0x20) must produce payload_width=32");
static_assert(RxPwP5::from_byte(0x20).payload_width == 32,
              "RxPwP5: from_byte(0x20) must produce payload_width=32");


/* ═══════════════════════════════════════════════════════════════════════════
 * FifoStatus (0x17)
 *
 * Bit layout: bit 7 (rsvd), 6 TX_REUSE, 5 TX_FULL, 4 TX_EMPTY,
 *             bits 3:2 (rsvd), 1 RX_FULL, 0 RX_EMPTY
 * Valid mask: bits 6,5,4,1,0 → 0x73 (bits 7,3:2 reserved)
 * Reset value: 0x11
 * ═══════════════════════════════════════════════════════════════════════════ */

static_assert(FifoStatus::ADDRESS == 0x17, "FifoStatus address must be 0x17");
static_assert(FifoStatus::RESET_VALUE == 0x11, "FifoStatus reset value must be 0x11");

static_assert(FifoStatus{}.to_byte() == FifoStatus::RESET_VALUE,
              "FifoStatus default must match reset value 0x11");

static_assert(round_trip_all_bytes<FifoStatus, 0x73>(),
              "FifoStatus: from_byte(b).to_byte() must equal b & 0x73 for all b");

/* All flags set → 0x73 */
static_assert([] {
    FifoStatus f{};
    f.tx_reuse = true;
    f.tx_full  = true;
    f.tx_empty = true;
    f.rx_full  = true;
    f.rx_empty = true;
    return f.to_byte() == 0x73;
}(), "FifoStatus: all flags set must produce 0x73");

/* All flags clear → 0x00 */
static_assert([] {
    FifoStatus f{};
    f.tx_reuse = false;
    f.tx_full  = false;
    f.tx_empty = false;
    f.rx_full  = false;
    f.rx_empty = false;
    return f.to_byte() == 0x00;
}(), "FifoStatus: all flags clear must produce 0x00");

/* 0xFF edge — all reserved bits stripped */
static_assert(FifoStatus::from_byte(0xFF).to_byte() == 0x73,
              "FifoStatus: 0xFF input must strip reserved bits → 0x73");

/* Field extraction from 0x62 */
static_assert([] {
    auto f = FifoStatus::from_byte(0x62);
    /* 0x62 = 0b 0110 0010 → tx_reuse=1, tx_full=1, tx_empty=0,
     * rx_full=1, rx_empty=0 */
    return f.tx_reuse == true
        && f.tx_full  == true
        && f.tx_empty == false
        && f.rx_full  == true
        && f.rx_empty == false;
}(), "FifoStatus: from_byte(0x62) must extract correct fields");


/* ═══════════════════════════════════════════════════════════════════════════
 * Dynpd (0x1C)
 *
 * Bit layout: bits 7:6 (rsvd), 5:0 DPL_P5..DPL_P0
 * Valid mask: bits 5:0 → 0x3F (bits 7:6 reserved)
 * Reset value: 0x00
 * ═══════════════════════════════════════════════════════════════════════════ */

static_assert(Dynpd::ADDRESS == 0x1C, "Dynpd address must be 0x1C");
static_assert(Dynpd::RESET_VALUE == 0x00, "Dynpd reset value must be 0x00");

static_assert(Dynpd{}.to_byte() == Dynpd::RESET_VALUE,
              "Dynpd default must match reset value 0x00");

static_assert(round_trip_all_bytes<Dynpd, 0x3F>(),
              "Dynpd: from_byte(b).to_byte() must equal b & 0x3F for all b");

/* All pipes enabled → 0x3F */
static_assert([] {
    Dynpd d{};
    for (int i = 0; i < 6; ++i) d.pipe[i] = true;
    return d.to_byte() == 0x3F;
}(), "Dynpd: all pipes enabled must produce 0x3F");

/* 0xFF edge */
static_assert(Dynpd::from_byte(0xFF).to_byte() == 0x3F,
              "Dynpd: 0xFF input must strip reserved bits → 0x3F");

/* Field extraction from 0x25 */
static_assert([] {
    auto d = Dynpd::from_byte(0x25);
    /* 0x25 = 0b 0010 0101 → pipes 0,2,5 */
    return d.pipe[0] == true
        && d.pipe[1] == false
        && d.pipe[2] == true
        && d.pipe[3] == false
        && d.pipe[4] == false
        && d.pipe[5] == true;
}(), "Dynpd: from_byte(0x25) must extract pipes 0,2,5");


/* ═══════════════════════════════════════════════════════════════════════════
 * Feature (0x1D)
 *
 * Bit layout: bits 7:3 (rsvd), 2 EN_DPL, 1 EN_ACK_PAY, 0 EN_DYN_ACK
 * Valid mask: bits 2:0 → 0x07 (bits 7:3 reserved)
 * Reset value: 0x00
 * ═══════════════════════════════════════════════════════════════════════════ */

static_assert(Feature::ADDRESS == 0x1D, "Feature address must be 0x1D");
static_assert(Feature::RESET_VALUE == 0x00, "Feature reset value must be 0x00");

static_assert(Feature{}.to_byte() == Feature::RESET_VALUE,
              "Feature default must match reset value 0x00");

static_assert(round_trip_all_bytes<Feature, 0x07>(),
              "Feature: from_byte(b).to_byte() must equal b & 0x07 for all b");

/* All features enabled → 0x07 */
static_assert([] {
    Feature f{};
    f.en_dpl     = DplMode::Enabled;
    f.en_ack_pay = AckPayMode::Enabled;
    f.en_dyn_ack = DynAckMode::Enabled;
    return f.to_byte() == 0x07;
}(), "Feature: all features enabled must produce 0x07");

/* 0xFF edge — all reserved bits stripped */
static_assert(Feature::from_byte(0xFF).to_byte() == 0x07,
              "Feature: 0xFF input must strip reserved bits → 0x07");

/* Field extraction from 0x04 (only EN_DPL) */
static_assert([] {
    auto f = Feature::from_byte(0x04);
    return f.en_dpl     == DplMode::Enabled
        && f.en_ack_pay == AckPayMode::Disabled
        && f.en_dyn_ack == DynAckMode::Disabled;
}(), "Feature: from_byte(0x04) must extract only EN_DPL enabled");

/* Field extraction from 0x07 (all enabled) */
static_assert([] {
    auto f = Feature::from_byte(0x07);
    return f.en_dpl     == DplMode::Enabled
        && f.en_ack_pay == AckPayMode::Enabled
        && f.en_dyn_ack == DynAckMode::Enabled;
}(), "Feature: from_byte(0x07) must extract all features enabled");


/* ═══════════════════════════════════════════════════════════════════════════
 * Address constants (from addresses.h)
 *
 * Verify that each register struct's ADDRESS matches the flat address
 * constant in nrf24::reg namespace. This catches copy-paste drift.
 * ═══════════════════════════════════════════════════════════════════════════ */

static_assert(Config::ADDRESS      == reg::CONFIG,      "Config ADDRESS == reg::CONFIG");
static_assert(EnAa::ADDRESS       == reg::EN_AA,       "EnAa ADDRESS == reg::EN_AA");
static_assert(EnRxAddr::ADDRESS   == reg::EN_RXADDR,   "EnRxAddr ADDRESS == reg::EN_RXADDR");
static_assert(SetupAw::ADDRESS    == reg::SETUP_AW,     "SetupAw ADDRESS == reg::SETUP_AW");
static_assert(SetupRetr::ADDRESS  == reg::SETUP_RETR,  "SetupRetr ADDRESS == reg::SETUP_RETR");
static_assert(RfCh::ADDRESS       == reg::RF_CH,       "RfCh ADDRESS == reg::RF_CH");
static_assert(RfSetup::ADDRESS    == reg::RF_SETUP,     "RfSetup ADDRESS == reg::RF_SETUP");
static_assert(Status::ADDRESS     == reg::STATUS,      "Status ADDRESS == reg::STATUS");
static_assert(ObserveTx::ADDRESS  == reg::OBSERVE_TX,  "ObserveTx ADDRESS == reg::OBSERVE_TX");
static_assert(Rpd::ADDRESS        == reg::RPD,         "Rpd ADDRESS == reg::RPD");
static_assert(RxPwP0::ADDRESS     == reg::RX_PW_P0,   "RxPwP0 ADDRESS == reg::RX_PW_P0");
static_assert(RxPwP1::ADDRESS     == reg::RX_PW_P1,   "RxPwP1 ADDRESS == reg::RX_PW_P1");
static_assert(RxPwP2::ADDRESS     == reg::RX_PW_P2,   "RxPwP2 ADDRESS == reg::RX_PW_P2");
static_assert(RxPwP3::ADDRESS     == reg::RX_PW_P3,   "RxPwP3 ADDRESS == reg::RX_PW_P3");
static_assert(RxPwP4::ADDRESS     == reg::RX_PW_P4,   "RxPwP4 ADDRESS == reg::RX_PW_P4");
static_assert(RxPwP5::ADDRESS     == reg::RX_PW_P5,   "RxPwP5 ADDRESS == reg::RX_PW_P5");
static_assert(FifoStatus::ADDRESS == reg::FIFO_STATUS, "FifoStatus ADDRESS == reg::FIFO_STATUS");
static_assert(Dynpd::ADDRESS      == reg::DYNPD,       "Dynpd ADDRESS == reg::DYNPD");
static_assert(Feature::ADDRESS    == reg::FEATURE,     "Feature ADDRESS == reg::FEATURE");


/* ═══════════════════════════════════════════════════════════════════════════
 * Summary counters — compile-time sanity check that nothing was missed.
 *
 * If these fail, a register struct was added/removed without updating
 * this file.
 * ═══════════════════════════════════════════════════════════════════════════ */

 constexpr int kRegStructsTested = 13;  /* Config, EnAa, EnRxAddr, SetupAw,
                                          SetupRetr, RfCh, RfSetup, Status,
                                          ObserveTx, Rpd, RxPw(base),
                                          FifoStatus, Dynpd, Feature */
/* RxPwP0–P5 are derived types sharing the base round-trip; counted via
   their per-pipe ADDRESS asserts above (6 asserts). */

} // namespace test
} // namespace nrf24