//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Core/Bindless/Types.h>

namespace oxygen::renderer::resources {

//! Maps resource keys to shader-visible bindless indices.
/*!
 Provides the minimal capability needed by `MaterialBinder` to resolve
 `content::ResourceKey` values into stable, shader-visible indices in a bindless
 descriptor table.

 Implementations typically allocate a new slot on first use and return the
 existing slot on subsequent calls with the same key.

 Thread-safety and lifetime management are implementation-defined.

 ### Usage Patterns

 `MaterialBinder` can depend on this interface in both production and tests.
 Unit tests may provide a fake implementation that returns deterministic
 indices without requiring a full texture-binding or asset-loading stack.

 @warning The returned index must remain valid for as long as any shader or
 material may reference it.
 @see MaterialBinder
 @see TextureBinder
*/
class IResourceBinder {
public:
  IResourceBinder() = default;
  virtual ~IResourceBinder() = default;

  OXYGEN_DEFAULT_COPYABLE(IResourceBinder)
  OXYGEN_DEFAULT_MOVABLE(IResourceBinder)

  //! Gets a stable shader-visible index for `key`, allocating one if needed.
  /*!
   Resolves an opaque `content::ResourceKey` into a `ShaderVisibleIndex` usable
   in shader code.

   @param key Opaque resource identifier.
   @return A shader-visible bindless index for the resolved resource.

   ### Must-hold contracts

   - **Idempotent mapping**: Calling `GetOrAllocate(key)` repeatedly must return
     the same `ShaderVisibleIndex` for the lifetime of the binder.
   - **Stability / non-recycling**: Once an index is returned for a key, that
     index must remain valid and continue to refer to some shader-visible
     descriptor for as long as materials/shaders might use it.
   - **Always returns a valid shader-visible index**: If resolution/allocation
     fails, return a valid fallback/error binding rather than
     `kInvalidShaderVisibleIndex`.

   ### Common expectations

   - **Distinctness**: Different keys should generally map to different indices.
     Exceptions may exist for reserved fast-path keys.
   - **Should not throw**: Callers may invoke this from `noexcept` code paths.
     Implementations should be effectively non-throwing.

   ### Explicitly implementation-defined

   - Thread-safety and locking strategy.
   - Allocation strategy and descriptor lifetime management.
   - Whether the descriptor behind an index may be repointed over time. The
     returned index must remain stable, but what it references may change.
  */
  [[nodiscard]] virtual auto GetOrAllocate(const content::ResourceKey& key)
    -> ShaderVisibleIndex
    = 0;
};

} // namespace oxygen::renderer::resources
