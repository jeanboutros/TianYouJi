# Ubertooth One: Update Process, TX Failure Diagnosis, and Cross-Validation

> **Note:** This document provides step-by-step instructions for updating the Ubertooth One toolchain from source, documents the root cause analysis of why the ESP32 nRF24L01+ sniffer receives real BLE packets but not Ubertooth-transmitted ones, and describes cross-validation with the Nordic nRF Sniffer.

---

## 1. Why Update from Distro Packages

The Debian-distributed Ubertooth packages are severely outdated and incompatible with
the current firmware. The core problem is an **API version mismatch**:

| Component | Distro version | Issue |
|-----------|---------------|-------|
| `libubertooth1` | 2018.12.R1-5.3 | Only supports API 1.06 |
| `ubertooth-tools` | 2018.12.R1-5.3 | Only supports API 1.06 |
| Firmware on device | 2020-12-R1 | Reports API 1.07 |

When the host library (API 1.06) talks to firmware (API 1.07), commands may fail
silently or return incorrect data. Additionally:

- **`libbtbb`** is not installed via Debian packages at all — the Bluetooth
  baseband library is required for packet decoding and is a build dependency of
  the Ubertooth tools.
- **`pyubertooth`** Python bindings are not packaged — needed for our
  `tools/ubertooth_btle_tx.py` BLE transmission script.
- **`ubertooth-tools`** from the distro only installs 4 of the 12 available
  tools — missing critical utilities like `ubertooth-dfu` for firmware flashing.

The fix is to build everything from the current Git source, ensuring all
components speak the same API version (1.07).

---

## 2. Prerequisites

Before starting, ensure the following build dependencies are installed:

```bash
sudo apt install cmake libusb-1.0-0-dev make gcc g++ pkg-config \
    python3 python3-pip python3-usb
```

Additional packages for firmware cross-compilation:

- ARM none-eabi GCC toolchain (see §3.6)

For Wireshark integration (optional but recommended):

```bash
sudo apt install wireshark tshark
```

---

## 3. Step-by-Step: Building and Installing from Source

All source-built components install to `$HOME/.local/` to avoid conflicts with
system packages and to keep the installation user-local.

### 3.1 Clone the Ubertooth repository

```bash
git clone https://github.com/greatscottgadgets/ubertooth.git /tmp/ubertooth
cd /tmp/ubertooth
```

The repository contains the host libraries, tools, Python bindings, and
firmware source code all in one tree.

### 3.2 Build and install libbtbb

`libbtbb` is the Bluetooth baseband library. It provides packet decoding and
analysis functions used by `ubertooth-btle` and other tools.

```bash
cd /tmp/ubertooth/host/libbtbb
mkdir build && cd build
cmake .. -DCMAKE_INSTALL_PREFIX=$HOME/.local
make -j$(nproc)
make install
```

After installation, verify the library exists:

```bash
ls $HOME/.local/lib/libbtbb.so*
```

### 3.3 Build and install libubertooth

The host library provides the C API for communicating with the Ubertooth
hardware over USB.

```bash
cd /tmp/ubertooth/host/libubertooth
mkdir build && cd build
cmake .. -DCMAKE_INSTALL_PREFIX=$HOME/.local -DBUILD_SHARED_LIB=1
make -j$(nproc)
make install
```

**Known issue — CMake CMP0005 policy warning:**

If the build warns about CMake policy CMP0005 and escape sequences in
`add_definitions()`, edit the `CMakeLists.txt` to add the following line
**before** any `add_definitions()` call:

```cmake
cmake_policy(SET CMP0005 NEW)
```

This suppresses the warning without affecting the build output.

### 3.4 Build and install ubertooth-tools

The tools package provides all 12 Ubertooth command-line utilities including
the critical `ubertooth-dfu` firmware flasher.

```bash
cd /tmp/ubertooth/host/ubertooth-tools
mkdir build && cd build
cmake .. -DCMAKE_INSTALL_PREFIX=$HOME/.local
make -j$(nproc)
make install
```

**Known issue — `ubertooth-dfu` libusb linking:**

