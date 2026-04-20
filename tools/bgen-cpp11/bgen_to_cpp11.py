#!/usr/bin/env python3
"""
bgen_to_cpp11.py - Convert bgen-generated C++23 sources to strict C++11.

Usage: bgen_to_cpp11.py --input <dir> --output <dir>

The converter applies text-level transformations keyed on the mechanical
patterns bgen emits. Input files must have been produced by the bgen
code generator. Output is valid strict C++11 that depends on the
cpp11-compat headers (compat11/ and bgen11/).
"""

from __future__ import print_function
import argparse
import os
import re
import sys
import shutil


# ---------------------------------------------------------------------------
# Transformation rules
# ---------------------------------------------------------------------------

CONDUIT_INCLUDE_MAP = {
    r'conduit/core/error.hpp':          'bgen11/error.hpp',
    r'conduit/io/bit_reader.hpp':       'bgen11/bit_reader.hpp',
    r'conduit/io/bit_writer.hpp':       'bgen11/bit_writer.hpp',
    r'conduit/io/endian.hpp':           'bgen11/endian.hpp',
    r'conduit/traits/session_traits.hpp':'bgen11/session_traits.hpp',
    r'conduit/traits/codec_traits.hpp': 'bgen11/session_traits.hpp',
    r'conduit/string/encoding.hpp':     'bgen11/string_encoding.hpp',
    r'conduit/logging/logger.hpp':      'bgen11/logging.hpp',
}

STDLIB_POLYFILLED = {'span', 'optional', 'variant', 'string_view', 'any', 'expected'}

# Namespace / symbol substitutions (order matters: more-specific first).
NS_SUBS = [
    # conduit::io::Endian scoped enum -> bgen11 unscoped
    (re.compile(r'\bconduit::io::Endian::Big\b'),    r'::bgen11::io::Endian_Big'),
    (re.compile(r'\bconduit::io::Endian::Little\b'), r'::bgen11::io::Endian_Little'),
    (re.compile(r'\bconduit::io::Endian\b'),         r'int'),  # parameter-type context
    # conduit::ErrorCode scoped enum -> bgen11 unscoped
    (re.compile(r'\bconduit::ErrorCode::([A-Za-z_][A-Za-z_0-9]*)'),
                                                      r'::bgen11::ErrorCode_\1'),
    (re.compile(r'\bconduit::ErrorCode\b'),          r'int'),
    # conduit::traits -> bgen11::traits
    (re.compile(r'\bconduit::traits::'),             r'::bgen11::traits::'),
    # conduit::io -> bgen11::io
    (re.compile(r'\bconduit::io::'),                 r'::bgen11::io::'),
    # conduit:: top-level types
    (re.compile(r'\bconduit::VoidResult\b'),         r'::bgen11::VoidResult'),
    (re.compile(r'\bconduit::Result\b'),             r'::bgen11::Result'),
    (re.compile(r'\bconduit::Error\b'),              r'::bgen11::Error'),
    # conduit macros
    (re.compile(r'\bCONDUIT_TRY_ASSIGN\b'),          r'BGEN11_TRY_ASSIGN'),
    (re.compile(r'\bCONDUIT_TRY_CONTEXT\b'),         r'BGEN11_TRY_CONTEXT'),
    (re.compile(r'\bCONDUIT_TRY\b'),                 r'BGEN11_TRY'),
    (re.compile(r'\bCONDUIT_ERROR\b'),               r'BGEN11_ERROR'),
    (re.compile(r'\bCONDUIT_ENSURE\b'),              r'BGEN11_ENSURE'),
    # std:: types polyfilled as cpp11::
    (re.compile(r'\bstd::span\b'),                   r'::cpp11::span'),
    (re.compile(r'\bstd::optional\b'),               r'::cpp11::optional'),
    (re.compile(r'\bstd::variant\b'),                r'::cpp11::variant'),
    (re.compile(r'\bstd::visit\b'),                  r'::cpp11::visit'),
    (re.compile(r'\bstd::holds_alternative\b'),      r'::cpp11::holds_alternative'),
    (re.compile(r'\bstd::get_if\b'),                 r'::cpp11::get_if'),
    (re.compile(r'\bstd::string_view\b'),            r'::cpp11::string_view'),
    (re.compile(r'\bstd::any_cast\b'),               r'::cpp11::any_cast'),
    (re.compile(r'\bstd::any\b'),                    r'::cpp11::any'),
    (re.compile(r'\bstd::nullopt\b'),                r'::cpp11::nullopt'),
    (re.compile(r'\bstd::unexpected\b'),             r'::cpp11::make_unexpected'),
    (re.compile(r'\bstd::make_unique\b'),            r'::cpp11::make_unique'),
    # std:: type traits _v / _t
    (re.compile(r'\bstd::is_same_v\b'),              r'std::is_same'),
    (re.compile(r'\bstd::decay_t\b'),                r'typename std::decay'),
    (re.compile(r'\bstd::remove_cv_t\b'),            r'typename std::remove_cv'),
    (re.compile(r'\bstd::remove_reference_t\b'),     r'typename std::remove_reference'),
    (re.compile(r'\bstd::remove_const_t\b'),         r'typename std::remove_const'),
    (re.compile(r'\bstd::enable_if_t\b'),            r'typename std::enable_if'),
    (re.compile(r'\bstd::conditional_t\b'),          r'typename std::conditional'),
    (re.compile(r'\bstd::underlying_type_t\b'),      r'typename std::underlying_type'),
]

