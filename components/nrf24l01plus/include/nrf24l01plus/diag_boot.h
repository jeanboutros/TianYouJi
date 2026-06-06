#pragma once

/**
 * @file diag_boot.h
 * @brief Structured boot-time diagnostic phases for the nRF24L01+ driver.
 *
 * Provides a typed, phased diagnostic sequence that replaces the boolean
 * spi_comm_test() and verify_ble_rx() functions with structured results
 * (status, detail, timing).  Each phase can be run independently or as
 * part of full_boot_diagnostic().
 *
 * @code
 *   nrf24::Driver radio(hal);
 *   nrf24::ble::RxConfig cfg;
 *   nrf24::diag::DiagOpts opts;
 *   opts.verbosity = nrf24::diag::DiagVerbosity::Detailed;
 *   auto result = nrf24::diag::full_boot_diagnostic(radio, cfg, opts);
 *   if (!result.overall_pass()) {
 *       // Inspect result.phases[0], result.phases[1], ...
 *   }
 * @endcode
 */

#include <cstdint>
#include <cstring>

#include "nrf24l01plus/driver.h"
#include "nrf24l01plus/ble_config.h"

namespace nrf24 {
namespace diag {

/**
 * @brief Identifies a single diagnostic phase.
 *
 * Each phase tests a distinct aspect of the nRF24L01+ setup:
 * - HalInit verifies GPIO and HAL readback.
 * - SpiComm verifies SPI connectivity, write-verify, and clone detection.
 * - BleConfig verifies BLE RX configuration registers.
 * - PostConfig verifies post-configuration register state cross-checks.
 * - CeState verifies CE pin, CONFIG, and FIFO state alignment.
 * - Extended performs a CE-high RPD/FIFO poll test on a live channel.
 *
 * Use as an index into DiagFullResult::phases.
 */
enum class DiagPhase : uint8_t {
    HalInit    = 0,  ///< CE GPIO readback, pin state verification
    SpiComm    = 1,  ///< SPI POR, write-verify, clone detection
    BleConfig  = 2,  ///< BLE RX configuration (nrf24::ble::configure_rx)
    PostConfig = 3,  ///< Post-configuration register verification
    CeState    = 4,  ///< CE state, CONFIG, FIFO cross-check
    Extended   = 5,  ///< Extended CE-high RPD/FIFO poll test
};

/**
 * @brief Maximum number of diagnostic phases (one per DiagPhase value).
 */
static constexpr uint8_t DIAG_PHASE_COUNT = 6;

/**
 * @brief Outcome of a single diagnostic phase.
 *
 * @code
 *   nrf24::diag::DiagPhaseResult r;
 *   r.status = nrf24::diag::DiagStatus::Pass;
 *   // or:
 *   r.status = nrf24::diag::DiagStatus::Warn;
 * @endcode
 */
enum class DiagStatus : uint8_t {
    Pass = 0,  ///< Phase completed successfully
    Fail = 1,  ///< Phase failed — blocking error
    Skip = 2,  ///< Phase skipped (prerequisite failed)
    Warn = 3,  ///< Passed with warnings (e.g., clone detected)
};

/**
 * @brief Verbosity level for diagnostic output.
 *
 * @code
 *   nrf24::diag::DiagOpts opts;
 *   opts.verbosity = nrf24::diag::DiagVerbosity::Detailed;
 * @endcode
 */
enum class DiagVerbosity : uint8_t {
    Silent   = 0,  ///< No output (structured results only)
    Summary  = 1,  ///< Phase pass/fail summary
    Detailed = 2,  ///< Per-register values and cross-checks
    Verbose  = 3,  ///< Timing data, raw dumps, CE transitions
};

/**
 * @brief Result of a single diagnostic phase.
 *
 * Contains the phase outcome, a human-readable detail string, and the
 * elapsed wall-clock time.  The detail string uses a fixed-size buffer
 * (no heap allocation) to remain safe in embedded bare-metal contexts.
 *
 * @code
 *   auto result = nrf24::diag::verify_hal_ce(radio, nrf24::diag::DiagVerbosity::Detailed);
 *   if (result.status == nrf24::diag::DiagStatus::Fail) {
 *       printf("HalInit failed: %s\n", result.detail);
 *   }
 * @endcode
 */
struct DiagPhaseResult {
    DiagStatus status = DiagStatus::Skip;  ///< Phase outcome
    char detail[128] = {};                  ///< Human-readable detail (NUL-terminated, no heap)
    uint32_t elapsed_us = 0;                ///< Wall-clock time for this phase (microseconds)

