# C++ `enum class` and `struct` — Design Patterns for Embedded Libraries

## What was learned

When modelling hardware registers as C++ types, two constructs are essential:
`enum class` for named field options, and `struct` with methods for the register itself.

---

## `enum class` vs plain `enum`

Plain `enum` has two problems that make it unsuitable for library code:

1. **Name leakage** — enumerators pollute the enclosing scope:
   ```cpp
   enum TxPower { Minus18dBm, dBm0 };
   uint8_t x = Minus18dBm;  // works — but where did Minus18dBm come from?
   ```

2. **Silent implicit conversion to `int`**:
   ```cpp
   uint8_t reg = Minus18dBm;  // compiles silently — may truncate
   ```

`enum class` fixes both:
```cpp
enum class TxPower : uint8_t { Minus18dBm = 0b00, dBm0 = 0b11 };

nrf24::TxPower p = nrf24::TxPower::Minus18dBm;  // must qualify — name does not leak
uint8_t x = static_cast<uint8_t>(nrf24::TxPower::Minus18dBm);     // ✓ explicit cast
// uint8_t x = nrf24::TxPower::Minus18dBm;                       // ❌ compile error
```

The `: uint8_t` suffix pins the **underlying storage type** to one byte.
Without it the compiler chooses `int` (4 bytes), which wastes space and makes
`static_cast` return a wider type than expected.

---

## `struct` vs `class`

In C++ they are **identical** except for default member access:

| Keyword | Default access |
|---------|---------------|
| `struct` | `public` |
| `class`  | `private` |

Convention:
- Use **`struct`** for data containers + simple helpers (e.g. a register model).
- Use **`class`** when you need enforced encapsulation and invariants (e.g. a stateful driver).

Methods inside a `struct` are perfectly ordinary C++ — a `struct` is just a
`class` with `public`-by-default access.

---

## `const` and `static` methods

```cpp
struct RfSetup {
    uint8_t to_byte() const;          // const  — does not modify *this
    static RfSetup from_byte(uint8_t); // static — does not need an instance
};
```

This matches the actual `nrf24::RfSetup` struct in the library. See `nrf24l01plus/registers/rf_setup.h` for the full implementation.

- **`const` method**: the compiler enforces that it never modifies any member.
  Call it on a `const RfSetup` or through a `const` pointer.
- **`static` method**: belongs to the type, not to any instance.
  Used here as a named constructor / factory function.

---

## Code examples

```cpp
// Configure register fields with named values
nrf24::RfSetup cfg;
cfg.data_rate = nrf24::DataRate::Kbps250;
cfg.tx_power  = nrf24::TxPower::Minus6dBm;
nrf24_write_reg(nrf24::RfSetup::ADDRESS, cfg.to_byte());  // serialise to raw byte

// Read back and inspect
nrf24::RfSetup actual = nrf24::RfSetup::from_byte(
    nrf24_read_reg(nrf24::reg::RF_SETUP));
// actual.data_rate == nrf24::DataRate::Kbps250  (if device accepted it)

// Or use the typed overload (deduces address from struct type):
radio.write_reg(cfg);  // deduces nrf24::RfSetup::ADDRESS

// Round-trip test
nrf24::RfSetup a;
a.data_rate = nrf24::DataRate::Kbps250;
nrf24::RfSetup b = nrf24::RfSetup::from_byte(a.to_byte());
assert(b.data_rate == nrf24::DataRate::Kbps250);
```

---

## Pitfalls

- **Non-adjacent bits**: the nRF24L01+ data rate is split across bit 5 (`RF_DR_LOW`)
  and bit 3 (`RF_DR_HIGH`). The enum internally encodes both as a single 2-bit
  value, and `to_byte()` / `from_byte()` handle the split. This is easy to get
  wrong — always verify with a known raw byte.

- **Do not put learning notes in library headers**. Headers become the public API.
  Only Doxygen `/** */` blocks belong there. Rationale and design decisions
  belong in `docs/learning/`.

- **Reserved enum values**: `NrfDataRate` has no enumerator for `0b11`
  (RF_DR_LOW=1, RF_DR_HIGH=1) because the datasheet marks it reserved.
  `from_byte()` will return a garbage `data_rate` if the device returns that
  combination — add a validity check if reading from untrusted hardware.

---

## References

- [nRF24L01+ Product Specification v1.0, §6.3 — RF_SETUP register](https://www.nordicsemi.com/products/nrf24l01)
- [cppreference — enum declaration](https://en.cppreference.com/w/cpp/language/enum)
- [cppreference — class declaration](https://en.cppreference.com/w/cpp/language/class)