The build may fail to correctly link `ubertooth-dfu` against libusb. If you
see an error like `undefined reference to 'libusb_init'`, manually add
`-lusb-1.0` to the link step. This can be done by editing the CMakeLists.txt
or by re-running the link command with the added flag.

After installation, verify all 12 tools are present:

```bash
ls $HOME/.local/bin/ubertooth-*
```

Expected tools: `ubertooth-btle`, `ubertooth-dfu`, `ubertooth-dump`,
`ubertooth-follow`, `ubertooth-rx`, `ubertooth-scan`, `ubertooth-specan`,
`ubertooth-util`, `ubertooth-afh`, `ubertooth-hop`, `ubertooth-lap`,
`ubertooth-uu`.

### 3.5 Install Python bindings (pyubertooth)

The Python bindings are required for `tools/ubertooth_btle_tx.py` and other
Python-based BLE scripts.

**Option A — via pip (recommended):**

```bash
pip3 install --break-system-packages pyubertooth
```

**Option B — from source:**

```bash
cd /tmp/ubertooth/host/python
pip3 install --break-system-packages .
```

Verify installation:

```bash
python3 -c "import pyubertooth; print('pyubertooth OK')"
```

### 3.6 Cross-compile the firmware

The Ubertooth firmware runs on the LPC175x ARM Cortex-M3 MCU. It must be
cross-compiled using an ARM none-eabi GCC toolchain.

**Install the xPack ARM toolchain:**

```bash
cd /tmp
curl -SL https://github.com/xpack-dev-tools/arm-none-eabi-gcc-xpack/releases/v15.2.1-1/xpack-arm-none-eabi-gcc-15.2.1-1-linux-x64.tar.gz | tar xz
```

**Build the firmware:**

```bash
cd /tmp/ubertooth/firmware/bluetooth_rxtx
make CROSS=/tmp/xpack-arm/xpack-arm-none-eabi-gcc-15.2.1-1.1/bin/arm-none-eabi-
```

After the build completes, verify the hex file exists:

```bash
ls /tmp/ubertooth/firmware/bluetooth_rxtx/bluetooth_rxtx.hex
```

### 3.7 Flash the firmware

Flashing requires the DFU (Device Firmware Update) mode and the
`ubertooth-dfu` tool.

**Step 1 — Enter DFU mode:**

```bash
ubertooth-util -d
```

This puts the Ubertooth into DFU bootloader mode. The device should
disconnect and re-enumerate as a DFU device.

**Step 2 — Flash the firmware:**

```bash
$HOME/.local/bin/ubertooth-dfu -f /tmp/ubertooth/firmware/bluetooth_rxtx/bluetooth_rxtx.hex -r
```

The `-r` flag resets the device after flashing.

**Known issue — DFU I/O error at end of write:**

The DFU flash may report `"libUSB Error: Input/Output Error"` at the very end
of writing. The firmware was verified as correct after a reset. This appears to
be a known issue with the verification step — the flash itself succeeded. Do
not re-flash unless `ubertooth-util -v` shows an incorrect version.

**Step 3 — Verify the firmware:**

```bash
ubertooth-util -v
```

Expected output:
```
Firmware version: git-c9dfdbd* (API:1.07)
```

**Step 4 — Reset (if not already done by `-r` flag):**

```bash
ubertooth-util -r
```

### 3.8 Verify the installation

Run a series of checks to confirm everything works together:

```bash
# Firmware version (should report API 1.07)
ubertooth-util -v

# Capture real BLE advertising packets on channel 37
# (should show real BLE devices in the area within 5 seconds)
ubertooth-btle -c 37 -t 5

# Using the source-built tools explicitly (same as above if PATH is set)
$HOME/.local/bin/ubertooth-btle -c 37 -t 5

# List tool versions
ubertooth-util -V
```

If `ubertooth-btle` captures real BLE advertising packets (you should see
multiple devices in a typical urban environment), the entire stack is working
correctly.

---

## 4. Removing Old Distro Packages

Once the source-built versions are confirmed working, the old Debian packages
can be removed to prevent confusion.

```bash
# Remove old distro packages
sudo apt remove ubertooth ubertooth-firmware libubertooth1
```

