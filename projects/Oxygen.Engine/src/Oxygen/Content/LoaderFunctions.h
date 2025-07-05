//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <functional>
#include <memory>

#include <Oxygen/Base/FileStream.h>
#include <Oxygen/Base/Reader.h>
#include <Oxygen/Base/Stream.h>
#include <Oxygen/Composition/TypeSystem.h>
#include <Oxygen/Data/AssetKey.h>

namespace oxygen::content {

// Forward declarations for loader context
class AssetLoader;
class PakFile;

//! Context passed to loader functions containing all necessary loading state.
template <oxygen::serio::Stream S> struct LoaderContext {
  //! Asset loader for dependency registration (null for resource loaders)
  AssetLoader* asset_loader { nullptr };

  //! Asset key of the item being loaded (for dependency registration)
  oxygen::data::AssetKey current_asset_key {};

  //! Reader for the data stream
  std::reference_wrapper<oxygen::serio::Reader<S>> reader {};

  //! Whether loading is performed in offline mode
  bool offline { false };

  //! Source PAK file for intra-PAK resource resolution
  const PakFile* source_pak { nullptr };
};

template <typename F, typename S>
concept LoadFunctionForStream
  = oxygen::serio::Stream<S> && requires(F f, LoaderContext<S> context) {
      typename std::remove_cvref_t<decltype(*f(context))>;
      requires IsTyped<std::remove_pointer_t<decltype(f(context).get())>>;
      { f(context) };
    };

template <typename F>
concept LoadFunction = LoadFunctionForStream<F, oxygen::serio::FileStream<>>;

template <typename F, typename T>
concept UnloadFunction = IsTyped<T>
  && requires(
    F f, std::shared_ptr<T> asset, AssetLoader& loader, bool offline) {
       { f(asset, loader, offline) } -> std::same_as<void>;
     };

} // namespace oxygen::content
