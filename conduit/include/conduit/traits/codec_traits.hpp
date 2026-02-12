// SPDX-License-Identifier: MIT
// Conduit - Codec Traits (Concepts for Generated Types)

#pragma once

#include <conduit/core/error.hpp>
#include <conduit/io/bit_reader.hpp>
#include <conduit/io/bit_writer.hpp>
#include <concepts>

namespace conduit::traits {

// ============================================================================
// Concepts for generated types
// ============================================================================

template<typename T>
concept Encodable = requires(const T& t, io::BitWriter& w) {
    { t.encode(w) } -> std::same_as<VoidResult>;
};

template<typename T>
concept Decodable = requires(io::BitReader& r) {
    { T::decode(r) } -> std::same_as<Result<T>>;
};

template<typename T>
concept Message = Encodable<T> && Decodable<T> && requires {
    { T::TYPE_ID } -> std::convertible_to<uint64_t>;
    { T::TYPE_NAME } -> std::convertible_to<std::string_view>;
};

} // namespace conduit::traits
