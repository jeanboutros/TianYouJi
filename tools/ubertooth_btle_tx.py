#!/usr/bin/env python3
"""
Ubertooth BLE Advertising Transmitter

Transmits known BLE advertising packets on specific channels (37, 38, 39)
using an Ubertooth One device. Uses direct USB commands to the firmware's
BTLE_SLAVE mode (UBERTOOTH_BTLE_SLAVE = 54, UBERTOOTH_LE_SET_ADV_DATA = 71).

This is used for dewhitening validation: the firmware constructs proper BLE
ADV_IND packets with correct CRC and data whitening, so the ESP32 nRF24L01+
sniffer should be able to dewhiten and decode the same advertising data.

Reference:
  - Bluetooth Core Spec Vol 6 Part B §2.3 — Advertising PDU format
  - Bluetooth Core Spec Vol 6 Part B §1.4.1 — Channel mapping
  - Bluetooth Core Spec Vol 6 Part B §3.2 — Data whitening
  - Ubertooth firmware: bluetooth_rxtx.c bt_slave_le() function
  - Ubertooth API: ubertooth_interface.h (UBERTOOTH_BTLE_SLAVE, UBERTOOTH_LE_SET_ADV_DATA)

USB Protocol:
  CTRL_OUT = vendor_type | endpoint_out
  UBERTOOTH_BTLE_SLAVE:    ctrl_transfer(CTRL_OUT, cmd=54, data=6-byte MAC)
  UBERTOOTH_LE_SET_ADV_DATA: ctrl_transfer(CTRL_OUT, cmd=71, data=adv_data_bytes)
  UBERTOOTH_SET_CHANNEL:    ctrl_transfer(CTRL_OUT, cmd=12, wIndex=0, wValue=freq_MHz)
  UBERTOOTH_STOP:           ctrl_transfer(CTRL_OUT, cmd=21)

Channel mapping (BLE spec Vol 6 Part B §1.4.1):
  ch37 = 2402 MHz  (advertising)
  ch38 = 2426 MHz  (advertising)
  ch39 = 2480 MHz  (advertising)
"""

import sys
import time
import argparse
import signal

try:
    import usb.core
    import usb.util
except ImportError:
    print("ERROR: pyusb not installed. Install with: pip install pyusb", file=sys.stderr)
    sys.exit(1)

# Ubertooth USB Vendor/Product IDs
UBERTOOTH_VENDOR_ID = 0x1d50
UBERTOOTH_PRODUCT_ID = 0x6002

# USB control transfer request type (from ubertooth_control.h)
# CTRL_OUT = LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_ENDPOINT_OUT
CTRL_OUT = usb.util.build_request_type(usb.util.CTRL_OUT,
                                         usb.util.CTRL_TYPE_VENDOR,
                                         usb.util.CTRL_RECIPIENT_DEVICE)

# Ubertooth USB command codes (from ubertooth_interface.h)
UBERTOOTH_STOP               = 21
UBERTOOTH_SET_CHANNEL         = 12
UBERTOOTH_BTLE_SLAVE          = 54
UBERTOOTH_LE_SET_ADV_DATA     = 71

# BLE channel to frequency mapping (Bluetooth Core Spec Vol 6 Part B §1.4.1)
BLE_ADV_CHANNELS = {
    37: 2402,
    38: 2426,
    39: 2480,
}


def find_ubertooth():
    """Find and return the first connected Ubertooth One device."""
    dev = usb.core.find(idVendor=UBERTOOTH_VENDOR_ID, idProduct=UBERTOOTH_PRODUCT_ID)
    if dev is None:
        print("ERROR: Ubertooth One not found (USB VID:PID = 1d50:6002)", file=sys.stderr)
        print("  - Check device is plugged in: lsusb | grep 1d50", file=sys.stderr)
        print("  - Check udev rules: /etc/udev/rules.d/40-ubertooth.rules", file=sys.stderr)
        print("  - Check user is in 'plugdev' group: groups", file=sys.stderr)
        return None

    try:
        # Detach kernel driver if claimed
        if dev.is_kernel_driver_active(0):
            dev.detach_kernel_driver(0)
        dev.set_configuration()
        usb.util.claim_interface(dev, 0)
    except usb.core.USBError as e:
        print(f"ERROR: Cannot claim USB interface: {e}", file=sys.stderr)
        print("  - Another process may be using the device (ubertooth-btle, ubertooth-specan)", file=sys.stderr)
        print("  - Stop it first: ubertooth-util -S", file=sys.stderr)
        return None

    return dev


def ubertooth_stop(dev):
    """Stop any ongoing Ubertooth operation."""
    try:
        dev.ctrl_transfer(CTRL_OUT, UBERTOOTH_STOP, 0, 0, b'', 1000)
    except usb.core.USBError:
        pass
    time.sleep(0.1)