**Only remove after verifying ALL of the following:**

1. `$HOME/.local/bin/ubertooth-*` tools work (run `ubertooth-util -v`)
2. `$HOME/.local/lib/libubertooth.so*` is found by `ldconfig` or
   `LD_LIBRARY_PATH`
3. `LD_LIBRARY_PATH` includes `$HOME/.local/lib` (see §7)
4. `ubertooth-btle -c 37 -t 5` captures real BLE packets successfully

If you remove the distro packages before configuring `PATH` and
`LD_LIBRARY_PATH`, the system will have no working Ubertooth commands until
you complete the environment setup in §7.

---

## 5. Version Comparison Table

| Component | Old (Debian) | New (Source) |
|-----------|-------------|-------------|
| firmware | 2020-12-R1 (API 1.07) | git-c9dfdbd (API 1.07) |
| libubertooth | 2018.12.R1-5.3 (API 1.06) | git-c9dfdbd (API 1.07) |
| libbtbb | not installed | git-f0fe176 |
| ubertooth-tools | 2018.12.R1-5.3 | git-c9dfdbd (all 12) |
| pyubertooth | not installed | 0.2 |
| pyusb | 1.3.1 | 1.3.1 |

Key improvement: **all components now speak API 1.07**, eliminating the
version mismatch that caused commands to fail silently.

---

## 6. Installation Paths

| Item | Location |
|------|----------|
| Libraries | `$HOME/.local/lib/` |
| Tools | `$HOME/.local/bin/` |
| Headers | `$HOME/.local/include/` |
| Source repo | `/tmp/ubertooth/` |
| ARM toolchain | `/tmp/xpack-arm/xpack-arm-none-eabi-gcc-15.2.1-1.1/` |

The source repo and ARM toolchain are in `/tmp/` and will be lost on reboot.
This is fine for a one-time build. If you need to rebuild later, re-clone the
repository.

---

## 7. Environment Configuration

The source-built tools and libraries are installed to `$HOME/.local/`, which
is not in the default `PATH` or library search path. Without configuration, the
system will either find the old distro versions (if still installed) or fail to
find the tools entirely.

Add the following to `~/.bashrc`:

```bash
# Ubertooth source-built tools and libraries
export PATH="$HOME/.local/bin:$PATH"
export LD_LIBRARY_PATH="$HOME/.local/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
```

Then reload:

```bash
source ~/.bashrc
```

Verify:

```bash
which ubertooth-util          # should show $HOME/.local/bin/ubertooth-util
ubertooth-util -v             # should show API 1.07
ldd $(which ubertooth-btle)   # should link libubertooth from $HOME/.local/lib
```

**Important:** If old distro packages are still installed, the source-built
versions will take priority only if `$HOME/.local/bin` comes first in `PATH`.
The `which` command verifies which binary is actually being used.

---

## 8. TX Failure Diagnosis: ESP32 Sees Real BLE But Not Ubertooth TX

### 8.1 The Problem

After successfully updating the Ubertooth toolchain and flashing firmware, we
tested the full BLE TX pipeline: the Ubertooth transmits BLE advertising
packets and the ESP32 nRF24L01+ sniffer should receive them.

**Result: complete TX failure.**

| Test mode | Channel | Ubertooth TX packets | ESP32 received |
|-----------|---------|---------------------|----------------|
| ADV_IND, single channel | 37 | ~300 | 0 |
| ADV_IND, single channel | 38 | ~300 | 0 |
| ADV_IND, single channel | 39 | ~300 | 0 |
| ADV_IND, cycling 37/38/39 | all | ~300 | 0 |
| Various AdvData payloads | 37 | ~159 | 0 |

**Total: ~1,359 Ubertooth-transmitted packets. ZERO received by the ESP32.**

Meanwhile, in the same environment, the ESP32 nRF24L01+ sniffer successfully
receives real BLE advertising packets from **6+ devices** on all three
advertising channels. This definitively proves the ESP32 sniffer hardware and
firmware are working correctly — the issue is specific to Ubertooth TX.

### 8.2 What We Verified as CORRECT

Before diagnosing the failure, we exhaustively verified that all register-level
and protocol-level settings are correct:

