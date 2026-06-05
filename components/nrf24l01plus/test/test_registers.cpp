#include "nrf24l01plus/registers.h"
#include <cassert>
#include <cstdio>

// Use static_assert for constexpr tests (compile-time verification)
// Use assert() for runtime tests (non-constexpr functions)

#define TEST_SECTION(name) printf("  [PASS] %s\n", name)

// ═══════════════════════════════════════════════════════════════════════════
// Config (0x00)
// ═══════════════════════════════════════════════════════════════════════════

static_assert(nrf24::Config::ADDRESS == 0x00);
static_assert(nrf24::Config::RESET_VALUE == 0x08);

// Reset value
static_assert(nrf24::Config().to_byte() == 0x08);

// Round-trip
static_assert(nrf24::Config::from_byte(0x08).to_byte() == 0x08);
static_assert(nrf24::Config::from_byte(0x00).to_byte() == 0x00);
static_assert(nrf24::Config::from_byte(0x7F).to_byte() == 0x7F);

// Individual field placement
static_assert(nrf24::to_reg(nrf24::IrqMask::None) == 0x00);
static_assert(nrf24::to_reg(nrf24::IrqMask::All) == 0x70);
static_assert(nrf24::to_reg(nrf24::IrqMask::RxDr) == 0x40);
static_assert(nrf24::to_reg(nrf24::IrqMask::TxDs) == 0x20);
static_assert(nrf24::to_reg(nrf24::IrqMask::MaxRt) == 0x10);
static_assert(nrf24::to_reg(nrf24::CrcMode::Enabled) == 0x08);
static_assert(nrf24::to_reg(nrf24::CrcMode::Disabled) == 0x00);
static_assert(nrf24::to_reg(nrf24::CrcEncoding::Bytes1) == 0x00);
static_assert(nrf24::to_reg(nrf24::CrcEncoding::Bytes2) == 0x04);
static_assert(nrf24::to_reg(nrf24::PowerMode::Down) == 0x00);
static_assert(nrf24::to_reg(nrf24::PowerMode::Up) == 0x02);
static_assert(nrf24::to_reg(nrf24::PrimaryMode::TX) == 0x00);
static_assert(nrf24::to_reg(nrf24::PrimaryMode::RX) == 0x01);

// All fields set to non-default values
static_assert([] {
    nrf24::Config c;
    c.irq_mask = nrf24::IrqMask::All;
    c.crc_mode = nrf24::CrcMode::Disabled;
    c.crc_encoding = nrf24::CrcEncoding::Bytes2;
    c.power_mode = nrf24::PowerMode::Up;
    c.primary = nrf24::PrimaryMode::RX;
    // 0x70 | 0x00 | 0x04 | 0x02 | 0x01 = 0x77
    return c.to_byte() == 0x77;
}());

// from_byte field extraction
static_assert([] {
    auto c = nrf24::Config::from_byte(0x77);
    return c.irq_mask == nrf24::IrqMask::All
        && c.crc_mode == nrf24::CrcMode::Disabled
        && c.crc_encoding == nrf24::CrcEncoding::Bytes2
        && c.power_mode == nrf24::PowerMode::Up
        && c.primary == nrf24::PrimaryMode::RX;
}());

// ═══════════════════════════════════════════════════════════════════════════
// EnAa (0x01)
// ═══════════════════════════════════════════════════════════════════════════

static_assert(nrf24::EnAa::ADDRESS == 0x01);
static_assert(nrf24::EnAa::RESET_VALUE == 0x3F);

// Reset value
static_assert(nrf24::EnAa().to_byte() == 0x3F);

// All disabled
static_assert([] {
    nrf24::EnAa aa{};
    for (int i = 0; i < 6; ++i) aa.pipe[i] = false;
    return aa.to_byte() == 0x00;
}());

// Single pipe enabled
static_assert([] {
    nrf24::EnAa aa{};
    for (int i = 0; i < 6; ++i) aa.pipe[i] = false;
    aa.pipe[3] = true;
    return aa.to_byte() == 0x08;
}());

