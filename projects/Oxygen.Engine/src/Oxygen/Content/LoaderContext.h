//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <Oxygen/Content/Internal/SourceToken.h>
#include <Oxygen/Content/ResourceTypeList.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Serio/Reader.h>
#include <Oxygen/Serio/Stream.h>

namespace oxygen::content {

namespace internal {
  struct DependencyCollector;
} // namespace internal

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

  //! Opaque token representing the mounted source being decoded.
  /*!
   This token is safe to copy across threads and MUST be used by async decode
   pipelines when recording `internal::ResourceRef` dependencies.
  */
  internal::SourceToken source_token {};

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

  //! Whether offline mode must not perform GPU side effects.
  /*!
    When true, loader implementations must treat offline mode as a strict
    contract: do not create, upload, or otherwise touch GPU resources.
  */
  bool work_offline { false };

  //! Optional dependency collector for async decode pipelines.
  /*!
   When non-null, loader implementations MAY record dependency identities into
   this collector instead of mutating the loader dependency graph directly.

   This is intended for "pure decode" loaders used by the async pipeline,
   where dependency graph mutation is deferred to an owning-thread publish
   step.

  @note The collector is shared to provide strong lifetime guarantees across
  thread-pool execution and cancellation paths.
  */
  std::shared_ptr<internal::DependencyCollector> dependency_collector {};

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
