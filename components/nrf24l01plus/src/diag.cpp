#include "nrf24l01plus/diag.h"

#include <cstdio>
#include <cstring>
#include "nrf24l01plus/registers.h"
#include "nrf24l01plus/ble.h"
#include "nrf24l01plus/ble_config.h"

namespace nrf24 {
namespace diag {

/**
 * @brief Backward-compatible wrapper for the structured verify_spi_comm() diagnostic.
 *
 * Calls verify_spi_comm() with Detailed verbosity and converts the result
 * to a bool.  Returns true if the phase passed or warned (Warn indicates
 * SPI works but a clone chip was detected).
 *
 * @deprecated Use full_boot_diagnostic() or verify_spi_comm() instead.
 *   The structured diagnostics module (diag_boot.h) provides typed results,
 *   per-phase status, and automatic retry handling.
 *
 * @param radio  Driver instance (must NOT have been configured yet).
 * @return       true if SPI communication is confirmed working.
 */
bool spi_comm_test(Driver &radio)
{
    printf("\n=== SPI Communication Test (legacy wrapper) ===\n");
    auto result = verify_spi_comm(radio, DiagVerbosity::Detailed);
    bool ok = result.ok();
    printf("\nSPI comm test: %s%s\n\n",
           ok ? "PASS" : "FAIL",
           (result.status == DiagStatus::Warn) ? " (clone chip detected)" : "");
    return ok;
}

/**
 * @brief Backward-compatible wrapper for the structured verify_ble_config() diagnostic.
 *
 * Calls verify_ble_config() with Detailed verbosity and converts the
 * result to a bool.  This function calls nrf24::ble::configure_rx()
 * internally and verifies all programmed registers.
 *
 * @deprecated Use full_boot_diagnostic() or verify_ble_config() instead.
 *   The structured diagnostics module (diag_boot.h) provides typed results,
 *   per-phase status, and automatic retry handling.
 *
 * @param radio   Driver instance (must already be configured for BLE RX,
 *                or this function will configure it first).
 * @param config  The RxConfig that was passed to configure_rx().
 * @return        true if all register checks pass, false otherwise.
 */
bool verify_ble_rx(Driver &radio, const ble::RxConfig &config)
{
    printf("\n=== NRF24L01+ Diagnostic (legacy wrapper) ===\n");
    auto result = verify_ble_config(radio, config, DiagVerbosity::Detailed);
    bool ok = result.ok();
    printf("\nDiagnostic: %s\n\n",
           ok ? "PASS -- device ready" : "FAIL -- check connections / power");
    return ok;
}

} // namespace diag
} // namespace nrf24