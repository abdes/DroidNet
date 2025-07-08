//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <functional>
#include <memory>

#include <Oxygen/Base/FileStream.h>
#include <Oxygen/Base/Stream.h>
#include <Oxygen/Composition/TypeSystem.h>
#include <Oxygen/Content/LoaderContext.h>

namespace oxygen::content {

template <typename F, typename DescS, typename DataS>
concept LoadFunctionForStream
  = oxygen::serio::Stream<DescS> && oxygen::serio::Stream<DataS>
  && requires(F f, LoaderContext<DescS, DataS> context) {
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
 type. The load function is responsible for constructing and returning a fully
 initialized asset or resource from a data stream, while the unload function is
 responsible for cleanup when the object is evicted from the cache. Both must
 consistently use the type `T`.

 ### Load Function Requirements

 - Must be callable as `std::unique_ptr<T> f(LoaderContext<S>)` for a stream S.
 - The returned type `T` must satisfy the `IsTyped` concept.
 - Failure to load must be indicated by returning a null pointer. Do not use
   exceptions to indicate normal load errors; only throw for truly exceptional
   situations (e.g., unrecoverable system errors).
 - The load function must not retain ownership of the context or any temporary
   resources.

 ### Unload Function Requirements

 - Must be callable as `void(std::shared_ptr<T>, AssetLoader&, bool)`.
 - Must handle all errors locally and only throw for exceptional situations that
   cannot be handled internally.

 @see LoaderContext, AssetLoader
*/
template <typename F>
concept LoadFunction
  = LoadFunctionForStream<F, serio::FileStream<>, serio::FileStream<>>;

} // namespace oxygen::content