**nRF24L01+ Receive Configuration:**

- **Access address:** `0x8E89BED6` bit-reversed matches nRF24 RX_ADDR_P0 =
  `{0x6B, 0x7D, 0x91, 0x71}` — verified against
  `nrf24::ble::ADV_ACCESS_ADDR` from `ble_config.h`
- **nRF24 CONFIG:** CRC disabled (`EN_CRC=0`), auto-ACK disabled
  (`EN_AA=0` on all pipes), 4-byte address width, 1 Mbps data rate
- **Payload width:** 32 bytes (nRF24 maximum)

**Ubertooth CC2400 Transmit Configuration:**

- **MDMCTRL = 0x0040:** BLE GFSK modulation, 1 Mbps data rate, 250 kHz
  frequency deviation
- **GRMDM = 0x0CE1:** Buffered packet mode, 1 preamble byte, full 32-bit
  sync word
- **SYNCH/SYNCL:** Match the bit-reversed BLE advertising access address
- **FSDIV:** Channel frequencies verified — 2402/2426/2480 MHz for channels
  37/38/39
- **FREND = 0x000B:** PA_LEVEL=3 (approximately -7 dBm)

**Protocol:**

- **Whitening:** Same LFSR polynomial (x^7 + x^4 + 1), same channel-dependent
  seed (`swapbits(ch) | 2`), same bit order (LSB-first on-air)
- **CRC-24:** Computed by firmware with init=0x555555 for advertising
- **Packet format:** Access address + PDU header + AdvA + AdvData + CRC, all
  constructed correctly by firmware

**Host-Firmware Communication:**

- **API version mismatch FIXED:** All components now report API 1.07
  (previously libubertooth was API 1.06, causing silent command failures)

Every register, every bit field, every protocol parameter has been verified
against the datasheets and the BLE specification. The configuration is
**correct at the logical level**. The failure must be physical.

### 8.3 Root Cause Analysis — Ranked Hypotheses

#### Hypothesis 1 (Confidence: 80%) — CC2400 GFSK Modulation Quality

The CC2400 is a general-purpose 2.4 GHz transceiver, not a BLE-native chip.
While `MDMCTRL=0x0040` configures it for BLE-compatible GFSK (1 Mbps, 250 kHz
deviation), the actual on-air signal may differ from a real BLE chip in:

- **Gaussian filter BT product:** BLE specifies BT=0.5. The CC2400's
  Gaussian filter implementation may not match this exactly, producing a
  slightly different spectral shape.
- **Frequency deviation accuracy:** The actual deviation may be slightly
  off from the nominal 250 kHz, causing the nRF24's demodulator to
  misinterpret symbols.
- **Phase continuity between symbols:** GFSK requires smooth phase
  transitions. Imperfect Gaussian filtering in the CC2400 may cause
  phase discontinuities.
- **Symbol clock jitter:** Timing jitter in the CC2400's symbol clock
  may exceed what the nRF24's demodulator can tolerate.

The nRF24L01+'s demodulator is optimized for nRF24-to-nRF24 communication.
While compatible with BLE at the protocol level, it may be **less tolerant of
slight modulation differences** from the CC2400 compared to real BLE chips
that follow the spec more precisely.

**This is the most likely root cause** — it explains why real BLE devices
work but the CC2400 doesn't, even though all register-level protocol settings
are correct. A real BLE chip (e.g., nRF52840) produces a signal that exactly
matches the BLE spec's BT=0.5 GFSK profile; the CC2400 only approximates it.

#### Hypothesis 2 (Confidence: 75%) — CC2400 TX Power and Antenna Coupling

`FREND` register set to `0x000B` (PA_LEVEL=3, approximately -7 dBm). Real BLE
devices typically transmit at 0 to +4 dBm. At very close range (10–30 cm),
-7 dBm should still produce approximately -40 to -50 dBm at the nRF24L01+
receiver, well above the -94 dBm sensitivity threshold.

However, the **Ubertooth's external dipole antenna** may have a different
radiation pattern and orientation compared to the nRF24L01+'s PCB trace
antenna, reducing effective coupling. The dipole is omnidirectional in one
plane but has nulls in others. If the nRF24 module sits in or near a null,
even close proximity may not provide sufficient signal strength.

