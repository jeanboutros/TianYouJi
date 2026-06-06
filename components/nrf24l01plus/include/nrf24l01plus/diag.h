#pragma once

#include <cstdint>
#include "nrf24l01plus/driver.h"
#include "nrf24l01plus/ble_config.h"
#include "nrf24l01plus/diag_boot.h"

namespace nrf24 {
namespace diag {

/**
 * @brief Run a register-level diagnostic after BLE RX configuration.
 *
 * Reads back all registers programmed by nrf24::ble::configure_rx() and
 * compares them against expected values derived from the given RxConfig.
 * Results are printed to stdout.
 *
 * @deprecated Use full_boot_diagnostic() or verify_ble_config() instead.
 *   The structured diagnostics module (diag_boot.h) provides typed results,
 *   per-phase status, and automatic retry handling.
 *
 * @code
 *   nrf24::Driver radio(hal);
 *   nrf24::ble::RxConfig cfg;
 *   nrf24::ble::configure_rx(radio, cfg);
 *   bool ok = nrf24::diag::verify_ble_rx(radio, cfg);
 * @endcode
 *
 * @param radio   Driver instance (must already be configured).
 * @param config  The RxConfig that was passed to configure_rx().
 * @return        true if all register checks pass, false otherwise.
 */
bool verify_ble_rx(Driver &radio, const ble::RxConfig &config = {});

/**
 * @brief Test raw SPI communication before any register configuration.
 *
 * Performs three stages of SPI validation:
 *
 * 1. **Power-on reset (POR) test** — reads registers in their default
 *    state and compares against datasheet POR values.  If these don't
 *    match, the SPI bus itself is not working (wiring, speed, or mode
 *    issue, or the module is dead).
 *
 * 2. **Write-verify test** — writes known patterns (0x00, 0x3F, 0x55,
 *    0xAA) to EN_AA, reads each back, and checks for a match.  This
 *    confirms that the nRF24 is accepting SPI writes.
 *
 * 3. **Clone chip detection** — tests the EN_AA/EN_CRC override
 *    behaviour documented in the nRF24L01+ datasheet §CONFIG.  Genuine
 *    chips allow EN_CRC=0 when EN_AA=0x00; Si24R1 clones may not.
 *
 * Must be called BEFORE nrf24::ble::configure_rx() so that POR values
 * are still intact.  After this function returns, EN_AA is left at 0x00
 * (as a side effect of the clone test).
 *
 * @deprecated Use full_boot_diagnostic() or verify_spi_comm() instead.
 *   The structured diagnostics module (diag_boot.h) provides typed results,
 *   per-phase status, and automatic retry handling.
 *
 * @code
 *   nrf24::EspIdfHal hal;
 *   hal.init(pins);
 *   nrf24::Driver radio(hal);
 *   bool spi_ok = nrf24::diag::spi_comm_test(radio);
 *   if (!spi_ok) {
 *       printf("SPI communication failed — check wiring and power\n");
 *       return;
 *   }
 *   nrf24::ble::configure_rx(radio);
 * @endcode
 *
 * @param radio  Driver instance (must NOT have been configured yet).
 * @return       true if SPI communication is confirmed working.
 */
bool spi_comm_test(Driver &radio);

} // namespace diag
} // namespace nrf24
