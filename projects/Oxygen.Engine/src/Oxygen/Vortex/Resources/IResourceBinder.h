//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <optional>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Core/Bindless/Types.h>

namespace oxygen::vortex::resources {

//! Maps resource keys to shader-visible bindless indices.
/*!
 Provides the minimal capability needed by `MaterialBinder` to resolve
 `content::ResourceKey` values into shader-visible indices in a bindless
 descriptor table.

 The index for a key is versioned: it changes when the content behind it
 changes (for example a placeholder replaced by the loaded texture), because
 a descriptor that recorded GPU work may read is never rewritten. Consumers
 that store indices re-resolve them when `GetResidentContentRevision()`
 changes.

 Thread-safety and lifetime management are implementation-defined.

 ### Usage Patterns

 `MaterialBinder` can depend on this interface in both production and tests.
 Unit tests may provide a fake implementation that returns deterministic
 indices without requiring a full texture-binding or asset-loading stack.

 @warning A returned index stays valid until the frames that may have read it
 complete, not longer; store it only with the revision it was resolved at.
 @see MaterialBinder
 @see TextureBinder
*/
class IResourceBinder {
public:
  IResourceBinder() = default;
  virtual ~IResourceBinder() = default;

  OXYGEN_DEFAULT_COPYABLE(IResourceBinder)
  OXYGEN_DEFAULT_MOVABLE(IResourceBinder)

  //! Gets the current shader-visible index for `key`, allocating if needed.
  /*!
   Resolves an opaque `content::ResourceKey` into a `ShaderVisibleIndex` usable
   in shader code.

   @param key Opaque resource identifier.
   @return A shader-visible bindless index for the resolved resource.

   ### Must-hold contracts

   - **Revisioned mapping**: Calling `GetOrAllocate(key)` repeatedly returns
     the same `ShaderVisibleIndex` until `GetResidentContentRevision()`
     changes.
   - **Receipt-retired**: A replaced index keeps referring to its descriptor
     until the frames that may have read it complete; its content is never
     rewritten.
   - **Always returns a valid shader-visible index**: If resolution/allocation
     fails, return a valid fallback/error binding rather than
     `kInvalidShaderVisibleIndex`.

   ### Common expectations

   - **Distinctness**: Different resident resources map to different indices.
     Placeholder and error bindings are shared.
   - **Should not throw**: Callers may invoke this from `noexcept` code paths.
     Implementations should be effectively non-throwing.

   ### Explicitly implementation-defined

   - Thread-safety and locking strategy.
   - Allocation strategy and descriptor lifetime management.
  */
  [[nodiscard]] virtual auto GetOrAllocate(const content::ResourceKey& key)
    -> ShaderVisibleIndex = 0;

  //! Advances whenever an index returned by GetOrAllocate() may have changed.
  /*!
   Binders whose indices never change keep the default, which never advances.
  */
  [[nodiscard]] virtual auto GetResidentContentRevision() const noexcept
    -> std::uint64_t
  {
    return 0U;
  }

  //! Returns whether a resolved resource is ready for shader sampling.
  /*!
   Indicates whether `key` is expected to resolve to its final descriptor
   contents (not a temporary placeholder binding).

   Implementations may conservatively return `true` for non-placeholder keys if
   they do not track readiness.

   @param key Opaque resource identifier.
   @return `true` if sampling the index returned by GetOrAllocate(key) should
           read the intended final data.
  */
  [[nodiscard]] virtual auto IsResourceReady(
    const content::ResourceKey& key) const noexcept -> bool
  {
    return !key.IsPlaceholder();
  }

  //! Optionally returns the number of mip levels for a resolved resource.
  /*!
   Implementations may return std::nullopt if the information is unavailable.

   @param key Opaque resource identifier.
   @return Number of mip levels for the resource, or std::nullopt if unknown.
  */
  [[nodiscard]] virtual auto TryGetMipLevels(
    const content::ResourceKey& key) const noexcept
    -> std::optional<std::uint32_t>
  {
    (void)key;
    return std::nullopt;
  }
};

} // namespace oxygen::vortex::resources
