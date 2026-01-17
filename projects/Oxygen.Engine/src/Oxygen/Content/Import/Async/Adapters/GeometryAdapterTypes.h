//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Content/Import/Async/Pipelines/GeometryPipeline.h>
#include <Oxygen/Content/Import/ImportDiagnostics.h>
#include <Oxygen/Content/Import/ImportRequest.h>
#include <Oxygen/Data/AssetKey.h>

namespace oxygen::content::import::adapters {

//! Inputs shared by geometry adapters.
struct GeometryAdapterInput final {
  std::string_view source_id_prefix;
  std::string_view object_path_prefix;

  std::span<const data::AssetKey> material_keys;
  data::AssetKey default_material_key;

  ImportRequest request;
  std::stop_token stop_token;
};

//! Adapter output container.
struct GeometryAdapterOutput final {
  std::vector<GeometryPipeline::WorkItem> work_items;
  std::vector<ImportDiagnostic> diagnostics;
  bool success = true;
};

//! Concept for geometry adapters.
template <typename T, typename SourceT>
concept GeometryAdapter = requires(
  const T adapter, const SourceT& source, const GeometryAdapterInput& input) {
  {
    adapter.BuildWorkItems(source, input)
  } -> std::same_as<GeometryAdapterOutput>;
};

} // namespace oxygen::content::import::adapters