# Trait templates that need ::value suffix when converting from _v form.
# Converted e.g. "std::is_same_v<A, B>" -> "std::is_same<A, B>" above; this
# second pass adds ::value to the trailing closer.
TRAIT_VALUE_FIXUP = [
    (re.compile(r'\bstd::is_same<([^<>]*(?:<[^<>]*>[^<>]*)*)>'),
     r'std::is_same<\1>::value'),
]

# Trait templates that need ::type suffix when converting from _t form.
TRAIT_TYPE_FIXUP = [
    (re.compile(r'typename std::decay<([^<>]*(?:<[^<>]*>[^<>]*)*)>'),
     r'typename std::decay<\1>::type'),
    (re.compile(r'typename std::remove_cv<([^<>]*(?:<[^<>]*>[^<>]*)*)>'),
     r'typename std::remove_cv<\1>::type'),
    (re.compile(r'typename std::remove_reference<([^<>]*(?:<[^<>]*>[^<>]*)*)>'),
     r'typename std::remove_reference<\1>::type'),
    (re.compile(r'typename std::remove_const<([^<>]*(?:<[^<>]*>[^<>]*)*)>'),
     r'typename std::remove_const<\1>::type'),
    (re.compile(r'typename std::enable_if<([^<>]*(?:<[^<>]*>[^<>]*)*)>'),
     r'typename std::enable_if<\1>::type'),
    (re.compile(r'typename std::conditional<([^<>]*(?:<[^<>]*>[^<>]*)*)>'),
     r'typename std::conditional<\1>::type'),
    (re.compile(r'typename std::underlying_type<([^<>]*(?:<[^<>]*>[^<>]*)*)>'),
     r'typename std::underlying_type<\1>::type'),
]

# Attribute strip / alias.
ATTR_SUBS = [
    (re.compile(r'\[\[nodiscard\]\]\s*'),            r''),
    (re.compile(r'\[\[maybe_unused\]\]\s*'),         r''),
    (re.compile(r'\[\[likely\]\]\s*'),               r''),
    (re.compile(r'\[\[unlikely\]\]\s*'),             r''),
    (re.compile(r'\[\[fallthrough\]\]\s*;'),         r''),
]

# noexcept -> throw() fallback (optional; C++11 has noexcept so skip).
# if constexpr -> if (older compilers may accept; in strict C++11, rewrite).
# Since our generator emits no if-constexpr into output (verified by grep),
# leave this out.

