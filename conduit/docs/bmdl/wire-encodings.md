# Wire Encodings

[Back to index](index.md)

The `wire-encoding` attribute controls how integer bits are interpreted on the wire. Wire encoding is orthogonal to bit width -- a 16-bit BCD field occupies the same 16 bits as a 16-bit CB2 field, but the bit patterns represent different numeric values.

## Overview

| Encoding | Full Name | Description | Valid Base | Constraints |
|----------|-----------|-------------|------------|-------------|
| `bnr` | Binary Number Representation | Unsigned binary (default for `uint`) | `uint` | -- |
| `cb2` | Complement Binary 2 | Two's complement (default for `int`) | `int` | -- |
| `bnr-s` | Sign-Magnitude | MSB = sign, rest = unsigned magnitude | `int` | `bits >= 2` |
| `bcd` | Binary Coded Decimal | 4-bit nibbles encode digits 0-9 | `uint` | `bits % 4 == 0` |
| `bcd-s` | Signed BCD | MSB = sign, rest = BCD nibbles | `int` | `(bits-1) % 4 == 0`, `bits >= 5` |

## BNR (Default for `uint`)

Standard unsigned binary representation. This is the default for `uint` types and does not need to be specified explicitly.

```xml
<type name="altitude" base="uint" bits="16"/>
<!-- wire-encoding="bnr" is implied -->
```

**Encoding:** Value is stored directly as an unsigned binary number.

**Example (16-bit):**
```
Value: 1000
Wire:  0x03E8 = 0000 0011 1110 1000
```

## CB2 (Default for `int`)

Standard two's complement signed representation. This is the default for `int` types.

```xml
<type name="offset" base="int" bits="16"/>
<!-- wire-encoding="cb2" is implied -->
```

**Encoding:** Standard two's complement. MSB is the sign bit.

**Example (16-bit):**
```
Value: -100
Wire:  0xFF9C = 1111 1111 1001 1100
```

## BNR_S (Sign-Magnitude)

MSB is the sign bit (0 = positive, 1 = negative), remaining bits are the unsigned magnitude.

```xml
<type name="sign-mag-offset" base="int" bits="16" wire-encoding="bnr-s"/>
```

**Requirements:** `int` base, `bits >= 2`

**Encoding:**
```
Bit layout: [sign:1][magnitude:N-1]
Decode:     if sign == 0: value = magnitude
            if sign == 1: value = -magnitude
Encode:     if value >= 0: sign = 0, magnitude = value
            if value <  0: sign = 1, magnitude = -value
```

**Example (16-bit):**
```
Value: -100
Wire:  0x8064 = 1_000 0000 0110 0100
                 ^sign  ^magnitude=100
```

Note: BNR_S is NOT the same as two's complement. `-1` in BNR_S is `0x8001`, not `0xFFFF`.

## BCD (Binary Coded Decimal)

Each 4-bit nibble encodes one decimal digit (0-9). Used in aviation protocols for transponder codes, altitudes, etc.

```xml
<type name="bcd-altitude" base="uint" bits="16" wire-encoding="bcd"/>
```

**Requirements:** `uint` base, `bits % 4 == 0`

**Encoding:** Each nibble (4 bits) represents one decimal digit.

**Example (16-bit = 4 digits):**
```
Value: 1234
Wire:  0x1234 = 0001 0010 0011 0100
                 1    2    3    4
```

**Range:** A 16-bit BCD field can represent 0-9999 (not 0-65535).

## BCD_S (Signed BCD)

MSB is the sign bit (ARINC 429 convention), remaining bits are BCD nibbles.

```xml
<type name="signed-bcd-heading" base="int" bits="13" wire-encoding="bcd-s"/>
```

**Requirements:** `int` base, `bits >= 5`, `(bits-1) % 4 == 0`

**Encoding:**
```
Bit layout: [sign:1][BCD nibbles:(bits-1)/4 digits]
Decode:     decode BCD nibbles, negate if sign == 1
Encode:     encode abs(value) as BCD, set sign if negative
```

**Example (13-bit = 1 sign + 3 digits):**
```
Value: -123
Wire:  1_0001 0010 0011
       ^sign  ^BCD=123
```

## Naming Variants

The parser accepts both hyphenated and underscored forms:

| Canonical | Also Accepted |
|-----------|---------------|
| `bnr-s` | `bnr_s` |
| `bcd-s` | `bcd_s` |

Both forms are equivalent. The hyphenated form is canonical and used throughout this documentation.

## Specifying Wire Encoding

### On Types

```xml
<type name="bcd-altitude" base="uint" bits="16" wire-encoding="bcd"/>
<type name="sign-mag-offset" base="int" bits="16" wire-encoding="bnr-s"/>
```

### On Fields (Overrides Type)

```xml
<field name="altitude" type="uint16" wire-encoding="bcd"/>
<field name="offset" bits="16" signed="true" wire-encoding="bnr-s"/>
```

Field-level `wire-encoding` overrides type-level, allowing the same type to be used with different encodings in different contexts.

## Interaction with Scale and Offset

Wire encoding and [scaling](types.md#scaled-types) are independent:

1. **Decode:** Read raw bits using wire encoding -> apply `decoded = (raw * scale) + offset`
2. **Encode:** Compute `raw = (value - offset) / scale` -> write bits using wire encoding

```xml
<type name="bcd-altitude" base="uint" bits="16" wire-encoding="bcd">
  <scale>25</scale>
  <unit>feet</unit>
</type>
```

A wire value of BCD `0400` (decimal 400) decodes to `400 * 25 = 10000` feet.

## Defaults and No-Ops

Explicitly specifying `wire-encoding="cb2"` on `int` or `wire-encoding="bnr"` on `uint` is a no-op -- these are the defaults. The generator accepts them without error.

## Best Practices

- Explicitly specifying the default encoding (`cb2` for `int`, `bnr` for `uint`) is harmless but unnecessary.
- Wire encoding is orthogonal to `<scale>` and `<offset>`. The encoding determines how bits are interpreted on the wire, then scale/offset are applied after decode (and before encode).
- Use `bcd` for fields where the wire format encodes each digit separately (common in aviation transponder codes).
- Use `bnr-s` for protocols that use sign-magnitude rather than two's complement (some older aviation standards).

## Common Pitfalls

- BCD requires `bits % 4 == 0` (each nibble is one decimal digit). BCD_S requires `(bits-1) % 4 == 0` (MSB is sign, rest is BCD).
- BNR_S (sign-magnitude) requires `bits >= 2` and `int` base. It is NOT the same as two's complement.
- BCD fields have a smaller value range than binary fields of the same width. A 16-bit BCD field holds 0-9999, not 0-65535.
- Field-level `wire-encoding` overrides type-level. If a type has `wire-encoding="bcd"` but a field overrides it, the field's encoding wins.
- BCD nibble values above 9 (0xA-0xF) are invalid and produce a decode error.
