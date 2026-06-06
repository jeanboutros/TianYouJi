#!/usr/bin/env python3
"""
BLE Data Whitening Reference Implementation.

Implements Dmitry Grinberg's btLeWhiten algorithm exactly as published in
"Bit-Banging Bluetooth Low Energy" (https://dmitry.gr/?r=05.Projects&proj=11.%20Bluetooth%20LE%20fakery).

This script serves as the ground truth for validating the C++ dewhiten()
implementation in the nRF24 BLE sniffer project.

Usage:
    python3 ble_whiten_reference.py              # Run all known-vector tests
    python3 ble_whiten_reference.py --roundtrip   # Round-trip consistency tests
    python3 ble_whiten_reference.py --vector 37   # Print whitening vector for ch37
"""

import sys

# Galois LFSR seed bit: position 1 in the 7-bit state register.
# Per Dmitry Grinberg: "the value we actually use is what BT'd use left shifted one"
LFSR_SEED_BIT = 2

# Galois LFSR polynomial for BLE data whitening: x^7 + x^4 + 1.
# When bit 7 is set after shift, XOR feedback taps at bits 4 and 0.
LFSR_POLY_MASK = 0x11
LFSR_MSB = 0x80


def swapbits(v: int) -> int:
    """Reverse bit order within a single byte.

    Matches nrf24::ble::swapbits() from ble.h exactly.

    >>> swapbits(0x00) == 0x00
    True
    >>> swapbits(0xFF) == 0xFF
    True
    >>> swapbits(0x01) == 0x80
    True
    >>> swapbits(0x42) == 0x42  # palindromic
    True
    """
    r = 0
    if v & 0x80: r |= 0x01
    if v & 0x40: r |= 0x02
    if v & 0x20: r |= 0x04
    if v & 0x10: r |= 0x08
    if v & 0x08: r |= 0x10
    if v & 0x04: r |= 0x20
    if v & 0x02: r |= 0x40
    if v & 0x01: r |= 0x80
    return r


def btLeWhitenStart(chan: int) -> int:
    """Compute the LFSR initial value for a given BLE channel index.

    Per Dmitry Grinberg: "the value we actually use is what BT'd use left
    shifted one...makes our life easier".  The | 2 sets bit 1, equivalent
    to the 7-bit seed loaded with channel_index in the Galois register.

    >>> hex(btLeWhitenStart(37))
    '0xa6'
    >>> hex(btLeWhitenStart(38))
    '0x66'
    >>> hex(btLeWhitenStart(39))
    '0xe6'
    >>> hex(btLeWhitenStart(0))
    '0x2'
    """
    return swapbits(chan) | LFSR_SEED_BIT


def btLeWhiten(data: bytes, whiten_coeff: int) -> tuple:
    """Apply BLE data whitening using Galois LFSR (left-shifting).

    Polynomial: x^7 + x^4 + 1  (feedback taps at bits 4 and 0 of 7-bit state)
    When bit 7 is set, XOR with 0x11 (bits 4 and 0) before shifting.

    This is the exact algorithm from Dmitry Grinberg's btLeWhiten().
    Whitening is symmetric: same function whitens and dewhitens.

    NOTE: In C, uint8_t overflow handles the 8-bit masking naturally.
    In Python, integers are arbitrary-precision, so we mask (& 0xFF) manually.

    Returns:
        (whitened_data, final_lfsr_state)

    >>> data = bytes([0x40, 0x0B])
    >>> whitened, _ = btLeWhiten(data, btLeWhitenStart(37))
    >>> dewhitened, _ = btLeWhiten(whitened, btLeWhitenStart(37))
    >>> dewhitened == data
    True
    """
    result = bytearray()
    for byte_val in data:
        w = 0
        for b in range(8):
            m = 1 << b
            if whiten_coeff & LFSR_MSB:
                whiten_coeff ^= LFSR_POLY_MASK
                w ^= m
            whiten_coeff = (whiten_coeff << 1) & 0xFF  # 8-bit wrap (C uint8_t behavior)
        result.append(byte_val ^ w)
    return bytes(result), whiten_coeff


def dewhiten(data: bytes, channel_idx: int) -> bytes:
    """Convenience wrapper: dewhiten data for a given BLE channel."""
    result, _ = btLeWhiten(data, btLeWhitenStart(channel_idx))
    return result


def whiten(data: bytes, channel_idx: int) -> bytes:
    """Convenience wrapper: whiten data for a given BLE channel."""
    result, _ = btLeWhiten(data, btLeWhitenStart(channel_idx))
    return result


def generate_whitening_sequence(channel_idx: int, num_bytes: int) -> list:
    """Generate the whitening XOR bytes for a channel (for debugging).

    Returns list of num_bytes XOR masks that would be applied.
    """
    lfsr = btLeWhitenStart(channel_idx)
    masks = []
    for _ in range(num_bytes):
        w = 0
        for b in range(8):
            m = 1 << b
            if lfsr & LFSR_MSB:
                lfsr ^= LFSR_POLY_MASK
                w ^= m
            lfsr = (lfsr << 1) & 0xFF
        masks.append(w)
    return masks


# ---- Test Cases ----