# static inline constexpr std::string_view  ->  static constexpr const char[]
STATIC_STRVIEW_RE = re.compile(
    r'static\s+(?:inline\s+)?constexpr\s+::?cpp11::string_view\s+(\w+)\s*=\s*("[^"]*")\s*;'
)

# static constexpr std::array<TypeInfo, N>  ->  retain but drop constexpr for complex init
# (We currently leave these; generated ProtocolDescriptor is optional.)

# bool operator==(const T&) const = default;
# -> generate an explicit member-wise equality body by scanning the class fields.
DEFAULT_EQ_RE = re.compile(
    r'bool\s+operator==\s*\(\s*const\s+(\w+)\s*&\s*\)\s*const\s*=\s*default\s*;'
)


def rewrite_includes(text):
    """Rewrite conduit includes and strip polyfilled stdlib includes."""
    # Replace conduit/... includes.
    for src, dst in CONDUIT_INCLUDE_MAP.items():
        text = re.sub(
            r'#include\s*<' + re.escape(src) + r'>',
            '#include "' + dst + '"',
            text)
    # Strip std includes that the polyfill supplies.
    for hdr in STDLIB_POLYFILLED:
        text = re.sub(
            r'#include\s*<' + hdr + r'>\s*\n',
            '',
            text)
    return text


def prepend_polyfill_includes(text):
    """Insert polyfill umbrella includes right after the #pragma once."""
    marker = '#pragma once'
    inject = ('\n#include "compat11/compat11.hpp"'
              '\n#include "bgen11/bgen11.hpp"\n')
    if marker in text:
        return text.replace(marker, marker + inject, 1)
    return inject + text


def rewrite_default_equality(text):
    """Replace `operator== ... = default;` with an explicit member-wise body.

    Scans the enclosing class to find data member declarations.
    """
    def replace_one(match):
        cls = match.group(1)
        members = find_class_members(text, match.start(), cls)
        if not members:
            return 'bool operator==(const ' + cls + '& other) const { (void)other; return true; }'
        expr = ' && '.join('{0} == other.{0}'.format(m) for m in members)
        return ('bool operator==(const ' + cls + '& other) const { return '
                + expr + '; }')
    return DEFAULT_EQ_RE.sub(replace_one, text)


MEMBER_DECL_RE = re.compile(
    r'^\s*(?!static\s+)(?!typedef\s+)(?!using\s+)'
    r'[A-Za-z_][\w:<>, \t\*&]*?'
    r'\b(\w+)_\s*(?:=\s*[^;{]+)?\s*\{?[^{};]*\};?\s*$',
    re.MULTILINE)


