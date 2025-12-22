//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>

#include <Oxygen/Base/api_export.h>

namespace oxygen::base {

using Sha256Digest = std::array<uint8_t, 32>;

//! Incremental SHA-256 implementation.
/*!
 Provides a small incremental SHA-256 implementation for hashing memory and
 files.

 @note This API is used by higher-level modules (e.g. Content validation).
*/
class Sha256 final {
public:
  static constexpr size_t kDigestSize = 32;

  OXYGEN_BASE_API Sha256() noexcept;

  OXYGEN_BASE_API auto Update(std::span<const std::byte> data) noexcept -> void;

  OXGN_BASE_NDAPI auto Finalize() noexcept -> Sha256Digest;

private:
  auto ProcessBlock_(std::span<const std::byte, 64> block) noexcept -> void;

  uint64_t total_bits_ = 0;
  std::array<std::byte, 64> buffer_ = {};
  size_t buffer_size_ = 0;
  std::array<uint32_t, 8> state_ = {};
};

OXGN_BASE_NDAPI auto ComputeSha256(std::span<const std::byte> data) noexcept
  -> Sha256Digest;

OXGN_BASE_NDAPI auto ComputeFileSha256(const std::filesystem::path& path)
  -> Sha256Digest;

OXGN_BASE_NDAPI auto IsAllZero(const Sha256Digest& digest) noexcept -> bool;

} // namespace oxygen::base