// Round-trip
static_assert(nrf24::EnAa::from_byte(0x3F).to_byte() == 0x3F);
static_assert(nrf24::EnAa::from_byte(0x00).to_byte() == 0x00);
static_assert(nrf24::EnAa::from_byte(0x15).to_byte() == 0x15);

// from_byte extracts only bits 5:0 (upper bits ignored)
static_assert(nrf24::EnAa::from_byte(0xFF).to_byte() == 0x3F);

// ═══════════════════════════════════════════════════════════════════════════
// EnRxAddr (0x02)
// ═══════════════════════════════════════════════════════════════════════════

static_assert(nrf24::EnRxAddr::ADDRESS == 0x02);
static_assert(nrf24::EnRxAddr::RESET_VALUE == 0x03);

// Reset value
static_assert(nrf24::EnRxAddr().to_byte() == 0x03);

// All enabled
static_assert([] {
    nrf24::EnRxAddr rx{};
    for (int i = 0; i < 6; ++i) rx.pipe[i] = true;
    return rx.to_byte() == 0x3F;
}());

// All disabled
static_assert([] {
    nrf24::EnRxAddr rx{};
    for (int i = 0; i < 6; ++i) rx.pipe[i] = false;
    return rx.to_byte() == 0x00;
}());

// Round-trip
static_assert(nrf24::EnRxAddr::from_byte(0x03).to_byte() == 0x03);
static_assert(nrf24::EnRxAddr::from_byte(0x3F).to_byte() == 0x3F);
static_assert(nrf24::EnRxAddr::from_byte(0x00).to_byte() == 0x00);

// from_byte masks upper bits
static_assert(nrf24::EnRxAddr::from_byte(0xFF).to_byte() == 0x3F);

// ═══════════════════════════════════════════════════════════════════════════
// SetupAw (0x03)
// ═══════════════════════════════════════════════════════════════════════════

static_assert(nrf24::SetupAw::ADDRESS == 0x03);
static_assert(nrf24::SetupAw::RESET_VALUE == 0x03);

// Reset value (5 bytes)
static_assert(nrf24::SetupAw().to_byte() == 0x03);

// 3 bytes
static_assert([] {
    nrf24::SetupAw s;
    s.address_width = nrf24::AddressWidth::Bytes3;
    return s.to_byte() == 0x01;
}());

// 4 bytes
static_assert([] {
    nrf24::SetupAw s;
    s.address_width = nrf24::AddressWidth::Bytes4;
    return s.to_byte() == 0x02;
}());

// to_reg
static_assert(nrf24::to_reg(nrf24::AddressWidth::Bytes3) == 0x01);
static_assert(nrf24::to_reg(nrf24::AddressWidth::Bytes4) == 0x02);
static_assert(nrf24::to_reg(nrf24::AddressWidth::Bytes5) == 0x03);

// Round-trip
static_assert(nrf24::SetupAw::from_byte(0x01).to_byte() == 0x01);
static_assert(nrf24::SetupAw::from_byte(0x02).to_byte() == 0x02);
static_assert(nrf24::SetupAw::from_byte(0x03).to_byte() == 0x03);

// from_byte masks to bits 1:0
static_assert(nrf24::SetupAw::from_byte(0xFF).to_byte() == 0x03);

// ═══════════════════════════════════════════════════════════════════════════
// SetupRetr (0x04)
// ═══════════════════════════════════════════════════════════════════════════

static_assert(nrf24::SetupRetr::ADDRESS == 0x04);
static_assert(nrf24::SetupRetr::RESET_VALUE == 0x03);

// Reset value
static_assert(nrf24::SetupRetr().to_byte() == 0x03);

// Max delay + max count
static_assert([] {
    nrf24::SetupRetr s;
    s.delay = nrf24::AutoRetransmitDelay::Us4000;
    s.count = nrf24::AutoRetransmitCount::Count15;
    return s.to_byte() == 0xFF;
}());

