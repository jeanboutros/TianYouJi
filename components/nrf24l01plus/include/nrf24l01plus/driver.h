#pragma once

#include <cstdint>
#include "nrf24l01plus/hal.h"

namespace nrf24 {

/**
 * @brief Platform-independent driver for the nRF24L01+ transceiver.
 *
 * All hardware access is delegated to a Hal implementation supplied at
 * construction.  This class encodes the nRF24L01+ SPI command protocol
 * and provides a convenient register/payload API.
 *
 * @code
 *   EspIdfHal hal;
 *   hal.init(pins);
 *   nrf24::Driver radio(hal);
 *   radio.write_reg(nrf24::reg::CONFIG, nrf24::Config().to_byte());
 *   radio.ce_high();                // Enter RX mode
 * @endcode
 */
class Driver {
public:
    /**
     * @brief Construct the driver with a HAL implementation.
     *
     * @param hal  Reference to a platform-specific Hal.  Must outlive
     *             this Driver instance.
     */
    explicit Driver(Hal &hal) : hal_(hal) {}

    /**
     * @brief Read a single-byte register.
     *
     * Sends the R_REGISTER command (bits [7:5] = 000) with the 5-bit
     * register address and clocks out one byte on MISO.
     *
     * @code
     *   cmd byte: 0b000A_AAAA   (A = reg address)
     *   read_reg(nrf24::reg::STATUS) -> cmd = 0x07, reads STATUS
     * @endcode
     *
     * @param reg  Register address (only bits [4:0] used).  Use constants
     *              from nrf24l01plus/registers/addresses.h (e.g. nrf24::reg::STATUS).
     * @return     The byte value read from the register.
     */
    uint8_t read_reg(uint8_t reg);

    /**
     * @brief Write a single-byte register.
     *
     * Sends the W_REGISTER command (bits [7:5] = 001) with the 5-bit
     * register address followed by one data byte on MOSI.
     *
     * Prefer the typed struct overload (write_reg(cfg)) for single-byte
     * registers — it deduces the address and prevents wrong-register
     * mistakes at compile time.
     *
     * @code
     *   // Preferred: typed struct overload (deduces address)
     *   nrf24::Config cfg;
     *   cfg.power_mode = nrf24::PowerMode::Up;
     *   cfg.primary    = nrf24::PrimaryMode::RX;
     *   radio.write_reg(cfg);
     *
     *   // Low-level: internal use only (does not enforce struct types)
     *   radio.write_reg(nrf24::reg::CONFIG, cfg.to_byte());
     * @endcode
     *
     * @param reg    Register address (only bits [4:0] used).  Use constants
     *               from nrf24l01plus/registers/addresses.h (e.g. nrf24::reg::CONFIG).
     * @param value  Byte to write into the register.
     */
    void write_reg(uint8_t reg, uint8_t value);

    /**
     * @brief Write a multi-byte register (e.g. TX_ADDR, RX_ADDR_Px).
     *
     * Sends W_REGISTER followed by `len` bytes from `data`.
     *
     * @param reg   Register address (only bits [4:0] used).  Use constants
     *              from nrf24l01plus/registers/addresses.h (e.g. nrf24::reg::TX_ADDR).
     * @param data  Pointer to bytes to write (LSByte first per nRF24 convention).
     * @param len   Number of bytes (1-5 for address registers).
     */
    void write_reg_multi(uint8_t reg, const uint8_t *data, uint8_t len);

    /**
     * @brief Read a multi-byte register.
     *
     * Sends R_REGISTER followed by `len` clock bytes, storing MISO data
     * into `buf`.
     *
     * @param reg  Register address (only bits [4:0] used).  Use constants
     *              from nrf24l01plus/registers/addresses.h (e.g. nrf24::reg::RX_ADDR_P0).
     * @param buf  Destination buffer (must be >= len bytes).
     * @param len  Number of bytes to read.
     */
    void read_reg_multi(uint8_t reg, uint8_t *buf, uint8_t len);

    /**
     * @brief Read the RX payload from the FIFO.
     *
     * Sends the R_RX_PAYLOAD command (cmd::R_RX_PAYLOAD) and clocks out
     * `len` bytes.  The payload is removed from the RX FIFO after reading.
     *
     * @param buf  Destination buffer (must be >= len bytes).
     * @param len  Number of payload bytes to read (1-32).
     */
    void read_payload(uint8_t *buf, uint8_t len);

    /**
     * @brief Flush the RX FIFO.
     *
     * Sends the FLUSH_RX command (cmd::FLUSH_RX).  Should be used in RX mode
     * only; behaviour is undefined in TX mode per the datasheet.
     */
    void flush_rx();

    /**
     * @brief Flush the TX FIFO.
     *
     * Sends the FLUSH_TX command (cmd::FLUSH_TX).  Should be used in TX mode
     * only; behaviour is undefined in RX mode per the datasheet.
     */
    void flush_tx();

    /**
     * @brief Drive CE pin high (activate RX or start TX).
     */
    void ce_high();

    /**
     * @brief Drive CE pin low (return to standby-I).
     */
    void ce_low();

