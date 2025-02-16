//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <concepts>

#include <Oxygen/Base/Result.h>

namespace oxygen::serio {

//! Concept to specify a stream that can be written to and read from.
template <typename T>
concept Stream = requires(T t, char* data, size_t size) {
    { t.write(data, size) } -> std::same_as<Result<void>>;
    { t.read(data, size) } -> std::same_as<Result<void>>;
    { t.flush() } -> std::same_as<Result<void>>;
    { t.position() } -> std::same_as<Result<size_t>>;
    { t.seek(size) } -> std::same_as<Result<void>>;
    { t.size() } -> std::same_as<Result<size_t>>;
};

namespace limits {
    using SequenceSizeType = uint32_t;
    constexpr SequenceSizeType kMaxStringLength = 1024ULL * 1024; // 1MB
    constexpr SequenceSizeType kMaxArrayLength = 1024ULL * 1024; // 1MB
}

} // namespace oxygen::serio