// Min (disabled retransmit, 250us)
static_assert([] {
    nrf24::SetupRetr s;
    s.delay = nrf24::AutoRetransmitDelay::Us250;
    s.count = nrf24::AutoRetransmitCount::Disabled;
    return s.to_byte() == 0x00;
}());

// to_reg
static_assert(nrf24::to_reg(nrf24::AutoRetransmitDelay::Us250) == 0x00);
static_assert(nrf24::to_reg(nrf24::AutoRetransmitDelay::Us4000) == 0xF0);
static_assert(nrf24::to_reg(nrf24::AutoRetransmitCount::Disabled) == 0x00);
static_assert(nrf24::to_reg(nrf24::AutoRetransmitCount::Count15) == 0x0F);

// Round-trip
static_assert(nrf24::SetupRetr::from_byte(0x03).to_byte() == 0x03);
static_assert(nrf24::SetupRetr::from_byte(0xFF).to_byte() == 0xFF);
static_assert(nrf24::SetupRetr::from_byte(0x5A).to_byte() == 0x5A);

// Field extraction
static_assert([] {
    auto s = nrf24::SetupRetr::from_byte(0xFF);
    return s.delay == nrf24::AutoRetransmitDelay::Us4000
        && s.count == nrf24::AutoRetransmitCount::Count15;
}());

// ═══════════════════════════════════════════════════════════════════════════
// RfCh (0x05)
// ═══════════════════════════════════════════════════════════════════════════

static_assert(nrf24::RfCh::ADDRESS == 0x05);
static_assert(nrf24::RfCh::RESET_VALUE == 0x02);

// Reset value
static_assert(nrf24::RfCh().to_byte() == 0x02);

// Channel 125
static_assert([] {
    nrf24::RfCh r;
    r.channel = 125;
    return r.to_byte() == 0x7D;
}());

// Channel 0
static_assert([] {
    nrf24::RfCh r;
    r.channel = 0;
    return r.to_byte() == 0x00;
}());

// Masks to 7 bits (channel > 127 truncated)
static_assert([] {
    nrf24::RfCh r;
    r.channel = 0xFF;
    return r.to_byte() == 0x7F;
}());

// Round-trip
static_assert(nrf24::RfCh::from_byte(0x02).to_byte() == 0x02);
static_assert(nrf24::RfCh::from_byte(0x7D).to_byte() == 0x7D);
static_assert(nrf24::RfCh::from_byte(0x00).to_byte() == 0x00);

// from_byte masks bit 7
static_assert(nrf24::RfCh::from_byte(0xFF).to_byte() == 0x7F);

// ═══════════════════════════════════════════════════════════════════════════
// RfSetup (0x06) — NOT constexpr, uses runtime assert
// ═══════════════════════════════════════════════════════════════════════════

static_assert(nrf24::RfSetup::ADDRESS == 0x06);
static_assert(nrf24::RfSetup::RESET_VALUE == 0x0E);

// to_reg for enums (these ARE constexpr)
static_assert(nrf24::to_reg(nrf24::TxPower::Minus18dBm) == 0x00);
static_assert(nrf24::to_reg(nrf24::TxPower::Minus12dBm) == 0x02);
static_assert(nrf24::to_reg(nrf24::TxPower::Minus6dBm) == 0x04);
static_assert(nrf24::to_reg(nrf24::TxPower::dBm0) == 0x06);
static_assert(nrf24::to_reg(nrf24::DataRate::Mbps1) == 0x00);
static_assert(nrf24::to_reg(nrf24::DataRate::Mbps2) == 0x08);
static_assert(nrf24::to_reg(nrf24::DataRate::Kbps250) == 0x20);
static_assert(nrf24::to_reg(nrf24::ContWave::Disabled) == 0x00);
static_assert(nrf24::to_reg(nrf24::ContWave::Enabled) == 0x80);
static_assert(nrf24::to_reg(nrf24::PllLock::Disabled) == 0x00);
static_assert(nrf24::to_reg(nrf24::PllLock::Enabled) == 0x10);

