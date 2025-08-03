//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <functional>
#include <memory>

#include <Oxygen/Composition/TypeSystem.h>
#include <Oxygen/Content/LoaderContext.h>
#include <Oxygen/Serio/FileStream.h>
#include <Oxygen/Serio/Stream.h>

namespace oxygen::content {

template <typename F>
concept LoadFunctionForStream = requires(F f, LoaderContext context) {
  {
    f(context)
  } -> std::same_as<
    std::unique_ptr<std::remove_pointer_t<decltype(f(context).get())>>>;
  requires IsTyped<std::remove_pointer_t<decltype(f(context).get())>>;
};

//! Concept for asset/resource load functions used with AssetLoader.
/*!
 Load and unload functions are always registered as a pair for a specific asset
 or resource type `T`, where `T` is deduced from the load function's return
 type. The load function constructs and returns a fully initialized asset or
 resource from a data stream, while the unload function performs cleanup when
 the object is evicted from the cache. Both must consistently use the type `T`.

 ### Load Function Requirements

 - Must be callable as `std::unique_ptr<T> f(LoaderContext)`.
 - The returned type `T` must satisfy the `IsTyped` concept.
 - Failure to load must be indicated by returning a null pointer.
 - Must not retain ownership of the context or any temporary resources.

 ### How Load Functions Are Called

 - Signature: `std::unique_ptr<T> LoadFunc(LoaderContext context)`
   - `context`: LoaderContext provides access to the asset/resource descriptor
     stream, asset loader, current asset key, and data readers for all resource
     types. It is always passed by value and contains all necessary state for
     loading.
   - The function must read from `context.desc_reader` and may use other fields
     as needed. It must not retain ownership of the context or any temporary
     resources.
   - Return: A fully initialized `std::unique_ptr<T>` (where T satisfies
     `IsTyped`). If loading fails, return a null pointer. Do not use exceptions
     for normal load errors; only throw for unrecoverable system errors.

 @param context LoaderContext for the load function (see above)
 @param resource Shared pointer to resource for unload function
 @param loader AssetLoader reference for unload function
 @param offline Boolean indicating offline mode for unload function
*/
template <typename F>
concept LoadFunction = LoadFunctionForStream<F>;

//! Concept for asset/resource unload functions used with AssetLoader.
/*!
 Unload functions are registered as cleanup handlers for a specific asset or
 resource type `T`. They are called when an asset/resource is evicted from the
 cache or explicitly unloaded.

 ### How Unload Functions Are Called

 - Signature: `void UnloadFunc(std::shared_ptr<T> resource, AssetLoader& loader,
   bool offline)`
   - `resource`: Shared pointer to the asset/resource to be cleaned up. May be
     null if the resource was never loaded.
   - `loader`: Reference to the AssetLoader managing the resource. Used for
     dependency cleanup or resource deregistration.
   - `offline`: Indicates whether the engine is running in offline mode (no GPU
     resources). If true, skip GPU resource cleanup.
   - The function must handle all errors locally and only throw for exceptional
     situations that cannot be handled internally.

 ### Unload Function Requirements

 - Must be callable as `void(std::shared_ptr<T>, AssetLoader&, bool)`.
 - Must not return a value.
 - Must handle all errors locally and only throw for exceptional situations.

 @param resource Shared pointer to resource for unload function
 @param loader AssetLoader reference for unload function
 @param offline Boolean indicating offline mode for unload function
*/
template <typename F, typename T>
concept UnloadFunction = requires(
  F f, std::shared_ptr<T> resource, AssetLoader& loader, bool offline) {
  { f(resource, loader, offline) } -> std::same_as<void>;
};

} // namespace oxygen::content
