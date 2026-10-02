//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <stdexcept>
#include <string>
#include <utility>

#include <Oxygen/Base/Macros.h>

namespace oxygen::content::import {

//! One registry participation. Explicit retirement follows the async write
//! drain. The registry consumes it at its counter mutation, even if
//! finalization fails.
class ImportSessionToken final {
public:
  ~ImportSessionToken() = default;
  OXYGEN_MAKE_NON_COPYABLE(ImportSessionToken)
  ImportSessionToken(ImportSessionToken&& other) noexcept
    : owner_(other.owner_)
    , key_(std::move(other.key_))
    , active_(std::exchange(other.active_, false))
  {
  }
  auto operator=(ImportSessionToken&&) -> ImportSessionToken& = delete;
  [[nodiscard]] auto IsActive() const noexcept -> bool { return active_; }

private:
  friend class ResourceTableRegistry;
  friend class LooseCookedIndexRegistry;
  ImportSessionToken(const void* owner, std::string key)
    : owner_(owner)
    , key_(std::move(key))
  {
  }
  auto Validate(const void* owner) const -> void
  {
    if (!active_ || owner != owner_) {
      throw std::logic_error(
        "Inactive or foreign import session participation");
    }
  }
  const void* owner_;
  std::string key_;
  bool active_ = true;
};

} // namespace oxygen::content::import
