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
 * @brief BLE advertising access address (0x8E89BED6), bit-reversed per byte.
 *
 * BLE transmits LSbit-first; nRF24L01+ addresses are MSbit-first.
 * Each byte is bit-mirrored:
 * @code
 *   0xD6 → 0x6B,  0xBE → 0x7D,  0x89 → 0x91,  0x8E → 0x71
 * @endcode
 */
inline constexpr uint8_t ADV_ACCESS_ADDR[4] = {0x6B, 0x7D, 0x91, 0x71};

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
     * Returns 3 (advertising) + extra_channel_count.
     *
     * @return Total channel count for one full scan cycle.
     */
    constexpr uint8_t total_channels() const
    {
        return static_cast<uint8_t>(3 + extra_channel_count);
    }

    /**
     * @brief Get the BLE channel index for a given position in the scan sequence.
     *
     * Positions 0–2 map to the advertising channels (37, 38, 39).
     * Positions 3+ map to entries in extra_channels[].
     *
     * @param seq_idx  Position in the scan sequence (0 to total_channels()-1).
     * @return         BLE channel index (0–39).
     */
    constexpr uint8_t channel_at(uint8_t seq_idx) const
    {
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
 * After calling, assert CE high to begin receiving.
 *
 * @code
 *   nrf24::Driver radio(hal);
 *   nrf24::ble::configure_rx(radio);
 *   radio.ce_high();  // start listening
 * @endcode
 *
 * @param radio   Driver instance (dependency injection).
 * @param config  Optional RX configuration overrides.
 */
void configure_rx(Driver &radio, const RxConfig &config = {});

/**
 * @brief Clear all STATUS interrupt flags (RX_DR, TX_DS, MAX_RT).
 *
 * Writes 1 to bits 6:4 of STATUS to acknowledge and clear them.
 *
 * @param radio  Driver instance.
 */
inline void clear_irq_flags(Driver &radio)
{
    Status st;
    st.rx_dr  = true;
    st.tx_ds  = true;
    st.max_rt = true;
    /* Use the raw uint8_t overload because there is no typed write_reg(Status)
       overload yet — write_reg(reg::STATUS, byte) is the only path available
       for writing STATUS.  The st.to_byte() call still ensures typed
       serialisation with reserved-bit handling. */
    radio.write_reg(reg::STATUS, st.to_byte());
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
    radio.write_reg(reg::RF_CH, rf_ch.to_byte());
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

} // namespace ble
} // namespace nrf24
