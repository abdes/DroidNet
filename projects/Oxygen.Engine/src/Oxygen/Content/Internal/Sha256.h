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

namespace oxygen::content::internal {

using Sha256Digest = std::array<uint8_t, 32>;

//! Incremental SHA-256 implementation used for mount-time validation.
class Sha256 final {
public:
  static constexpr size_t kDigestSize = 32;

  Sha256() noexcept;

  auto Update(std::span<const std::byte> data) noexcept -> void;

  [[nodiscard]] auto Finalize() noexcept -> Sha256Digest;

private:
  auto ProcessBlock_(std::span<const std::byte, 64> block) noexcept -> void;

  uint64_t total_bits_ = 0;
  std::array<std::byte, 64> buffer_ = {};
  size_t buffer_size_ = 0;
  std::array<uint32_t, 8> state_ = {};
};

[[nodiscard]] auto ComputeSha256(std::span<const std::byte> data) noexcept
  -> Sha256Digest;

[[nodiscard]] auto ComputeFileSha256(const std::filesystem::path& path)
  -> Sha256Digest;

[[nodiscard]] auto IsAllZero(const Sha256Digest& digest) noexcept -> bool;

} // namespace oxygen::content::internal
