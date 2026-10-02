//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <optional>
#include <span>
#include <stdexcept>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/ResidencyPolicy.h>
#include <Oxygen/Content/ResourceTypeList.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/Data/SourceOrigin.h>
#include <Oxygen/Serio/Reader.h>
#include <Oxygen/Serio/Stream.h>

namespace oxygen::content {

namespace internal {
  struct DependencyCollector;
  class IContentSource;
} // namespace internal

//! Context passed to loader functions containing all necessary loading state.

struct LoaderContext {
  //! Raw descriptor parsing may omit metadata. Source-backed validation and
  //! loading supply it and must match every declared binding and key.
  auto ValidateReferences(std::span<const data::ResourceReferenceUse> resources,
    std::span<const data::KeyReference> keys) const -> void
  {
    if (!asset_references) {
      return;
    }
    const auto valid = asset_references->ValidateUsage(resources, keys);
    if (!valid) {
      throw std::runtime_error(valid.error());
    }
  }

  [[nodiscard]] auto ResolveResource(
    const data::ResourceReferenceIndex reference,
    const data::ResourceKind kind) const -> std::optional<ResourceIndexT>
  {
    if (reference == data::kNoResourceReference) {
      return std::nullopt;
    }
    if (!asset_references) {
      throw std::runtime_error(
        "Asset reference metadata is required for resource resolution");
    }
    const auto resolved = asset_references->ResolveResource(reference, kind);
    if (!resolved) {
      throw std::runtime_error(resolved.error());
    }
    return *resolved;
  }

  //! Key of the current asset being loaded (for dependency registration)
  data::AssetKey current_asset_key;

  //! Opaque token representing the mounted source being decoded.
  /*!
   This token is safe to copy across threads and MUST be used by async decode
   pipelines when recording `internal::ResourceRef` dependencies.
  */
  data::SourceInstanceId source_instance {};

  //! Reader, already positioned at the start of the asset/resource descriptor
  //! to load.
  serio::AnyReader* desc_reader {};
  //! Borrowed only during synchronous decode; the result takes ownership.
  observer_ptr<const data::AssetReferences> asset_references {};

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

  //! Default load priority class assigned by runtime residency policy.
  LoadPriorityClass default_priority_class { LoadPriorityClass::kDefault };

  //! Request-level load priority propagated by runtime APIs.
  LoadPriority request_priority { LoadPriority::kDefault };

  //! Request-level load intent propagated by runtime APIs.
  LoadIntent request_intent { LoadIntent::kRuntime };

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

  //! Retains source readers and auxiliary records through decode and asset use.
  std::shared_ptr<const internal::IContentSource> source_content {};
  //! Decoders preserve this identity on disk-loaded Data::Asset objects.
  data::SourceKey source_key {};

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
