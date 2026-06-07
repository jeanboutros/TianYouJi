#pragma once

/**
 * @file register_tests.h
 * @brief Runtime verification for nRF24L01+ register structs.
 *
 * Provides run_register_tests(), which exercises non-constexpr
 * serialisation paths that cannot be verified with @c static_assert.
 * All constexpr register tests live in
 * @ref nrf24l01plus/registers/static_asserts.h and are compiled as
 * part of @c static_asserts.cpp.
 *
 * @code
 *   // Call from app_main() or a test task:
 *   if (!nrf24::test::run_register_tests()) {
 *       printf("Register test FAILED\n");
 *   }
 * @endcode
 */

namespace nrf24 {
namespace test {

/**
 * @brief Run runtime register round-trip tests for non-constexpr paths.
 *
 * Tests all serialisation paths that rely on @c to_byte() or
 * @c from_byte() functions that are not @c constexpr (currently only
 * RfSetup, due to its non-adjacent DataRate encoding).
 *
 * On success, prints a summary line for each register.  On failure,
 * prints a diagnostic line identifying the failing assertion and
 * returns false.
 *
 * @return true if all assertions pass, false on the first failure.
 */
bool run_register_tests();

} // namespace test
} // namespace nrf24