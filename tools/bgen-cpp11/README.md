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
- Expands `std::is_same_v<>`, `std::decay_t<>`, `std::underlying_type_t<>`,
  `std::enable_if_t<>`, etc. to their C++11 trait forms.
- Rewrites `operator==(...) const = default` into explicit member-wise
  comparisons (scoped to the class's `private:` section).
- Rewrites every generic-lambda `std::visit` pattern bgen emits
  (encode, to_string, to_string(overrides), session-decode, to_json
  field dispatch) to call templated helpers in `bgen11::detail`.
- Rewrites C++17 structured bindings in range-for to C++11 form.
- Replaces `static constexpr std::string_view` with `static constexpr const
  char[]`.
- Strips `inline` from `inline constexpr` (C++17) variables.
- Replaces `static constexpr std::array<...>` with runtime-initialized
  function-local statics.
- Rewrites C++17 nested namespaces (`namespace a::b { ... }`) to C++11
  nested form.
- Rewrites the single `if constexpr (requires { ... })` shape bgen emits
  in `json.hpp` (fixed-size-array vs vector dispatch) to an
  SFINAE-backed `bgen11::detail::assign_fixed_or_move(...)` helper.
- Generates a stub `protocol.hpp` that preserves the namespace but drops
  the non-literal `ProtocolDescriptor`.
- Idempotent: writes a `// bgen-cpp11: translated` sentinel as the first
  line of each output, and short-circuits to a plain copy on second
  passes.

## json.hpp

The converter now handles the C++17/20 constructs bgen emits in
`json.hpp`. That file does **not** depend on anything beyond
nlohmann/json and cpp11-compat; nlohmann/json itself requires C++11 as a
minimum, so end-to-end translation is valid. The adaptor does not ship
a `json.hpp` because bgen currently emits broken `from_json` code that
references `mutable_cat()` / `mutable_len()` accessors that do not exist
on frame records; that is an upstream bgen bug and outside the
converter's scope.

## Output dependencies

The translated code depends on:
- [`cpp11-compat`](../../cpp11-compat) — `cpp11::` and `bgen11::` runtime.
- A C++11 (or later) compiler.

No other runtime deps.
