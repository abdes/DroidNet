//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Core/Bindless/Types.h>

namespace oxygen::renderer::resources {

//! Minimal interface for resolving texture keys to bindless SRV indices.
/*!
 MaterialBinder only requires the ability to map texture ResourceKeys to stable
 shader-visible indices.

 This interface allows MaterialBinder tests to use a fake implementation
 without requiring a full TextureBinder + AssetLoader stack.
*/
class ITextureBinder {
public:
  virtual ~ITextureBinder() = default;

  [[nodiscard]] virtual auto GetOrAllocate(const content::ResourceKey& key)
    -> ShaderVisibleIndex
    = 0;

  [[nodiscard]] virtual auto GetErrorTextureIndex() const -> ShaderVisibleIndex
    = 0;
};

} // namespace oxygen::renderer::resources
