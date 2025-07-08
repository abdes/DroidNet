//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <functional>
#include <memory>

#include <Oxygen/Base/Reader.h>
#include <Oxygen/Base/Stream.h>
#include <Oxygen/Content/ResourceTypeList.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/PakFormat.h>

namespace oxygen::content {

// Forward declarations for loader context
class AssetLoader;
class PakFile;

//! Context passed to loader functions containing all necessary loading state.

template <serio::Stream DescS, serio::Stream DataS> struct LoaderContext {
  //! Asset loader for dependency registration, guaranteed to be valid during a
  //! load operation.
  AssetLoader* asset_loader { nullptr };

  //! Key of the current asset being loaded (for dependency registration)
  data::AssetKey current_asset_key {};

  //! Reader, already positioned at the start of the asset/resource descriptor
  //! to load.
  std::reference_wrapper<serio::Reader<DescS>> desc_reader {};

  //=== Data Readers ===------------------------------------------------------//

  template <typename /*ResourceT*/>
  using DataReaderRef = std::reference_wrapper<serio::Reader<DataS>>;

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
  using DataReadersTuple =
    typename TypeListTransform<ResourceTypeList, DataReaderRef>::type;
  DataReadersTuple data_readers {};

  //! Whether loading is performed in offline mode
  bool offline { false };

  //! Source PAK file from which the asset/resource is being loaded. Guaranteed
  //! to be valid during a load operation.
  const PakFile* source_pak { nullptr };
};

} // namespace oxygen::content