def find_class_members(text, op_pos, cls_name):
    """Find the data members of the class that contains the operator== at
    `op_pos`.  Only scans the `private:` section (bgen always places data
    members there), so local variables in member-function bodies are
    ignored.

    Mechanical rules (matching bgen's emit conventions):
    - Start at the class-opening brace.
    - Find the top-level `private:` label (depth == 1).
    - Collect `Type  name_;` style declarations up to the matching
      closing brace, skipping nested block scopes.
    """
    class_pat = re.compile(
        r'(?:class|struct)\s+' + re.escape(cls_name) + r'\b[^{]*\{'
    )
    start_match = None
    for m in class_pat.finditer(text, 0, op_pos):
        start_match = m
    if not start_match:
        return []

    # Find matching close brace of the class body.
    depth = 0
    i = start_match.end() - 1
    class_end = -1
    while i < len(text):
        c = text[i]
        if c == '{':
            depth += 1
        elif c == '}':
            depth -= 1
            if depth == 0:
                class_end = i
                break
        i += 1
    if class_end < 0:
        return []

    body_start = start_match.end()
    body = text[body_start:class_end]

    # Find the `private:` label at the class body's top level (depth 0
    # relative to the body).  Ignore labels in nested scopes.
    private_idx = -1
    bd = 0
    i = 0
    while i < len(body):
        c = body[i]
        if c == '{':
            bd += 1
        elif c == '}':
            bd -= 1
        elif bd == 0 and body[i:i+8] == 'private:':
            private_idx = i + 8
            break
        i += 1
    if private_idx < 0:
        # No private section: scan the whole class body at depth 0.
        private_idx = 0

    # Walk the private region line-by-line, but skip any nested braces
    # (shouldn't normally occur for data members).
    members = []
    remainder = body[private_idx:]
    bd2 = 0
    line_buf = []
    def try_match_line(line):
        s = line.strip()
        if not s or s.startswith('//') or s.startswith('#'):
            return None
        if s.endswith(':'):  # another access label
            return None
        if s.startswith('static ') or s.startswith('typedef ') or s.startswith('using '):
            return None
        if 'friend ' in s or 'operator' in s or '(' in s.split('=')[0].split('{')[0]:
            # Looks like a function declaration, not a member variable.
            return None
        mm = re.match(
            r'^[A-Za-z_][\w:<>, \t\*&]*?\b(\w+_?)\s*'
            r'(?:=\s*[^;{]+|\{[^}]*\})?\s*;\s*(?://.*)?$',
            s)
        if mm:
            return mm.group(1)
        return None

    # Iterate characters; emit logical lines at `;` when brace depth is 0.
    i = 0
    line_start = 0
    while i < len(remainder):
        c = remainder[i]
        if c == '{':
            bd2 += 1
        elif c == '}':
            bd2 -= 1
        elif c == ';' and bd2 == 0:
            line = remainder[line_start:i + 1]
            name = try_match_line(line)
            if name:
                members.append(name)
            line_start = i + 1
        i += 1
    return members


def rewrite_inline_constexpr(text):
    """Strip `inline` from `inline constexpr` variable declarations.
    C++11 does not support inline variables; the `inline` specifier is
    only valid on functions. For integral/literal constexpr members
    defined at namespace scope, `constexpr` by itself is the C++11 form
    (internal linkage, so no ODR issue for single-TU headers).
    """
    # Matches `inline constexpr TYPE NAME = value;` at start of a line.
    return re.sub(
        r'(^|\n)([ \t]*)inline\s+constexpr\s+',
        r'\1\2constexpr ',
        text)


def rewrite_constexpr_array(text):
    """Convert `static constexpr std::array<T, N> NAME = {{...}};` at class
    scope (non-literal T) into a function returning a static local array.

    Specifically for bgen's ProtocolDescriptor which contains TypeInfo
    members that use `cpp11::string_view` (non-literal in C++11).
    """
    pat = re.compile(
        r'static\s+constexpr\s+(std::array\s*<[^;]+?>)\s+(\w+)\s*=\s*\{\{',
        re.DOTALL
    )
    def replace_array(m):
        arr_type = m.group(1).strip()
        name = m.group(2)
        return ('static const ' + arr_type + '& ' + name + '() {\n'
                '        static const ' + arr_type + ' _v = {{')
    text = pat.sub(replace_array, text)
    # Close the trailing `}};` -> `}}; return _v; }`.
    # We do this by finding the pattern our rewrite introduced and
    # replacing the trailing `}};` on the same nested block.
    out = []
    i = 0
    needle = 'static const std::array'
    while True:
        idx = text.find(needle, i)
        if idx < 0:
            out.append(text[i:])
            break
        # Only rewrite if followed by `... & name() {` (our own rewrite
        # signature). Otherwise ignore.
        # Search forward for the `{{` and match its end.
        open2 = text.find('{{', idx)
        if open2 < 0:
            out.append(text[i:])
            break
        # Find corresponding `}};`.
        close2 = text.find('}};', open2)
        if close2 < 0:
            out.append(text[i:])
            break
        out.append(text[i:close2 + 3])
        out.append(' return _v; }')
        i = close2 + 3
    text = ''.join(out)

    # Drop `constexpr` specifier on static class-scope methods that return
    # pointers via loops (bgen's `find` helpers).
    pat2 = re.compile(r'static\s+constexpr\s+(const\s+[\w:]+\s*\*\s+find)')
    text = pat2.sub(r'static \1', text)
    return text


