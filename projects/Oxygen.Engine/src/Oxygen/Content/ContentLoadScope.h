//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <Oxygen/Base/Macros.h>

namespace oxygen::content {
class AssetLoader;
namespace internal {
  struct ContentLoadScopeState;
}

//! Shares one content-layer snapshot across related asynchronous loads.
//! Release the scope after those loads complete; assets retain their own
//! bindings.
class ContentLoadScope final {
public:
  ContentLoadScope() = default;
  ~ContentLoadScope() = default;
  OXYGEN_DEFAULT_COPYABLE(ContentLoadScope)
  OXYGEN_DEFAULT_MOVABLE(ContentLoadScope)

private:
  friend class AssetLoader;
  std::shared_ptr<const internal::ContentLoadScopeState> state_ {};
};

} // namespace oxygen::content