def set_channel(dev, freq_mhz):
    """
    Set the RF channel to the specified frequency in MHz.
    Per firmware: UBERTOOTH_SET_CHANNEL sets le_adv_channel for BTLE_SLAVE mode.
    Valid advertising channels: 2402, 2426, 2480 MHz.
    """
    dev.ctrl_transfer(CTRL_OUT, UBERTOOTH_SET_CHANNEL, freq_mhz, 0, b'', 1000)


def set_adv_data(dev, adv_data):
    """
    Set the advertising data payload for BTLE_SLAVE mode.
    Per firmware bt_slave_le(): this data is placed after AdvA in the ADV_IND PDU.
    Max length: 255 bytes (LE_ADV_MAX_LEN per BLE 5.0 spec).
    The firmware handles CRC computation and data whitening automatically.
    """
    if len(adv_data) > 255:
        print(f"ERROR: Adv data too long ({len(adv_data)} > 255 bytes)", file=sys.stderr)
        return False
    dev.ctrl_transfer(CTRL_OUT, UBERTOOTH_LE_SET_ADV_DATA, 0, 0, adv_data, 1000)
    return True


def start_btle_slave(dev, mac_bytes):
    """
    Start BLE advertising slave mode with the given MAC address.
    Per firmware: bt_slave_le() constructs ADV_IND packets as:
      PDU Header (2 bytes): type=0x00 (ADV_IND), length=6+len(adv_data)
      AdvA (6 bytes): MAC address in reversed byte order
      AdvData (N bytes): from le_adv_data
      CRC (3 bytes): computed by firmware
    The firmware whitens the entire PDU before transmission.
    Transmits repeatedly with ~100ms interval on the selected le_adv_channel.
    """
    if len(mac_bytes) != 6:
        print(f"ERROR: MAC address must be 6 bytes, got {len(mac_bytes)}", file=sys.stderr)
        return False
    dev.ctrl_transfer(CTRL_OUT, UBERTOOTH_BTLE_SLAVE, 0, 0, mac_bytes, 1000)
    return True


def parse_mac(mac_str):
    """Parse a MAC address string like 'AA:BB:CC:DD:EE:FF' into 6 bytes."""
    parts = mac_str.replace(':', '').replace('-', '')
    if len(parts) != 12:
        raise ValueError(f"Invalid MAC address: '{mac_str}' (expected 6 hex bytes)")
    try:
        return bytes.fromhex(parts)
    except ValueError:
        raise ValueError(f"Invalid hex in MAC address: '{mac_str}'")


def parse_hex_data(hex_str):
    """Parse a hex string like '0201050FFF4C00' into bytes."""
    hex_str = hex_str.replace(' ', '').replace('0x', '')
    if len(hex_str) % 2 != 0:
        raise ValueError(f"Hex data has odd number of characters: '{hex_str}'")
    try:
        return bytes.fromhex(hex_str)
    except ValueError:
        raise ValueError(f"Invalid hex data: '{hex_str}'")


# Global for signal handler
running = True
dev_handle = None

def signal_handler(sig, frame):
    global running, dev_handle
    running = False
    if dev_handle:
        print("\nStopping Ubertooth TX...")
        ubertooth_stop(dev_handle)
        try:
            usb.util.release_interface(dev_handle, 0)
        except:
            pass
    sys.exit(0)