def rewrite_static_stringview_constants(text):
    """static constexpr std::string_view NAME = "...";
    -> static constexpr const char NAME[] = "...";
    (convertible to cpp11::string_view at point of use)."""
    # Apply after std::string_view -> ::cpp11::string_view has happened.
    pat = re.compile(
        r'static\s+(?:inline\s+)?constexpr\s+::?cpp11::string_view\s+(\w+)\s*=\s*("[^"]*")\s*;'
    )
    return pat.sub(r'static constexpr const char \1[] = \2;', text)


def apply_ns_subs(text):
    for pat, repl in NS_SUBS:
        text = pat.sub(repl, text)
    for pat, repl in TRAIT_VALUE_FIXUP:
        text = pat.sub(repl, text)
    for pat, repl in TRAIT_TYPE_FIXUP:
        text = pat.sub(repl, text)
    return text


def apply_attr_subs(text):
    for pat, repl in ATTR_SUBS:
        text = pat.sub(repl, text)
    return text


STRUCT_BIND_RE = re.compile(
    r'for\s*\(\s*(const\s+)?(auto)(\s*&)?\s*\[\s*(\w+)\s*,\s*(\w+)\s*\]\s*:\s*([^)]+)\)\s*'
)


def rewrite_structured_bindings(text):
    """Rewrite C++17 structured binding in range-for over pair-like items.

    Generated code uses:
        for (const auto& [k, v] : overrides) BODY;
    Rewritten to:
        for (const auto& _sb_pair : overrides) {
            const auto& k = _sb_pair.first;
            const auto& v = _sb_pair.second;
            BODY;
        }

    Only the single-statement and braced-statement forms bgen emits are
    supported. Single-statement form needs a surrounding block.
    """
    counter = [0]

    def replace_one(m):
        counter[0] += 1
        idx = counter[0]
        is_const = bool(m.group(1))
        is_ref = bool(m.group(3))
        k = m.group(4)
        v = m.group(5)
        rng = m.group(6).strip()
        qual = 'const ' if is_const else ''
        ref = '&' if is_ref else ''
        return (
            'for ({qual}auto{ref} _sb_pair_{idx} : {rng}) {{ '
            '{qual}auto{ref} {k} = _sb_pair_{idx}.first; '
            '{qual}auto{ref} {v} = _sb_pair_{idx}.second; '
            ).format(qual=qual, ref=ref, idx=idx, k=k, v=v, rng=rng)

    out_lines = []
    for line in text.splitlines(True):
        m = STRUCT_BIND_RE.search(line)
        if not m:
            out_lines.append(line)
            continue
        # Replace the "for (...)" header with a header that opens a block
        # and prepends key/value bindings. Then append a closing brace to
        # the end of the single-statement body on the same line.
        new_header = STRUCT_BIND_RE.sub(replace_one, line, count=1)
        # If the line already contains a '{' after the for-header, keep as
        # a braced block (we added our own '{'), else append '}' at end.
        rest = line[m.end():]
        stripped_rest = rest.lstrip()
        if stripped_rest.startswith('{'):
            # Braced block — drop our extra '{' by turning the opener
            # "for (...) {" into "for (...) { ... ; {" then replace
            # the trailing '{' in stripped_rest. Simpler: keep one block.
            new_header = new_header.rstrip() + ' '
            out_lines.append(new_header + stripped_rest)
        else:
            # Single-statement form: append '}' at end of line.
            body = rest.rstrip()
            if body.endswith(';'):
                out_lines.append(new_header + body + ' }\n')
            else:
                out_lines.append(new_header + body + ' }\n')
    return ''.join(out_lines)