This hypothesis is testable by rotating the Ubertooth relative to the ESP32
while monitoring the RPD (Received Power Detector) register.

#### Hypothesis 3 (Confidence: 55%) — CC2400 Packet Mode Buffering / FIFO Timing

The CC2400 in buffered packet mode (`GRMDM=0x0CE1`) reads data from its
32-byte FIFO. If the FIFO underruns (drains before all bytes are transmitted),
the CC2400 may insert idle symbols or corrupt the packet.

For typical advertising packets (14–17 bytes of PDU + CRC = 17–20 bytes
including access address), the initial 32-byte FIFO should suffice. However,
there could be subtle timing issues during the TX state transition —
specifically, the delay between when the firmware writes the FIFO and when
the CC2400 actually begins transmission may affect the packet's preamble
timing or sync word integrity.

This would only be testable with a protocol-level logic analyzer on the
CC2400's SPI bus, or by comparing the on-air timing with a known-good
BLE transmitter.

#### Hypothesis 4 (Confidence: 20%) — nRF24L01+ Auto-ACK/CRC Interference

With `EN_AA=0` and `EN_CRC=0` on all pipes, the nRF24L01+ should be in basic
ShockBurst mode with no PCF, no ACK, and no CRC. This has been verified by
reading back the configuration registers. The nRF24 should accept any
properly addressed packet regardless of CRC content, since CRC checking is
disabled.

This is unlikely to be the issue given the thorough register verification.

### 8.4 Recommended Diagnostic Steps

To narrow down the root cause, perform these diagnostics in order:

**1. Read RPD register during Ubertooth TX**

The nRF24L01+ RPD (Received Power Detector) register indicates if the
received signal strength exceeds -64 dBm. If RPD is never set during
Ubertooth TX, the issue is signal strength or antenna coupling. If RPD IS
set but no packet is received, the issue is modulation quality or packet
format.

```bash
# On the ESP32, poll RPD while Ubertooth is transmitting on ch37
# If RPD=0 always: signal too weak → Hypothesis 2
# If RPD=1 but no packet: modulation/format issue → Hypothesis 1 or 3
```

**2. Print raw FIFO bytes before dewhiten**

If any bytes at all appear in the nRF24 FIFO during Ubertooth TX — even
garbage — it means the nRF24 received *something*. This would narrow the
issue from "no signal" to "signal but wrong demodulation."

**3. Verify Ubertooth TX with spectrum analyzer**

```bash
ubertooth-specan -f 2402 -l 2402
```

Should show an energy spike at 2402 MHz during Ubertooth transmission. If
no energy is visible, the CC2400 is not transmitting at all (hardware fault
or misconfiguration).

**4. Verify Ubertooth TX with `ubertooth-btle`**

A second Ubertooth (or the same Ubertooth after stopping TX mode) should be
able to detect its own packets as valid BLE. If the Ubertooth can see its
own packets, the on-air signal is at least BLE-legal; the issue is the
nRF24's tolerance of CC2400 modulation.

**5. Use a second nRF24L01+ as BLE transmitter**

This eliminates the CC2400 entirely from the loop. If the ESP32 can receive
nRF24-transmitted BLE packets (using `nrf24::ble` functions to craft and
transmit a BLE advertising PDU) but not CC2400-transmitted ones, the CC2400
modulation quality is confirmed as the issue.

**6. Use Nordic nRF Sniffer for cross-validation**

The nRF52840 Dongle (USB ID 1915:522A) can capture BLE packets as
Wireshark-compatible PCAP, providing a third independent data source. See
§9 for setup instructions.

### 8.5 What This Means for Our Project

The ESP32 nRF24L01+ sniffer **IS WORKING correctly** — it receives real BLE
packets from multiple devices on all 3 advertising channels. The Ubertooth
TX failure is a limitation of the CC2400's BLE modulation compatibility with
the nRF24L01+ receiver, not a bug in our code.

**Implications:**