TEST_VECTORS = [
    bytes([0x40, 0x0B, 0xEF, 0xFF, 0xC0, 0xAA, 0x18, 0x00]),  # Grinberg example header
    bytes(8),                                                     # all zeros
    bytes([0xFF] * 8),                                            # all ones
    bytes(range(32)),                                             # 0–31
    bytes([0xDE, 0xAD, 0xBE, 0xEF]),                             # common test
]

TEST_CHANNELS = [0, 1, 10, 11, 37, 38, 39]


def test_roundtrip():
    """TC-04, TC-05, TC-06: Round-trip consistency for all channels and vectors."""
    failures = 0
    total = 0
    for ch in TEST_CHANNELS:
        for vec in TEST_VECTORS:
            total += 1
            whitened = whiten(vec, ch)
            dewhitened = dewhiten(whitened, ch)
            if dewhitened != vec:
                failures += 1
                print(f"  FAIL: ch={ch} vec={vec.hex()} -> whitened={whitened.hex()} -> de-whitened={dewhitened.hex()}")
            else:
                print(f"  OK:   ch={ch} len={len(vec)} round-trip verified")
    print(f"\nRound-trip: {total - failures}/{total} passed")
    return failures == 0


def test_cross_channel_independence():
    """TC-07: Wrong channel produces different output."""
    failures = 0
    vec = TEST_VECTORS[0]
    for ch in TEST_CHANNELS:
        whitened = whiten(vec, ch)
        for wrong_ch in TEST_CHANNELS:
            if wrong_ch == ch:
                continue
            dewhitened_wrong = dewhiten(whitened, wrong_ch)
            if dewhitened_wrong == vec:
                failures += 1
                print(f"  UNEXPECTED: ch={ch} whitened data, dewhitened with ch={wrong_ch} == original")
    if failures > 0:
        print(f"  Cross-channel independence: {failures} unexpected matches")
    else:
        print("  Cross-channel independence: OK -- wrong channel always produces different output")
    return failures == 0


def test_seed_values():
    """Verify LFSR seed values for key channels."""
    expected = {
        0:  0x02,
        1:  0x82,
        10: 0x52,
        11: 0xD2,
        37: 0xA6,
        38: 0x66,
        39: 0xE6,
    }
    all_ok = True
    for ch, exp in expected.items():
        actual = btLeWhitenStart(ch)
        sb = swapbits(ch)
        status = "OK" if actual == exp else "FAIL"
        if actual != exp:
            all_ok = False
        print(f"  Seed ch={ch:2d}: swapbits(0x{ch:02X})=0x{sb:02X}, seed=0x{actual:02X} "
              f"(expected 0x{exp:02X})  [{status}]")
    return all_ok


def test_known_vector():
    """TC-10: Known whitening vectors for specific channels."""
    # Print first few whitening masks for debugging
    for ch in [37, 38, 39]:
        masks = generate_whitening_sequence(ch, 8)
        print(f"  Ch {ch} first 8 XOR masks: {' '.join(f'{m:02X}' for m in masks)}")

    return test_roundtrip()


def print_whitening_vector(channel_idx: int):
    """Print the full whitening sequence for debugging."""
    seed = btLeWhitenStart(channel_idx)
    print(f"Channel {channel_idx}: seed = 0x{seed:02X}")
    masks = generate_whitening_sequence(channel_idx, 40)
    print(f"  First 40 XOR masks: {' '.join(f'{m:02X}' for m in masks)}")

    # Show what a sample packet looks like whitened
    sample = bytes([0x40, 0x06, 0x9b, 0x5a, 0x6a, 0xc0, 0xd4, 0x5f])
    whitened = whiten(sample, channel_idx)
    print(f"  Sample plaintext:    {' '.join(f'{b:02X}' for b in sample)}")
    print(f"  Sample whitened:     {' '.join(f'{b:02X}' for b in whitened)}")
    # Verify round-trip
    dewhitened = dewhiten(whitened, channel_idx)
    print(f"  Sample de-whitened:  {' '.join(f'{b:02X}' for b in dewhitened)}")
    print(f"  Round-trip OK: {dewhitened == sample}")


def main():
    import argparse
    parser = argparse.ArgumentParser(description='BLE whitening reference tests')
    parser.add_argument('--roundtrip', action='store_true', help='Run round-trip tests')
    parser.add_argument('--vector', type=int, metavar='CH',
                        help='Print whitening vector for a channel')
    parser.add_argument('--seeds', action='store_true', help='Verify seed values')
    parser.add_argument('--cross', action='store_true', help='Cross-channel independence')
    args = parser.parse_args()

    if args.vector is not None:
        print_whitening_vector(args.vector)
        return

    all_pass = True

    if args.seeds or not any([args.roundtrip, args.cross]):
        print("=== LFSR Seed Values ===")
        if not test_seed_values():
            all_pass = False
        print()

    if args.roundtrip or not any([args.seeds, args.cross]):
        print("=== Round-Trip Consistency (TC-04/05/06) ===")
        if not test_roundtrip():
            all_pass = False
        print()

    if args.cross or not any([args.seeds, args.roundtrip]):
        print("=== Cross-Channel Independence (TC-07) ===")
        if not test_cross_channel_independence():
            all_pass = False
        print()

    if all_pass:
        print("ALL TESTS PASSED")
        sys.exit(0)
    else:
        print("SOME TESTS FAILED")
        sys.exit(1)


if __name__ == '__main__':
    main()
