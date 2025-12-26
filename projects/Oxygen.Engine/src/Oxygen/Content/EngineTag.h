//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

namespace oxygen::content {

namespace internal {
  struct EngineTagFactory;
} // namespace internal

//! Capability token that allows engine internals to construct AssetLoader
class EngineTag {
  friend struct internal::EngineTagFactory;
  EngineTag() noexcept = default;
};

namespace internal {
  struct EngineTagFactory {
    static auto Get() noexcept -> EngineTag;
  };
} // namespace internal

} // namespace oxygen::content