void test_rf_setup() {
    // Reset value: 2 Mbps + 0 dBm = 0x08 | 0x06 = 0x0E
    assert(nrf24::RfSetup().to_byte() == 0x0E);

    // 250kbps + 0 dBm = 0x20 | 0x06 = 0x26
    {
        nrf24::RfSetup s;
        s.data_rate = nrf24::DataRate::Kbps250;
        s.tx_power = nrf24::TxPower::dBm0;
        assert(s.to_byte() == 0x26);
    }

    // 1Mbps + -18 dBm = 0x00 | 0x00 = 0x00
    {
        nrf24::RfSetup s;
        s.data_rate = nrf24::DataRate::Mbps1;
        s.tx_power = nrf24::TxPower::Minus18dBm;
        assert(s.to_byte() == 0x00);
    }

    // All bits set: cont_wave + pll_lock + 250kbps + 0dBm
    // 0x80 | 0x20 | 0x10 | 0x06 = 0xB6
    {
        nrf24::RfSetup s;
        s.cont_wave = nrf24::ContWave::Enabled;
        s.pll_lock = nrf24::PllLock::Enabled;
        s.data_rate = nrf24::DataRate::Kbps250;
        s.tx_power = nrf24::TxPower::dBm0;
        assert(s.to_byte() == 0xB6);
    }

    // Round-trip
    assert(nrf24::RfSetup::from_byte(0x0E).to_byte() == 0x0E);
    assert(nrf24::RfSetup::from_byte(0x26).to_byte() == 0x26);
    assert(nrf24::RfSetup::from_byte(0x00).to_byte() == 0x00);
    assert(nrf24::RfSetup::from_byte(0xB6).to_byte() == 0xB6);

    // Field extraction from 0x26 (250kbps, 0dBm)
    {
        auto s = nrf24::RfSetup::from_byte(0x26);
        assert(s.data_rate == nrf24::DataRate::Kbps250);
        assert(s.tx_power == nrf24::TxPower::dBm0);
        assert(s.cont_wave == nrf24::ContWave::Disabled);
        assert(s.pll_lock == nrf24::PllLock::Disabled);
    }

    // Field extraction from 0x0E (2Mbps, 0dBm) — reset
    {
        auto s = nrf24::RfSetup::from_byte(0x0E);
        assert(s.data_rate == nrf24::DataRate::Mbps2);
        assert(s.tx_power == nrf24::TxPower::dBm0);
    }

    TEST_SECTION("RfSetup (0x06)");
}

// ═══════════════════════════════════════════════════════════════════════════
// Status (0x07)
// ═══════════════════════════════════════════════════════════════════════════

static_assert(nrf24::Status::ADDRESS == 0x07);
static_assert(nrf24::Status::RESET_VALUE == 0x0E);

// Reset value: RX_P_NO=0b111 → bits 3:1 = 0x0E
static_assert(nrf24::Status().to_byte() == 0x0E);

// All IRQ flags set: 0x70 | 0x0E = 0x7E
static_assert([] {
    nrf24::Status s;
    s.rx_dr = true;
    s.tx_ds = true;
    s.max_rt = true;
    return s.to_byte() == 0x7E;
}());

// All flags + TX_FULL + pipe 0
static_assert([] {
    nrf24::Status s;
    s.rx_dr = true;
    s.tx_ds = true;
    s.max_rt = true;
    s.rx_p_no = nrf24::RxPipeNo::Pipe0;
    s.tx_full = true;
    return s.to_byte() == 0x71;
}());

// Round-trip
static_assert(nrf24::Status::from_byte(0x0E).to_byte() == 0x0E);
static_assert(nrf24::Status::from_byte(0x7E).to_byte() == 0x7E);
static_assert(nrf24::Status::from_byte(0x00).to_byte() == 0x00);
static_assert(nrf24::Status::from_byte(0x7F).to_byte() == 0x7F);

// Field extraction
static_assert([] {
    auto s = nrf24::Status::from_byte(0x7E);
    return s.rx_dr == true
        && s.tx_ds == true
        && s.max_rt == true
        && s.rx_p_no == nrf24::RxPipeNo::Empty
        && s.tx_full == false;
}());