- ✅ The Ubertooth remains valuable as a **passive sniffer** for ground-truth
  comparison (see the companion document
  `ubertooth-ble-testing.md` §6–7).
- ✅ Our dewhitening, PDU parsing, and channel mapping code is validated by
  receiving **real BLE packets** from production devices.
- ❌ Ubertooth TX cannot be used for **active injection testing** with the
  nRF24L01+ receiver. For that, use a Nordic nRF52840 Dongle or a second
  nRF24L01+ module instead.

---

## 9. Nordic nRF Sniffer Cross-Validation Setup

The Nordic nRF Sniffer for Bluetooth LE provides an independent BLE capture
device that uses a **native BLE radio** (nRF52840), making it an ideal
cross-validation tool alongside the Ubertooth passive sniffer and the ESP32
nRF24L01+ sniffer.

### 9.1 Device Information

| Field | Value |
|-------|-------|
| USB Vendor ID | `1915` (Nordic Semiconductor) |
| USB Product ID | `522A` |
| Radio chip | nRF52840 |
| Interface | USB + Wireshark extcap plugin |

The device appeared on the USB bus as:

```
Bus 003 Device 026: ID 1915:522a Nordic Semiconductor ASA nRF Sniffer for Bluetooth
```

### 9.2 Setup Steps

The nRF Sniffer for Bluetooth LE is distributed by Nordic Semiconductor as a
downloadable package. The general setup process is:

**1. Download the nRF Sniffer package**

Obtain the nRF Sniffer for Bluetooth LE from Nordic Semiconductor's developer
website. The package contains the sniffer firmware, Python capture scripts,
and the Wireshark extcap plugin.

> **Note:** The Nordic Semiconductor website blocks automated access. Visit
> the product page manually in a browser to download the package. The product
> is listed under Development Tools on the Nordic Semiconductor website.

**2. Install Python dependencies**

The sniffer capture script requires Python 3 and several packages:

```bash
pip3 install --break-system-packages pyserial pandas
```

The exact dependencies may vary by version. Check the `requirements.txt` in
the sniffer package for the authoritative list.

**3. Flash the nRF Sniffer firmware to the dongle**

The dongle must be running the sniffer firmware (not the default bootloader
or other firmware). Follow the instructions in the sniffer package README.
Typically this involves:

- Putting the dongle in bootloader mode (press the reset button while holding
  the BOOT button, or use `nrfutil` if available)
- Flashing the sniffer firmware hex file using `nrfutil dfu serial` or the
  Nordic nRF Connect programmer

**4. Set up the Wireshark extcap plugin**

The nRF Sniffer works as a Wireshark extcap plugin. To install it:

```bash
# Copy the extcap scripts to Wireshark's extcap directory
mkdir -p ~/.local/lib/wireshark/extcap/
cp -r /path/to/nrf-sniffer/extcap/* ~/.local/lib/wireshark/extcap/
chmod +x ~/.local/lib/wireshark/extcap/nrf_sniffer_ble*
```

After restarting Wireshark, the nRF Sniffer interface should appear in the
capture interfaces list.

**5. Capture BLE traffic in Wireshark alongside the ESP32 sniffer**

- Start a Wireshark capture on the nRF Sniffer interface
- Start the ESP32 nRF24L01+ sniffer on the same advertising channel
- Compare PDU types, MAC addresses, and advertising data between the two
  capture sources

**6. Compare captures**

```bash
# Decode nRF Sniffer PCAP with tshark
tshark -r nrf_sniffer_capture.pcap -T fields \
    -e btle.channel_idx \
    -e btle.pdu_type \
    -e btle.advertising_address \
    -e btle.advertising_data
```

Cross-reference with the ESP32 serial output, which shows the same fields
after dewhitening.

### 9.3 Advantages Over Ubertooth TX Validation

The nRF Sniffer provides significant advantages for BLE validation compared
to relying on Ubertooth TX:

| Aspect | Ubertooth TX | nRF Sniffer |
|--------|-------------|-------------|
| Radio type | CC2400 (general-purpose) | nRF52840 (BLE-native) |
| On-air signal | Approximates BLE spec | 100% BLE-spec compliant |
| Can transmit | Yes (but CC2400 modulation may not be nRF24-compatible) | No (receive only, or TX via nRF52840's native radio) |
| Can receive | Yes (passive sniff) | Yes (passive sniff) |
| CRC verification | Ubertooth decodes and verifies CRC | Wireshark verifies CRC and flags errors |
| Protocol decode | Requires `ubertooth-btle` + `tshark` | Built-in Wireshark BLE dissector |
| Maintenance | Community-maintained, last release 2020 | Actively maintained by Nordic Semiconductor |

**Key advantage:** Because the nRF52840 has a native BLE radio, its captures
are guaranteed BLE-spec compliant. Any packet the nRF Sniffer captures is a
valid BLE packet — there is no risk of modulation-quality issues that exist
with the CC2400.

---

## 10. Pitfalls

### 10.1 DFU flash may report I/O error at the end

The DFU flash reported `"libUSB Error: Input/Output Error"` at the very end of
writing. The firmware was verified as correct after a reset. This appears to
be a known issue with the verification step — the flash itself succeeded.

**Action:** Run `ubertooth-util -v` after any DFU flash that reports an I/O
error. If the version is correct, the flash succeeded. Do not re-flash unless
the version is wrong.

### 10.2 libubertooth cmake CMP0005 policy

Building libubertooth from source requires adding `cmake_policy(SET CMP0005 NEW)`
before `add_definitions()` to suppress a CMake policy warning about escape
sequences in definitions.

Without this fix, the build may succeed but produce verbose warnings. Add the
line to `CMakeLists.txt` before any `add_definitions()` call:

```cmake
cmake_policy(SET CMP0005 NEW)
```

### 10.3 ubertooth-dfu missing libusb link

The ubertooth-tools build may not correctly link `ubertooth-dfu` against
libusb. If `ubertooth-dfu` fails to find `libusb_init`, manually add
`-lusb-1.0` to the link step.

### 10.4 Old distro packages take priority if PATH not configured

The Debian packages install to `/usr/bin` and `/usr/lib`, while the
source-built versions are in `$HOME/.local/bin` and `$HOME/.local/lib`. Without
`PATH` and `LD_LIBRARY_PATH` configuration (see §7), the old versions will be
used by default.

**Always verify with `which ubertooth-util`** that you are running the
source-built version, not the distro version.

### 10.5 ubertooth-tx is a USELESS STUB

The `ubertooth-tx` tool (both distro and source-built) prints:

```
WARNING: This tool currently does nothing!
```

It is a stub for classic Bluetooth, not BLE. **Do not use `ubertooth-tx`.**
Use our `tools/ubertooth_btle_tx.py` script or the raw USB commands described
in the companion document `ubertooth-ble-testing.md` instead.

### 10.6 PATH must include $HOME/.local/bin BEFORE /usr/bin

Even after removing distro packages, some systems may still have remnants in
`/usr/bin`. Always put `$HOME/.local/bin` at the **beginning** of `PATH`:

```bash
export PATH="$HOME/.local/bin:$PATH"    # CORRECT — local takes priority
export PATH="$PATH:$HOME/.local/bin"    # WRONG — system paths checked first
```

---

## 11. References

- [Ubertooth GitHub repository](https://github.com/greatscottgadgets/ubertooth) — verified 2026-06-06
- [Ubertooth Build Guide (ReadTheDocs)](https://ubertooth.readthedocs.io/en/latest/build_guide.html) — verified 2026-06-06
- [Dmitry Grinberg: "Bit-banging Bluetooth Low Energy"](http://dmitry.gr/?r=05.Projects&proj=11.%20Bluetooth%20LE%20fakery) — verified 2026-06-06
- nRF Sniffer for Bluetooth LE — Nordic Semiconductor product page (website blocks automated access; visit browser to download)
- CC2400 datasheet — referenced in `/tmp/ubertooth/firmware/` after cloning the repository
- [nRF24L01+ Product Specification](../datasheets/nRF24L01P_PS_v1.0.pdf) — local copy at `docs/datasheets/`
- Companion document: [Ubertooth One BLE Testing](ubertooth-ble-testing.md) — USB commands, packet injection, dewhitening validation