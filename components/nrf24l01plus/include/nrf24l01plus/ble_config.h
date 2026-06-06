#pragma once

#include <cstdint>
#include "nrf24l01plus/driver.h"
#include "nrf24l01plus/registers.h"

namespace nrf24 {
namespace ble {

/**
 * @brief BLE advertising channel descriptor.
 *
 * Maps a BLE channel index (37, 38, 39) to the nRF24L01+ RF_CH value.
 * Frequency = 2400 + rf_ch MHz.
 *
 * @code
 *   nrf24::ble::ADV_CHANNELS[0].rf_ch    // 2 → 2402 MHz (ch37)
 *   nrf24::ble::ADV_CHANNELS[1].rf_ch    // 26 → 2426 MHz (ch38)
 *   nrf24::ble::ADV_CHANNELS[2].rf_ch    // 80 → 2480 MHz (ch39)
 * @endcode
 */
struct AdvChannel {
    uint8_t rf_ch;       ///< nRF24L01+ RF_CH register value
    uint8_t ch_idx;      ///< BLE channel index (37, 38, or 39)
    const char *name;    ///< Human-readable label (e.g. "ch37")
};

/**
 * @brief The three BLE advertising channels.
 */
inline constexpr AdvChannel ADV_CHANNELS[3] = {
    { 2, 37, "ch37"},   ///< 2402 MHz
    {26, 38, "ch38"},   ///< 2426 MHz
    {80, 39, "ch39"},   ///< 2480 MHz
};

/**
 * @brief BLE advertising access address (0x8E89BED6), bit-reversed per
 *        byte, in nRF24L01+ SPI LSByte-first write order.
 *
 * BLE transmits LSBit-first per byte; nRF24L01+ addresses are MSBit-first.
 * Each byte is bit-mirrored via swapbits().  The byte order follows the
 * nRF24L01+ SPI convention (datasheet §8.3.1: "LSByte is written first")
 * where the first SPI data byte maps to the register LSByte.
 *
 * Transformation chain:
 * - BLE AA 0x8E89BED6 on-air (LSByte-first): D6 BE 89 8E
 * - Per-byte bit-swap (BLE LSBit → nRF24 MSBit):
 *   swapbits(0xD6)=0x6B  swapbits(0xBE)=0x7D
 *   swapbits(0x89)=0x91  swapbits(0x8E)=0x71
 * - nRF24 on-air byte order (MSByte first): 6B 7D 91 71
 * - nRF24 SPI write order (LSByte first):  71 91 7D 6B
 *
 * @code
 *   // SPI data byte 1 (LSByte) = swapbits(0x8E) = 0x71
 *   // SPI data byte 2           = swapbits(0x89) = 0x91
 *   // SPI data byte 3           = swapbits(0xBE) = 0x7D
 *   // SPI data byte 4 (MSByte) = swapbits(0xD6) = 0x6B
 *   radio.write_reg_multi(reg::RX_ADDR_P0, ADV_ACCESS_ADDR, 4);
 * @endcode
 */
inline constexpr uint8_t ADV_ACCESS_ADDR[4] = {0x71, 0x91, 0x7D, 0x6B};

/**
 * @brief Configuration for BLE passive RX mode.
 *
 * @code
 *   // Scan only advertising channels, 50 ms each:
 *   nrf24::ble::RxConfig cfg;
 *   cfg.scan_duration_ms = 50;
 *
 *   // Scan advertising + some data channels:
 *   static constexpr uint8_t extra[] = {0, 1, 2, 12, 13};
 *   cfg.set_extra_channels(extra);  // count deduced automatically
 * @endcode
 */
struct RxConfig {
    uint8_t initial_channel_idx = 0; ///< Index into ADV_CHANNELS (0–2) for initial tune

    uint8_t payload_width = 32;      ///< Fixed payload width (bytes, 1–32)

    uint16_t scan_duration_ms = 50;  ///< Time to listen on each channel before rotating (ms)