def _find_matching_paren(text, open_idx):
    """Given the index of an '(', return the index of the matching ')'."""
    depth = 0
    i = open_idx
    while i < len(text):
        c = text[i]
        if c == '(':
            depth += 1
        elif c == ')':
            depth -= 1
            if depth == 0:
                return i
        i += 1
    return -1


def _find_matching_brace(text, open_idx):
    depth = 0
    i = open_idx
    while i < len(text):
        c = text[i]
        if c == '{':
            depth += 1
        elif c == '}':
            depth -= 1
            if depth == 0:
                return i
        i += 1
    return -1


def rewrite_generic_lambda_visits(text):
    """Rewrite every `::cpp11::visit([...](const auto& x) { body }, EXPR)`
    to one of the bgen11 detail helpers based on the body pattern.
    """
    out = []
    i = 0
    needle = '::cpp11::visit('
    while True:
        idx = text.find(needle, i)
        if idx < 0:
            out.append(text[i:])
            break
        out.append(text[i:idx])
        open_paren = idx + len(needle) - 1  # position of '('
        close_paren = _find_matching_paren(text, open_paren)
        if close_paren < 0:
            out.append(text[idx:])
            break
        inner = text[open_paren + 1:close_paren]
        # Parse lambda head
        m = re.match(
            r'\s*\[([^\]]*)\]\s*\(\s*const\s+auto\s*&\s*(\w+)\s*\)'
            r'(\s*->\s*[^\{]+)?\s*\{',
            inner)
        if not m:
            # Not a generic lambda — keep as-is
            out.append(text[idx:close_paren + 1])
            i = close_paren + 1
            continue
        capture = m.group(1).strip()
        var = m.group(2)
        brace_start = open_paren + 1 + m.end() - 1  # absolute index of '{'
        brace_end = _find_matching_brace(text, brace_start)
        if brace_end < 0:
            out.append(text[idx:close_paren + 1])
            i = close_paren + 1
            continue
        body = text[brace_start + 1:brace_end].strip()
        # Skip comma and whitespace after lambda
        j = brace_end + 1
        while j < close_paren and text[j] in ' \t\n,':
            j += 1
        variant_expr = text[j:close_paren].strip()

        # Identify helper to use based on body content.
        if re.match(r'return\s+' + re.escape(var) + r'\.encode\s*\(\s*\w+\s*\)\s*;\s*$', body):
            # Pattern 1: variant_encode
            cap_m = re.match(r'^&?\s*(\w+)$', capture)
            if cap_m:
                writer = cap_m.group(1)
                repl = '::bgen11::detail::variant_encode({0}, {1})'.format(variant_expr, writer)
                out.append(repl)
                i = close_paren + 1
                continue
        if re.match(r'return\s+' + re.escape(var) + r'\.to_string\s*\(\s*\)\s*;\s*$', body):
            # Pattern 2: variant_to_string
            if capture == '':
                repl = '::bgen11::detail::variant_to_string({0})'.format(variant_expr)
                out.append(repl)
                i = close_paren + 1
                continue
        if '::bgen11::traits::DecodedMessage' in body and 'messages' in body:
            # Pattern 3: session decode dispatch
            repl = '::bgen11::detail::variant_decode_append({0}, messages, raw_copy)'.format(variant_expr)
            out.append(repl)
            i = close_paren + 1
            continue
        # Unrecognized generic-lambda visit — emit as-is (will fail compile;
        # user gets a clear message rather than silently wrong code).
        out.append(text[idx:close_paren + 1])
        i = close_paren + 1
    return ''.join(out)


def strip_logger_macros(text):
    """Conduit's LOG_* macros have no C++11 equivalent defined here.
    Replace with no-ops so generated code still compiles.
    """
    # LOG_WARNF("fmt", args) -> (void)0;  etc.
    pat = re.compile(r'\bLOG_(?:INFOF|WARNF|ERRORF|DEBUGF|TRACEF|INFO|WARN|ERROR|DEBUG|TRACE)\s*\([^;]*\)\s*;',
                     re.DOTALL)
    return pat.sub('(void)0;', text)


