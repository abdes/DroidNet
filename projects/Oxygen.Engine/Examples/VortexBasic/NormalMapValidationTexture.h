//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/ResourceKey.h>

namespace oxygen::content {
class IAssetLoader;
}

namespace oxygen::examples::vortex_basic {

//! Small normal-map fixture loaded through the ordinary texture asset pipeline.
class NormalMapValidationTexture {
public:
  explicit NormalMapValidationTexture(
    observer_ptr<content::IAssetLoader> loader);
  ~NormalMapValidationTexture();

  OXYGEN_MAKE_NON_COPYABLE(NormalMapValidationTexture)
  OXYGEN_MAKE_NON_MOVABLE(NormalMapValidationTexture)

  [[nodiscard]] auto EnsureReady() -> bool;
  [[nodiscard]] auto Key() const noexcept -> content::ResourceKey
  {
    return key_;
  }

private:
  struct LoadState;
  observer_ptr<content::IAssetLoader> loader_;
  std::shared_ptr<LoadState> load_state_;
  content::ResourceKey key_ {};
  bool pinned_ { false };
};

} // namespace oxygen::examples::vortex_basic