static_assert([] {
    auto s = nrf24::Status::from_byte(0x4B);
    // 0x4B = 0b 0100 1011
    // rx_dr=1, tx_ds=0, max_rt=0, rx_p_no=0b101(Pipe5), tx_full=1
    return s.rx_dr == true
        && s.tx_ds == false
        && s.max_rt == false
        && s.rx_p_no == nrf24::RxPipeNo::Pipe5
        && s.tx_full == true;
}());

// ═══════════════════════════════════════════════════════════════════════════
// ObserveTx (0x08)
// ═══════════════════════════════════════════════════════════════════════════

static_assert(nrf24::ObserveTx::ADDRESS == 0x08);
static_assert(nrf24::ObserveTx::RESET_VALUE == 0x00);

// Reset value
static_assert(nrf24::ObserveTx().to_byte() == 0x00);

// Max counters: PLOS_CNT=15, ARC_CNT=15
static_assert([] {
    nrf24::ObserveTx o;
    o.plos_cnt = 15;
    o.arc_cnt = 15;
    return o.to_byte() == 0xFF;
}());

// Individual fields
static_assert([] {
    nrf24::ObserveTx o;
    o.plos_cnt = 8;
    o.arc_cnt = 3;
    return o.to_byte() == 0x83;
}());

// Round-trip
static_assert(nrf24::ObserveTx::from_byte(0x00).to_byte() == 0x00);
static_assert(nrf24::ObserveTx::from_byte(0xFF).to_byte() == 0xFF);
static_assert(nrf24::ObserveTx::from_byte(0xA5).to_byte() == 0xA5);

// Field extraction
static_assert([] {
    auto o = nrf24::ObserveTx::from_byte(0xFF);
    return o.plos_cnt == 15 && o.arc_cnt == 15;
}());

static_assert([] {
    auto o = nrf24::ObserveTx::from_byte(0x73);
    return o.plos_cnt == 7 && o.arc_cnt == 3;
}());

// ═══════════════════════════════════════════════════════════════════════════
// Rpd (0x09)
// ═══════════════════════════════════════════════════════════════════════════

static_assert(nrf24::Rpd::ADDRESS == 0x09);
static_assert(nrf24::Rpd::RESET_VALUE == 0x00);

// Reset value
static_assert(nrf24::Rpd().to_byte() == 0x00);

// Signal detected
static_assert([] {
    nrf24::Rpd r;
    r.received_power = true;
    return r.to_byte() == 0x01;
}());

// Round-trip
static_assert(nrf24::Rpd::from_byte(0x00).to_byte() == 0x00);
static_assert(nrf24::Rpd::from_byte(0x01).to_byte() == 0x01);

// from_byte masks to bit 0
static_assert(nrf24::Rpd::from_byte(0xFE).to_byte() == 0x00);
static_assert(nrf24::Rpd::from_byte(0xFF).to_byte() == 0x01);

// ═══════════════════════════════════════════════════════════════════════════
// RxPw (0x11–0x16)
// ═══════════════════════════════════════════════════════════════════════════

// Addresses per pipe
static_assert(nrf24::RxPwP0::ADDRESS == 0x11);
static_assert(nrf24::RxPwP1::ADDRESS == 0x12);
static_assert(nrf24::RxPwP2::ADDRESS == 0x13);
static_assert(nrf24::RxPwP3::ADDRESS == 0x14);
static_assert(nrf24::RxPwP4::ADDRESS == 0x15);
static_assert(nrf24::RxPwP5::ADDRESS == 0x16);

// Reset value
static_assert(nrf24::RxPw::RESET_VALUE == 0x00);
static_assert(nrf24::RxPw().to_byte() == 0x00);

// 32 bytes (max payload)
static_assert(nrf24::RxPw{32}.to_byte() == 0x20);

// 1 byte (min non-zero)
static_assert(nrf24::RxPw{1}.to_byte() == 0x01);

// Masks to 6 bits
static_assert(nrf24::RxPw{63}.to_byte() == 0x3F);
static_assert(nrf24::RxPw{0xFF}.to_byte() == 0x3F);