    /**
     * @brief Write a register and verify the value was accepted by the device.
     *
     * Writes `value` to the register at `reg`, then immediately reads it back
     * and compares.  Useful for debugging SPI communication issues where a
     * write may silently fail (e.g. MOSI pin misconfigured, wrong register
     * address, or device not responding).
     *
     * This is NOT the default write path — it adds a round-trip read and
     * comparison, so should only be used during debug/validation sequences.
     *
     * @code
     *   nrf24::Config cfg;
     *   cfg.power_mode = nrf24::PowerMode::Up;
     *   cfg.primary    = nrf24::PrimaryMode::RX;
     *   bool ok = radio.write_and_verify(cfg);
     *   // ok == true  → device accepted the write
     *   // ok == false → read-back mismatch (SPI or write issue)
     * @endcode
     *
     * Prefer the typed struct overload (write_and_verify(cfg)) for single-byte
     * registers — it deduces the address and normalises reserved bits.
     *
     * @param reg    Register address (use constants from nrf24::reg::).  // internal use
     * @param value  Byte to write.                                        // internal use
     * @return       true if read-back matches value, false otherwise.
     */
    bool write_and_verify(uint8_t reg, uint8_t value);

    /**
     * @brief Write a multi-byte register and verify all bytes were accepted.
     *
     * Writes `data` to the register at `reg`, then reads back `len` bytes and
     * compares each one.  Useful for validating address registers (e.g. RX_ADDR_P0).
     *
     * @param reg   Register address (use constants from nrf24::reg::).
     * @param data  Pointer to bytes to write.
     * @param len   Number of bytes (1–5 for address registers).
     * @return      true if all read-back bytes match, false otherwise.
     */
    bool write_and_verify_multi(uint8_t reg, const uint8_t *data, uint8_t len);

    // --- Typed register overloads ---

    /**
     * @brief Read a single-byte register and return the typed struct.
     *
     * Deduces the register address from StructType::ADDRESS, reads the byte,
     * and reconstructs the typed struct using StructType::from_byte().
     *
     * @code
     *   auto cfg = radio.read_reg(nrf24::Config{});
     *   auto st  = radio.read_reg(nrf24::Status{});
     * @endcode
     *
     * @tparam StructType  Register struct type (must have ADDRESS and from_byte()).
     * @param tag           Default-constructed struct for type deduction.
     * @return             Populated struct with from_byte() applied to the read value.
     */
    template <typename StructType>
    StructType read_reg(StructType tag = {});

    /**
     * @brief Write a single-byte register using a typed struct.
     *
     * Deduces the register address from StructType::ADDRESS and serialises
     * the struct using to_byte().  Callers never need to remember raw addresses.
     *
     * @code
     *   nrf24::Config cfg;
     *   cfg.power_mode = nrf24::PowerMode::Up;
     *   cfg.primary    = nrf24::PrimaryMode::RX;
     *   radio.write_reg(cfg);
     * @endcode
     *
     * @tparam StructType  Register struct type (must have ADDRESS and to_byte()).
     * @param reg_struct   Struct instance to write.
     */
    template <typename StructType>
    void write_reg(const StructType &reg_struct);

    /**
     * @brief Write a register, read it back, and verify the value matches.
     *
     * Deduces the register address from StructType::ADDRESS. Writes
     * reg_struct.to_byte(), reads back, and compares using StructType::from_byte().
     *
     * Normalises reserved bits: the read-back value is parsed with from_byte()
     * and re-serialised with to_byte() before comparison, so reserved-bit
     * differences do not cause false mismatches.
     *
     * @code
     *   nrf24::Config cfg;
     *   cfg.power_mode = nrf24::PowerMode::Up;
     *   cfg.primary    = nrf24::PrimaryMode::RX;
     *   cfg.crc_mode   = nrf24::CrcMode::Disabled;
     *   bool ok = radio.write_and_verify(cfg);
     *   // ok == true  → device accepted the write
     *   // ok == false → read-back mismatches
     * @endcode
     *
     * @tparam StructType  Register struct type (must have ADDRESS, to_byte(), from_byte()).
     * @param reg_struct   Struct instance to write.
     * @return            true if read-back matches, false otherwise.
     */
    template <typename StructType>
    bool write_and_verify(const StructType &reg_struct);

    /**
     * @brief Access the underlying HAL.
     *
     * @return Reference to the Hal implementation passed at construction.
     */
    Hal &hal() { return hal_; }

private:
    Hal &hal_;
};

// --- Template method definitions (must be in header for implicit instantiation) ---

template <typename StructType>
StructType Driver::read_reg(StructType)
{
    return StructType::from_byte(read_reg(StructType::ADDRESS));
}

template <typename StructType>
void Driver::write_reg(const StructType &reg_struct)
{
    write_reg(StructType::ADDRESS, reg_struct.to_byte());
}

template <typename StructType>
bool Driver::write_and_verify(const StructType &reg_struct)
{
    write_reg(StructType::ADDRESS, reg_struct.to_byte());
    auto readback = StructType::from_byte(read_reg(StructType::ADDRESS));
    return readback.to_byte() == reg_struct.to_byte();
}

} // namespace nrf24