def generate_protocol_stub(original):
    """Produce a minimal C++11 stub preserving only the namespace and
    session-factory forwarder. All constexpr metadata is dropped.
    """
    ns_match = re.search(r'namespace\s+(\w+)\s*\{', original)
    ns = ns_match.group(1) if ns_match else 'generated'
    return (
        '// Generated by bgen -- translated to C++11 by bgen-cpp11.\n'
        '// Original ProtocolDescriptor elided (constexpr types non-literal in C++11).\n'
        '#pragma once\n'
        '\n'
        '#include "compat11/compat11.hpp"\n'
        '#include "bgen11/bgen11.hpp"\n'
        '#include "sessions.hpp"\n'
        '\n'
        'namespace ' + ns + ' {\n'
        '\n'
        'struct ProtocolDescriptor {\n'
        '    struct TypeInfo {\n'
        '        uint64_t type_id;\n'
        '        const char* type_name;\n'
        '    };\n'
        '};\n'
        '\n'
        '}  // namespace ' + ns + '\n'
    )


def transform_file(src, dst):
    with open(src, 'r', encoding='utf-8') as f:
        text = f.read()

    # protocol.hpp uses C++17 constexpr types that are fundamentally
    # incompatible with strict C++11 (literal-type arrays of non-literal
    # members).  The adaptor never uses ProtocolDescriptor at runtime,
    # so replace this file with a minimal stub that only exposes the
    # session factory.
    if os.path.basename(src) == 'protocol.hpp':
        text = generate_protocol_stub(text)
        dst_dir = os.path.dirname(dst)
        if dst_dir and not os.path.isdir(dst_dir):
            os.makedirs(dst_dir)
        with open(dst, 'w', encoding='utf-8') as f:
            f.write(text)
        return

    text = rewrite_includes(text)
    text = prepend_polyfill_includes(text)
    text = apply_ns_subs(text)
    text = rewrite_inline_constexpr(text)
    text = rewrite_constexpr_array(text)
    text = rewrite_static_stringview_constants(text)
    text = rewrite_default_equality(text)
    text = apply_attr_subs(text)
    text = rewrite_structured_bindings(text)
    text = rewrite_generic_lambda_visits(text)
    text = strip_logger_macros(text)

    dst_dir = os.path.dirname(dst)
    if dst_dir and not os.path.isdir(dst_dir):
        os.makedirs(dst_dir)
    with open(dst, 'w', encoding='utf-8') as f:
        f.write(text)


def transform_tree(input_dir, output_dir):
    if not os.path.isdir(input_dir):
        print('error: input dir not found: ' + input_dir, file=sys.stderr)
        sys.exit(1)
    if not os.path.isdir(output_dir):
        os.makedirs(output_dir)
    count = 0
    for root, dirs, files in os.walk(input_dir):
        rel = os.path.relpath(root, input_dir)
        out_root = output_dir if rel == '.' else os.path.join(output_dir, rel)
        for name in files:
            src = os.path.join(root, name)
            dst = os.path.join(out_root, name)
            if name.endswith(('.hpp', '.cpp', '.h', '.cc', '.cxx')):
                transform_file(src, dst)
                count += 1
            else:
                if not os.path.isdir(out_root):
                    os.makedirs(out_root)
                shutil.copy2(src, dst)
    print('bgen_to_cpp11: translated {0} file(s) from {1} to {2}'
          .format(count, input_dir, output_dir))


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--input', required=True, help='bgen output directory')
    p.add_argument('--output', required=True, help='C++11 output directory')
    args = p.parse_args(argv)
    transform_tree(args.input, args.output)
    return 0


if __name__ == '__main__':
    sys.exit(main())