// Round-trip
static_assert(nrf24::RxPw::from_byte(0x00).to_byte() == 0x00);
static_assert(nrf24::RxPw::from_byte(0x20).to_byte() == 0x20);
static_assert(nrf24::RxPw::from_byte(0x3F).to_byte() == 0x3F);

// from_byte masks upper bits
static_assert(nrf24::RxPw::from_byte(0xFF).to_byte() == 0x3F);
static_assert(nrf24::RxPw::from_byte(0xC0).to_byte() == 0x00);

// Field extraction
static_assert(nrf24::RxPw::from_byte(0x20).payload_width == 32);
static_assert(nrf24::RxPw::from_byte(0x10).payload_width == 16);
static_assert(nrf24::RxPw::from_byte(0x00).payload_width == 0);

// ═══════════════════════════════════════════════════════════════════════════
// FifoStatus (0x17)
// ═══════════════════════════════════════════════════════════════════════════

static_assert(nrf24::FifoStatus::ADDRESS == 0x17);
static_assert(nrf24::FifoStatus::RESET_VALUE == 0x11);

// Reset value: TX_EMPTY=1(bit4), RX_EMPTY=1(bit0) → 0x11
static_assert(nrf24::FifoStatus().to_byte() == 0x11);

// All flags set
static_assert([] {
    nrf24::FifoStatus f;
    f.tx_reuse = true;
    f.tx_full = true;
    f.tx_empty = true;
    f.rx_full = true;
    f.rx_empty = true;
    // bit6 + bit5 + bit4 + bit1 + bit0 = 0x40|0x20|0x10|0x02|0x01 = 0x73
    return f.to_byte() == 0x73;
}());

// All flags clear
static_assert([] {
    nrf24::FifoStatus f;
    f.tx_reuse = false;
    f.tx_full = false;
    f.tx_empty = false;
    f.rx_full = false;
    f.rx_empty = false;
    return f.to_byte() == 0x00;
}());

// Round-trip
static_assert(nrf24::FifoStatus::from_byte(0x11).to_byte() == 0x11);
static_assert(nrf24::FifoStatus::from_byte(0x00).to_byte() == 0x00);
static_assert(nrf24::FifoStatus::from_byte(0x73).to_byte() == 0x73);

// from_byte ignores reserved bits (3:2 and 7)
static_assert(nrf24::FifoStatus::from_byte(0xFF).to_byte() == 0x73);

// Field extraction
static_assert([] {
    auto f = nrf24::FifoStatus::from_byte(0x11);
    return f.tx_reuse == false
        && f.tx_full == false
        && f.tx_empty == true
        && f.rx_full == false
        && f.rx_empty == true;
}());

static_assert([] {
    auto f = nrf24::FifoStatus::from_byte(0x62);
    // 0x62 = 0b 0110 0010 → tx_reuse=1, tx_full=1, tx_empty=0, rx_full=1, rx_empty=0
    return f.tx_reuse == true
        && f.tx_full == true
        && f.tx_empty == false
        && f.rx_full == true
        && f.rx_empty == false;
}());

// ═══════════════════════════════════════════════════════════════════════════
// Dynpd (0x1C)
// ═══════════════════════════════════════════════════════════════════════════

static_assert(nrf24::Dynpd::ADDRESS == 0x1C);
static_assert(nrf24::Dynpd::RESET_VALUE == 0x00);

// Reset value
static_assert(nrf24::Dynpd().to_byte() == 0x00);

// All pipes enabled
static_assert([] {
    nrf24::Dynpd d{};
    for (int i = 0; i < 6; ++i) d.pipe[i] = true;
    return d.to_byte() == 0x3F;
}());

// Single pipe
static_assert([] {
    nrf24::Dynpd d{};
    d.pipe[5] = true;
    return d.to_byte() == 0x20;
}());

// Round-trip
static_assert(nrf24::Dynpd::from_byte(0x00).to_byte() == 0x00);
static_assert(nrf24::Dynpd::from_byte(0x3F).to_byte() == 0x3F);
static_assert(nrf24::Dynpd::from_byte(0x15).to_byte() == 0x15);

