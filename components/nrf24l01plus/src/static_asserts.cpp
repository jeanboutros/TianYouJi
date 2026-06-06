/**
 * @file static_asserts.cpp
 * @brief Compilation unit for register round-trip static_asserts.
 *
 * This file simply includes the header containing all compile-time
 * tests. If any @c static_assert fails, the build stops with a
 * compiler error identifying the exact register and condition that
 * was violated.
 *
 * There is zero runtime cost — all verification happens at compile time.
 * This translation unit produces no runtime code beyond an empty
 * object file.
 *
 * @code
 *   // If the build passes, every register to_byte()/from_byte()
 *   // round-trip is verified for all 256 byte values.
 * @endcode
 */

#include "nrf24l01plus/registers/static_asserts.h"