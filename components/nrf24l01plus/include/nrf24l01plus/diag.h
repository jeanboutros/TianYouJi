#pragma once

#include <cstdint>
#include "nrf24l01plus/driver.h"
#include "nrf24l01plus/ble_config.h"

namespace nrf24 {
namespace diag {

/**
 * @brief Run a register-level diagnostic after BLE RX configuration.
 *
 * Reads back all registers programmed by nrf24::ble::configure_rx() and
 * compares them against expected values derived from the given RxConfig.
 * Results are printed to stdout.
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

} // namespace diag
} // namespace nrf24