    /**
     * @brief Check whether this phase result indicates success.
     *
     * A phase is considered successful if its status is Pass or Warn.
     * Skip and Fail are not successful.
     *
     * @return true if status is Pass or Warn.
     */
    bool ok() const
    {
        return status == DiagStatus::Pass || status == DiagStatus::Warn;
    }
};

/**
 * @brief Aggregate result of all diagnostic phases.
 *
 * Contains one DiagPhaseResult for each DiagPhase, indexable by phase value.
 * Also tracks whether a clone chip was detected and whether this was a
 * warm boot (module retains state from prior session).
 *
 * @code
 *   auto result = nrf24::diag::full_boot_diagnostic(radio, cfg, opts);
 *   if (!result.overall_pass()) {
 *       for (uint8_t i = 0; i < nrf24::diag::DIAG_PHASE_COUNT; ++i) {
 *           auto &phase = result.phases[i];
 *           printf("Phase %d: status=%d detail=%s\n",
 *                  i, static_cast<int>(phase.status), phase.detail);
 *       }
 *   }
 * @endcode
 */
struct DiagFullResult {
    DiagPhaseResult phases[DIAG_PHASE_COUNT] = {};  ///< Per-phase results, indexed by DiagPhase value
    bool clone_detected = false;                      ///< True if Si24R1/BK2425 clone behaviour detected
    bool warm_boot = false;                           ///< True if module was already configured (ESP32 reset, not power cycle)

    /**
     * @brief Check whether all phases passed or passed with warnings.
     *
     * A diagnostic run is considered overall-pass if no phase has
     * DiagStatus::Fail.  Skipped phases are allowed (they indicate a
     * prerequisite failure, but that prerequisite's failure is the
     * actual problem).
     *
     * @return true if no phase has DiagStatus::Fail.
     */
    bool overall_pass() const
    {
        for (uint8_t i = 0; i < DIAG_PHASE_COUNT; ++i) {
            if (phases[i].status == DiagStatus::Fail) {
                return false;
            }
        }
        return true;
    }

