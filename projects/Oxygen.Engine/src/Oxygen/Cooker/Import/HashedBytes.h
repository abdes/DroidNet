//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <span>
#include <utility>
#include <vector>

#include <Oxygen/Base/Sha256.h>

namespace oxygen::content::import {

//! Cooked bytes and their SHA-256, computed from them when constructed.
/*!
 The bytes cannot change afterwards, so the digest always describes them: an
 emitter records it in the resource row and deduplicates by it without
 hashing again. Producers construct it where the hashing cost belongs, on a
 worker thread. A default-constructed value holds no bytes and no digest.
*/
class HashedBytes final {
public:
  HashedBytes() = default;

  explicit HashedBytes(std::vector<std::byte> bytes)
    : bytes_(std::move(bytes))
    , sha256_(base::ComputeSha256(bytes_))
  {
  }

  [[nodiscard]] auto Bytes() const noexcept -> std::span<const std::byte>
  {
    return bytes_;
  }

  [[nodiscard]] auto Size() const noexcept -> size_t { return bytes_.size(); }

  [[nodiscard]] auto Empty() const noexcept -> bool { return bytes_.empty(); }

  //! The SHA-256 of `Bytes()`; all zero for a default-constructed value.
  [[nodiscard]] auto Sha256() const noexcept -> const base::Sha256Digest&
  {
    return sha256_;
  }

  //! Gives the bytes up, to the write that stores them.
  [[nodiscard]] auto TakeBytes() && noexcept -> std::vector<std::byte>
  {
    sha256_ = {};
    return std::move(bytes_);
  }

private:
  std::vector<std::byte> bytes_;
  base::Sha256Digest sha256_ {};
};

} // namespace oxygen::content::import