// from_byte masks upper bits
static_assert(nrf24::Dynpd::from_byte(0xFF).to_byte() == 0x3F);

// Field extraction
static_assert([] {
    auto d = nrf24::Dynpd::from_byte(0x25); // 0b 0010 0101 → pipes 0,2,5
    return d.pipe[0] == true
        && d.pipe[1] == false
        && d.pipe[2] == true
        && d.pipe[3] == false
        && d.pipe[4] == false
        && d.pipe[5] == true;
}());

// ═══════════════════════════════════════════════════════════════════════════
// Feature (0x1D)
// ═══════════════════════════════════════════════════════════════════════════

static_assert(nrf24::Feature::ADDRESS == 0x1D);
static_assert(nrf24::Feature::RESET_VALUE == 0x00);

// Reset value
static_assert(nrf24::Feature().to_byte() == 0x00);

// All enabled
static_assert([] {
    nrf24::Feature f;
    f.en_dpl = nrf24::DplMode::Enabled;
    f.en_ack_pay = nrf24::AckPayMode::Enabled;
    f.en_dyn_ack = nrf24::DynAckMode::Enabled;
    return f.to_byte() == 0x07;
}());

// Individual bits
static_assert(nrf24::to_reg(nrf24::DplMode::Enabled) == 0x04);
static_assert(nrf24::to_reg(nrf24::DplMode::Disabled) == 0x00);
static_assert(nrf24::to_reg(nrf24::AckPayMode::Enabled) == 0x02);
static_assert(nrf24::to_reg(nrf24::AckPayMode::Disabled) == 0x00);
static_assert(nrf24::to_reg(nrf24::DynAckMode::Enabled) == 0x01);
static_assert(nrf24::to_reg(nrf24::DynAckMode::Disabled) == 0x00);

// Round-trip
static_assert(nrf24::Feature::from_byte(0x00).to_byte() == 0x00);
static_assert(nrf24::Feature::from_byte(0x07).to_byte() == 0x07);
static_assert(nrf24::Feature::from_byte(0x05).to_byte() == 0x05);

// from_byte masks upper bits
static_assert(nrf24::Feature::from_byte(0xFF).to_byte() == 0x07);

// Field extraction
static_assert([] {
    auto f = nrf24::Feature::from_byte(0x07);
    return f.en_dpl == nrf24::DplMode::Enabled
        && f.en_ack_pay == nrf24::AckPayMode::Enabled
        && f.en_dyn_ack == nrf24::DynAckMode::Enabled;
}());

static_assert([] {
    auto f = nrf24::Feature::from_byte(0x04);
    return f.en_dpl == nrf24::DplMode::Enabled
        && f.en_ack_pay == nrf24::AckPayMode::Disabled
        && f.en_dyn_ack == nrf24::DynAckMode::Disabled;
}());

// ═══════════════════════════════════════════════════════════════════════════
// Main — runtime tests (for non-constexpr functions)
// ═══════════════════════════════════════════════════════════════════════════

int main() {
    printf("=== nRF24L01+ Register Tests ===\n");

    // All static_asserts pass at compile time; report them as sections
    TEST_SECTION("Config (0x00)");
    TEST_SECTION("EnAa (0x01)");
    TEST_SECTION("EnRxAddr (0x02)");
    TEST_SECTION("SetupAw (0x03)");
    TEST_SECTION("SetupRetr (0x04)");
    TEST_SECTION("RfCh (0x05)");

    // RfSetup needs runtime tests
    test_rf_setup();

    TEST_SECTION("Status (0x07)");
    TEST_SECTION("ObserveTx (0x08)");
    TEST_SECTION("Rpd (0x09)");
    TEST_SECTION("RxPw (0x11-0x16)");
    TEST_SECTION("FifoStatus (0x17)");
    TEST_SECTION("Dynpd (0x1C)");
    TEST_SECTION("Feature (0x1D)");

    printf("\nAll tests passed!\n");
    return 0;
}