    /**
     * @brief Optional additional BLE channel indices to scan (0–39).
     *
     * When non-null, the scanner cycles through the 3 advertising channels
     * followed by these additional channels, spending scan_duration_ms on
     * each.  If multiple radios are available, they can be assigned
     * non-overlapping slices of this array for parallel reception.
     *
     * Ownership: caller must keep the array alive for the lifetime of
     * the scanning loop.  Set to nullptr (default) to scan only the
     * 3 advertising channels.
     */
    const uint8_t *extra_channels = nullptr;

    uint8_t extra_channel_count = 0; ///< Number of entries in extra_channels (0 = adv only)

    /**
     * @brief Set extra channels from a C array (size deduced automatically).
     *
     * @code
     *   static constexpr uint8_t extra[] = {0, 1, 2, 12, 13};
     *   nrf24::ble::RxConfig cfg;
     *   cfg.set_extra_channels(extra);  // count deduced as 5
     * @endcode
     *
     * @tparam N  Array size (deduced).
     * @param arr  Reference to a uint8_t array of BLE channel indices.
     */
    template <uint8_t N>
    constexpr void set_extra_channels(const uint8_t (&arr)[N])
    {
        extra_channels      = arr;
        extra_channel_count = N;
    }

    /**
     * @brief Total number of channels in the scan sequence.
     *
     * Returns 3 (advertising) + extra_channel_count, clamped to 255
     * to prevent uint8_t overflow if extra_channel_count exceeds 252.
     *
     * @return Total channel count for one full scan cycle (max 255).
     */
    constexpr uint8_t total_channels() const
    {
        uint16_t total = static_cast<uint16_t>(3) + static_cast<uint16_t>(extra_channel_count);
        return static_cast<uint8_t>(total > 255 ? 255 : total);
    }

