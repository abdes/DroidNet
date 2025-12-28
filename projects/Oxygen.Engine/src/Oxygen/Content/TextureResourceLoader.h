//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <functional>
#include <memory>

#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Content/api_export.h>
#include <Oxygen/Data/TextureResource.h>

namespace oxygen::content {

//! Minimal texture loading interface for renderer subsystems.
/*!
 This interface intentionally exposes only the callback-based texture loading
 entrypoint that renderer systems require.

 The primary production implementation is `content::AssetLoader`, but tests can
 supply fakes that return deterministic CPU-side `data::TextureResource`
 payloads without requiring coroutine activation.
*/
class TextureResourceLoader {
public:
  virtual ~TextureResourceLoader() = default;

  //! Begin loading a texture resource and invoke `on_complete` on completion.
  virtual void StartLoadTexture(ResourceKey key,
    std::function<void(std::shared_ptr<data::TextureResource>)> on_complete)
    = 0;

  //! Mint a synthetic texture key suitable for buffer-driven workflows.
  [[nodiscard]] virtual auto MintSyntheticTextureKey() -> ResourceKey = 0;
};

} // namespace oxygen::content
