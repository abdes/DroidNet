//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Content/ResourceTypeList.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Serio/Reader.h>
#include <Oxygen/Serio/Stream.h>

namespace oxygen::content {

// Forward declarations for loader context
class AssetLoader;
class PakFile;

//! Context passed to loader functions containing all necessary loading state.

struct LoaderContext {
  //! Asset loader for dependency registration, guaranteed to be valid during a
  //! load operation.
  AssetLoader* asset_loader { nullptr };

  //! Key of the current asset being loaded (for dependency registration)
  data::AssetKey current_asset_key {};

  //! Reader, already positioned at the start of the asset/resource descriptor
  //! to load.
  serio::AnyReader* desc_reader {};

  //=== Data Readers ===------------------------------------------------------//

  template <typename /*ResourceT*/> using DataReaderPtr = serio::AnyReader*;

  //! Helper alias for a data reader reference for a given resource type.
  template <typename ResourceT> using DataReaderRef = serio::AnyReader*;

  //! Tuple of data region readers, one for each type in ResourceTypeList.
  /*!
   For each type in `ResourceTypeList`, this will be a Reader positioned at the
   start of the data region for that type. These readers may or may not use the
   same stream or stream type as the descriptor reader. Therefore, it is not
   correct and not legal to use the desc_reader to read data from the data
   regions.

   @note The tuple order matches ResourceTypeList.
   @see ResourceTypeList
  */
  using DataReadersTuple
    = TypeListTransform<ResourceTypeList, DataReaderRef>::Type;
  DataReadersTuple data_readers {};

  //! Whether loading is performed in offline mode
  bool offline { false };

  //! Whether offline mode must not perform GPU side effects.
  /*!
   When true and `offline` is true, loader implementations must treat offline
   mode as a strict contract: do not create, upload, or otherwise touch GPU
   resources.
  */
  bool enforce_offline_no_gpu_work { false };

  //! Source PAK file from which the asset/resource is being loaded. Guaranteed
  //! to be valid during a load operation.
  const PakFile* source_pak { nullptr };

  //! Parse-only mode: loaders should not attempt to load/register dependencies.
  /*!
   When true, loaders must avoid calling back into AssetLoader to resolve
   other assets/resources or to register dependencies.

   This is intended for tooling and unit tests that validate descriptor parsing
   without requiring a mounted content source.
  */
  bool parse_only { false };
};

} // namespace oxygen::content