    /**
     * @brief Get the BLE channel index for a given position in the scan sequence.
     *
     * Positions 0–2 map to the advertising channels (37, 38, 39).
     * Positions 3+ map to entries in extra_channels[].
     * If @p seq_idx is out of range (>= total_channels()), returns the
     * first advertising channel (ch37) as a safe default.
     *
     * @param seq_idx  Position in the scan sequence (0 to total_channels()-1).
     * @return         BLE channel index (0–39), or ADV_CHANNELS[0].ch_idx on out-of-range.
     */
    constexpr uint8_t channel_at(uint8_t seq_idx) const
    {
        if (seq_idx >= total_channels()) {
            return ADV_CHANNELS[0].ch_idx;
        }
        if (seq_idx < 3) return ADV_CHANNELS[seq_idx].ch_idx;
        return extra_channels[seq_idx - 3];
    }
};

/**
 * @brief Configure an nRF24L01+ for BLE passive reception.
 *
 * Programs all registers needed to receive raw BLE advertising frames:
 * - Power up in PRX mode, CRC disabled (BLE has its own CRC)
 * - Auto-ACK off, pipe 0 only, 4-byte address width
 * - No retransmission, 1 Mbps data rate, 0 dBm TX power
 * - Pipe 0 RX address set to BLE advertising access address
 * - Specified RF channel from ADV_CHANNELS
 *
 * After programming registers, this function waits for the PWR_UP
 * oscillator settling delay (2 ms, per datasheet §6.1.2 Table 9)
 * and asserts CE HIGH to transition the device into RX mode.
 * The caller does NOT need to call ce_high() again after this function.
 *
 * @code
 *   nrf24::Driver radio(hal);
 *   nrf24::ble::configure_rx(radio);
 *   // Device is now in RX mode — ready to receive
 * @endcode
 *
 * @param radio   Driver instance (dependency injection).
 * @param config  Optional RX configuration overrides.
 */
void configure_rx(Driver &radio, const RxConfig &config = {});

/**
 * @brief Clear all STATUS interrupt flags (RX_DR, TX_DS, MAX_RT).
 *
 * In the nRF24L01+ STATUS register, interrupt flags are cleared by writing 1
 * to the corresponding bits (write-1-to-clear semantics per datasheet §STATUS).
 * Writing 0 to a flag bit has no effect — only a 1 acknowledges and clears it.
 *
 * @param radio  Driver instance.
 */
inline void clear_irq_flags(Driver &radio)
{
    Status st;
    st.rx_dr  = true;
    st.tx_ds  = true;
    st.max_rt = true;
    radio.write_reg(st);
}

/**
 * @brief Convert a BLE channel index (0–39) to an nRF24L01+ RF_CH value.
 *
 * BLE channel-to-frequency mapping (Bluetooth Core Spec Vol 6 Part B §1.4.1):
 * - Data channels 0–10:  freq = 2404 + 2*k MHz  → RF_CH = 4 + 2*k
 * - Data channels 11–36: freq = 2428 + 2*(k-11) MHz → RF_CH = 28 + 2*(k-11)
 * - Advertising ch 37:   freq = 2402 MHz → RF_CH = 2
 * - Advertising ch 38:   freq = 2426 MHz → RF_CH = 26
 * - Advertising ch 39:   freq = 2480 MHz → RF_CH = 80
 *
 * @code
 *   nrf24::ble::channel_to_rf_ch(37) // returns 2
 *   nrf24::ble::channel_to_rf_ch(0)  // returns 4
 *   nrf24::ble::channel_to_rf_ch(12) // returns 30
 * @endcode
 *
 * @param ble_channel  BLE channel index (0–39).
 * @return             nRF24L01+ RF_CH register value (0–80).
 */
constexpr uint8_t channel_to_rf_ch(uint8_t ble_channel)
{
    if (ble_channel == 37) return 2;
    if (ble_channel == 38) return 26;
    if (ble_channel == 39) return 80;
    if (ble_channel <= 10) return static_cast<uint8_t>(4 + 2 * ble_channel);
    /* channels 11–36 */
    return static_cast<uint8_t>(28 + 2 * (ble_channel - 11));
}

/**
 * @brief Switch to any BLE channel (0–39).
 *
 * Lowers CE, reprograms RF_CH to the frequency corresponding to the
 * given BLE channel index, clears IRQ flags, flushes RX FIFO,
 * then reasserts CE.
 *
 * @code
 *   nrf24::ble::switch_channel(radio, 37);  // advertising ch 37 (2402 MHz)
 *   nrf24::ble::switch_channel(radio, 0);   // data ch 0 (2404 MHz)
 * @endcode
 *
 * @param radio        Driver instance.
 * @param ble_channel  BLE channel index (0–39).
 */
inline void switch_channel(Driver &radio, uint8_t ble_channel)
{
    radio.ce_low();
    RfCh rf_ch;
    rf_ch.channel = channel_to_rf_ch(ble_channel);
    radio.write_reg(rf_ch);
    clear_irq_flags(radio);
    radio.flush_rx();
    radio.ce_high();
}

/**
 * @brief Check if a new RX packet is available in the FIFO.
 *
 * Reads the STATUS register and checks the RX_DR flag.
 *
 * @param radio  Driver instance.
 * @return       true if RX_DR is set (payload available).
 */
inline bool rx_available(Driver &radio)
{
    auto st = radio.read_reg(Status{});
    return st.rx_dr;
}

/**
 * @brief Check if the RX FIFO contains data using FIFO_STATUS register.
 *
 * Reads FIFO_STATUS and returns true if RX_EMPTY is 0 (FIFO not empty).
 * This is more reliable than checking RX_DR alone, because RX_DR can be
 * set spuriously or may not reflect the actual FIFO state after
 * operations like flush_rx().
 *
 * @code
 *   if (nrf24::ble::rx_fifo_not_empty(radio)) {
 *       uint8_t buf[32];
 *       radio.read_payload(buf, 32);
 *   }
 * @endcode
 *
 * @param radio  Driver instance.
 * @return       true if RX FIFO is NOT empty (data available).
 */
inline bool rx_fifo_not_empty(Driver &radio)
{
    auto fs = radio.read_reg(FifoStatus{});
    return !fs.rx_empty;
}

} // namespace ble
} // namespace nrf24