def main():
    global running, dev_handle

    parser = argparse.ArgumentParser(
        description='Transmit BLE advertising packets using Ubertooth One',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Simple ADV_IND with MAC and "Hello" advertising data on channel 37
  %(prog)s -m AA:BB:CC:DD:EE:FF -d 02FF48656C6C6F -c 37

  # Transmit on all three advertising channels (cycle every 5s)
  %(prog)s -m AA:BB:CC:DD:EE:FF -d 020105 -C 37,38,39 -i 5

  # Transmit standard Flags + Complete Local Name "TEST"
  %(prog)s -m DE:AD:BE:EF:00:01 -d 0201060B0954455354 -c 38

The firmware automatically:
  - Constructs ADV_IND PDU: {Header(2), AdvA(6), AdvData(N), CRC(3)}
  - Applies BLE data whitening (x^7+x^4+1 LFSR, seed = channel_idx + 64)
  - Computes CRC-24 with init=0x555555 (advertising channel CRC init)
  - Sets access address 0x8E89BED6
  - Transmits with ~100ms interval
"""
    )

    parser.add_argument('-m', '--mac', required=True,
                        help='MAC address (e.g., AA:BB:CC:DD:EE:FF)')
    parser.add_argument('-d', '--data', required=True,
                        help='Advertising data as hex string (e.g., 0201050FFF4C00)')
    parser.add_argument('-c', '--channel', type=int, default=37,
                        help='Advertising channel index (37, 38, or 39). Default: 37')
    parser.add_argument('-C', '--channels',
                        help='Cycle through comma-separated channels (e.g., 37,38,39)')
    parser.add_argument('-i', '--interval', type=float, default=5.0,
                        help='Seconds between channel switches when cycling. Default: 5.')
    parser.add_argument('-n', '--count', type=int, default=0,
                        help='Number of packets to send (0=infinite). Default: 0')
    parser.add_argument('-t', '--timeout', type=float, default=0,
                        help='Total transmit time in seconds (0=infinite). Default: 0')
    parser.add_argument('-v', '--verbose', action='store_true',
                        help='Print packet details for each transmission')

    args = parser.parse_args()

    # Parse and validate inputs
    try:
        mac_bytes = parse_mac(args.mac)
    except ValueError as e:
        print(f"ERROR: {e}", file=sys.stderr)
        sys.exit(1)

    try:
        adv_data = parse_hex_data(args.data)
    except ValueError as e:
        print(f"ERROR: {e}", file=sys.stderr)
        sys.exit(1)

    # Determine channels
    if args.channels:
        try:
            channels = [int(c.strip()) for c in args.channels.split(',')]
        except ValueError:
            print(f"ERROR: Invalid channel list: '{args.channels}'", file=sys.stderr)
            sys.exit(1)
    else:
        channels = [args.channel]

    for ch in channels:
        if ch not in BLE_ADV_CHANNELS:
            print(f"ERROR: Invalid channel {ch}. Must be 37, 38, or 39.", file=sys.stderr)
            sys.exit(1)

    # Find Ubertooth
    dev = find_ubertooth()
    if dev is None:
        sys.exit(1)

    dev_handle = dev

    # Install signal handler
    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)

    # Stop any ongoing operation
    ubertooth_stop(dev)
    time.sleep(0.2)

    # Set advertising data
    if not set_adv_data(dev, adv_data):
        sys.exit(1)

    if args.verbose:
        print(f"BLE Advertising Transmitter")
        print(f"  MAC Address:  {':'.join(f'{b:02X}' for b in mac_bytes)}")
        print(f"  Adv Data:     {adv_data.hex().upper()}")
        print(f"  Adv Data Len: {len(adv_data)} bytes")
        print(f"  Channels:     {channels}")
        print(f"  PDU Type:     ADV_IND (0x00)")
        print(f"  CRC Init:     0x555555 (advertising)")
        print(f"  Access Addr:  0x8E89BED6")
        print()

    # Start transmitting
    start_time = time.time()
    pkt_count = 0
    ch_idx = 0
    last_switch = start_time

    current_ch = channels[ch_idx]
    freq = BLE_ADV_CHANNELS[current_ch]
    set_channel(dev, freq)

    if args.verbose:
        print(f"  -> Channel {current_ch} ({freq} MHz)")

    start_btle_slave(dev, mac_bytes)

    try:
        while running:
            now = time.time()
            elapsed = now - start_time

            # Check timeout
            if args.timeout > 0 and elapsed >= args.timeout:
                if args.verbose:
                    print(f"\n  Timeout reached ({args.timeout}s)")
                break

            # Check count
            if args.count > 0 and pkt_count >= args.count:
                if args.verbose:
                    print(f"\n  Packet count reached ({args.count})")
                break

            # Channel cycling
            if len(channels) > 1 and (now - last_switch) >= args.interval:
                ubertooth_stop(dev)
                time.sleep(0.1)

                ch_idx = (ch_idx + 1) % len(channels)
                current_ch = channels[ch_idx]
                freq = BLE_ADV_CHANNELS[current_ch]
                set_channel(dev, freq)

                if args.verbose:
                    print(f"  -> Channel {current_ch} ({freq} MHz)")

                # Restart slave mode with same data
                start_btle_slave(dev, mac_bytes)
                last_switch = now

            # Count packets (~100ms per packet per firmware)
            time.sleep(0.1)
            pkt_count += 1

            if args.verbose and pkt_count % 10 == 0:
                print(f"  [{pkt_count} pkts, {elapsed:.1f}s] ch={current_ch} freq={freq} MHz")

    except KeyboardInterrupt:
        pass
    finally:
        print("\nStopping Ubertooth TX...")
        ubertooth_stop(dev)
        time.sleep(0.2)
        try:
            usb.util.release_interface(dev, 0)
        except:
            pass
        if args.verbose:
            duration = time.time() - start_time
            print(f"  Sent ~{pkt_count} packets in {duration:.1f}s")
            print("  Ubertooth stopped.")


if __name__ == '__main__':
    main()