    /**
     * @brief Return the first phase that failed, or a default if none failed.
     *
     * Useful for diagnostic messages when overall_pass() is false.
     * If overall_pass() is true (no failures), returns HalInit as a
     * sensible default — callers should always check overall_pass() first.
     *
     * @code
     *   if (!result.overall_pass()) {
     *       printf("First failure: phase %d\n",
     *              static_cast<int>(result.first_fail_phase()));
     *   }
     * @endcode
     *
     * @return DiagPhase value of the first failed phase, or HalInit if none.
     */
    DiagPhase first_fail_phase() const
    {
        for (uint8_t i = 0; i < DIAG_PHASE_COUNT; ++i) {
            if (phases[i].status == DiagStatus::Fail) {
                return static_cast<DiagPhase>(i);
            }
        }
        return DiagPhase::HalInit;
    }
};

/**
 * @brief Configuration options for the boot diagnostic sequence.
 *
 * @code
 *   nrf24::diag::DiagOpts opts;
 *   opts.verbosity          = nrf24::diag::DiagVerbosity::Detailed;
 *   opts.retry_count        = 3;
 *   opts.retry_delay_ms     = 5000;
 *   opts.extended_test_ch   = 37;
 *   opts.extended_poll_ms   = 100;
 *   opts.extended_duration_s = 5;
 *   auto result = nrf24::diag::full_boot_diagnostic(radio, cfg, opts);
 * @endcode
 */
struct DiagOpts {
    DiagVerbosity verbosity = DiagVerbosity::Detailed;  ///< Output verbosity level
    uint8_t retry_count = 3;            ///< Max retry attempts per phase before giving up
    uint16_t retry_delay_ms = 5000;     ///< Delay between retries in milliseconds
    uint8_t extended_test_ch = 37;       ///< BLE channel for extended CE-high test (use constants from nrf24::ble::ADV_CHANNELS)
    uint16_t extended_poll_ms = 100;     ///< RPD/FIFO poll interval during extended test (milliseconds)
    uint16_t extended_duration_s = 5;   ///< Total duration of extended CE-high test (seconds)
};

/**
 * @brief Run the full boot diagnostic sequence.
 *
 * Executes all six diagnostic phases in order (HalInit → SpiComm →
 * BleConfig → PostConfig → CeState → Extended).  If any early phase
 * fails, subsequent phases are marked Skip.
 *
 * On max retries exhausted (all retry_count attempts fail), the function
 * returns the DiagFullResult with overall_pass() == false.  The caller
 * (typically app_main) decides what to do — e.g. enter deep sleep and
 * restart, or log the failure and continue.  The library does NOT call
 * esp_deep_sleep_start() or esp_restart() (those are platform-specific
 * and belong in main/, not in the driver library).
 *
 * @code
 *   nrf24::Driver radio(hal);
 *   nrf24::ble::RxConfig cfg;
 *   nrf24::diag::DiagOpts opts;
 *   opts.verbosity = nrf24::diag::DiagVerbosity::Detailed;
 *   auto result = nrf24::diag::full_boot_diagnostic(radio, cfg, opts);
 *   if (result.clone_detected) {
 *       printf("Warning: clone chip detected\n");
 *   }
 * @endcode
 *
 * @param radio   Driver instance (must NOT be fully configured yet for HalInit/SpiComm).
 * @param config  BLE RX configuration to verify during BleConfig phase.
 * @param opts    Diagnostic options (verbosity, retries, extended test params).
 * @return        Aggregated results for all phases.
 */
DiagFullResult full_boot_diagnostic(Driver &radio,
                                    const ble::RxConfig &config,
                                    const DiagOpts &opts = {});

/**
 * @brief Phase 0: Verify HAL and CE GPIO readback.
 *
 * Tests that the HAL can communicate with the nRF24L01+ by reading the
 * STATUS register, and that CE pin state matches the driver's ce_high()
 * and ce_low() commands.
 *
 * @param radio      Driver instance (must be constructed but not yet configured).
 * @param verbosity  Output verbosity level.
 * @return           Phase result with status, detail string, and elapsed time.
 */
DiagPhaseResult verify_hal_ce(Driver &radio, DiagVerbosity verbosity);

/**
 * @brief Phase 1: Verify SPI communication, POR defaults, and detect clones.
 *
 * Performs three sub-stages:
 * 1. POR value check — reads registers and compares against datasheet defaults.
 * 2. Write-verify — writes known patterns and reads them back.
 * 3. Clone detection — tests EN_AA/EN_CRC override behaviour.
 *
 * Detects warm boot (module retains state from prior session) and
 * reports it in the result detail string.
 *
 * @param radio      Driver instance (must NOT have been configured yet).
 * @param verbosity  Output verbosity level.
 * @return           Phase result with status, detail, and elapsed time.
 *                   status is Warn if clone detected but SPI works.
 */
DiagPhaseResult verify_spi_comm(Driver &radio, DiagVerbosity verbosity);

/**
 * @brief Phase 2: Verify BLE RX configuration.
 *
 * Calls nrf24::ble::configure_rx() and verifies that all registers
 * were programmed correctly.  Checks CONFIG (EN_CRC must be 0 for BLE),
 * EN_AA, EN_RXADDR, SETUP_AW, SETUP_RETR, RF_CH, RF_SETUP, RX_PW_P0,
 * DYNPD, FEATURE, RX_ADDR_P0, and TX_ADDR.
 *
 * @param radio      Driver instance (must have passed verify_spi_comm).
 * @param config     BLE RX configuration to apply and verify.
 * @param verbosity  Output verbosity level.
 * @return           Phase result with status, detail, and elapsed time.
 */
DiagPhaseResult verify_ble_config(Driver &radio,
                                  const ble::RxConfig &config,
                                  DiagVerbosity verbosity);

/**
 * @brief Phase 4: Verify CE state, CONFIG, and FIFO consistency.
 *
 * Checks that CE is in the expected state (high for RX mode after
 * configure_rx), that CONFIG matches the expected BLE configuration,
 * and that FIFO_STATUS is consistent with the operational state.
 *
 * @param radio      Driver instance (must be configured and in RX mode).
 * @param verbosity  Output verbosity level.
 * @return           Phase result with status, detail, and elapsed time.
 */
DiagPhaseResult verify_ce_state(Driver &radio, DiagVerbosity verbosity);

/**
 * @brief Phase 5: Extended CE-high RPD/FIFO poll test.
 *
 * Sets CE high on the specified BLE channel and polls RPD (Received
 * Power Detector) and FIFO status for the specified duration.  This
 * tests whether the nRF24L01+ can detect RF energy and receive packets
 * in a real environment.
 *
 * @param radio         Driver instance (must be configured for BLE RX).
 * @param ble_channel   BLE channel index for the test (use values from
 *                      nrf24::ble::ADV_CHANNELS, e.g. 37, 38, 39).
 * @param poll_ms       Polling interval in milliseconds between RPD/FIFO reads.
 * @param duration_s    Total duration of the test in seconds.
 * @param verbosity     Output verbosity level.
 * @return              Phase result with status, detail, and elapsed time.
 */
DiagPhaseResult verify_extended_ce(Driver &radio,
                                   uint8_t ble_channel,
                                   uint16_t poll_ms,
                                   uint16_t duration_s,
                                   DiagVerbosity verbosity);

} // namespace diag
} // namespace nrf24