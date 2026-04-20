# bgen-cpp11

Python tool that translates bgen-generated C++23 code into strict C++11.

## Usage

```sh
python3 bgen_to_cpp11.py --input <bgen-output-dir> --output <c++11-output-dir>
```

Or use the shell/batch wrappers:

```sh
./bgen_to_cpp11.sh  --input ./out-cpp23 --output ./out-cpp11
.\bgen_to_cpp11.bat --input .\out-cpp23 --output .\out-cpp11
```

## What it does

- Replaces `std::span`, `std::optional`, `std::variant`, `std::string_view`,
  `std::any`, `std::visit` with `cpp11::` equivalents from
  [`cpp11-compat/compat11/`](../../cpp11-compat).
- Maps `conduit::Result`, `conduit::Error`, `conduit::io::BitReader`,
  `conduit::traits::ISession`, etc. onto `bgen11::` drop-ins from
  [`cpp11-compat/bgen11/`](../../cpp11-compat).
- Strips `[[nodiscard]]` / `[[maybe_unused]]` attributes.
- Expands `std::is_same_v<>`, `std::decay_t<>`, etc. to their C++11 forms.
- Rewrites `operator==(...) const = default` into explicit member-wise
  comparisons.
- Rewrites the three generic-lambda `std::visit` patterns bgen emits
  (encode, to_string, session-decode) to call bgen11 detail helpers.
- Rewrites C++17 structured bindings in range-for to C++11 form.
- Replaces `static constexpr std::string_view` with `static constexpr const
  char[]`.
- Replaces `static constexpr std::array<...>` with runtime-initialized
  function-local statics.
- Generates a stub `protocol.hpp` that preserves the namespace but drops
  the non-literal `ProtocolDescriptor`.

## Output dependencies

The translated code depends on:
- [`cpp11-compat`](../../cpp11-compat) — `cpp11::` and `bgen11::` runtime.
- A C++11 (or later) compiler.

No other runtime deps.
