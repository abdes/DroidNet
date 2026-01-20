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

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/Import/ImportDiagnostics.h>
#include <Oxygen/Content/Import/ImportRequest.h>
#include <Oxygen/Content/Import/Naming.h>
#include <Oxygen/Content/Import/Pipelines/GeometryPipeline.h>
#include <Oxygen/Content/Import/Pipelines/MaterialPipeline.h>
#include <Oxygen/Content/Import/Pipelines/ScenePipeline.h>
#include <Oxygen/Content/Import/Pipelines/TexturePipeline.h>
#include <Oxygen/Data/AssetKey.h>

namespace oxygen::content::import::adapters {

//! Inputs shared by format adapters.
struct AdapterInput final {
  std::string_view source_id_prefix;
  std::string_view object_path_prefix;

  std::span<const data::AssetKey> material_keys;
  data::AssetKey default_material_key;

  ImportRequest request;
  observer_ptr<NamingService> naming_service;
  std::stop_token stop_token;
};

//! Tag selecting geometry work item production.
struct GeometryWorkTag final {
  explicit GeometryWorkTag() = default;
};

//! Tag selecting scene work item production.
struct SceneWorkTag final {
  explicit SceneWorkTag() = default;
};

//! Tag selecting material work item production.
struct MaterialWorkTag final {
  explicit MaterialWorkTag() = default;
};

//! Tag selecting texture work item production.
struct TextureWorkTag final {
  explicit TextureWorkTag() = default;
};

//! Streaming sink for geometry work items.
class GeometryWorkItemSink {
public:
  virtual ~GeometryWorkItemSink() = default;

  //! Consume one geometry work item. Return false to stop streaming.
  virtual auto Consume(GeometryPipeline::WorkItem item) -> bool = 0;
};

//! Streaming sink for scene work items.
class SceneWorkItemSink {
public:
  virtual ~SceneWorkItemSink() = default;

  //! Consume one scene work item. Return false to stop streaming.
  virtual auto Consume(ScenePipeline::WorkItem item) -> bool = 0;
};

//! Streaming sink for material work items.
class MaterialWorkItemSink {
public:
  virtual ~MaterialWorkItemSink() = default;

  //! Consume one material work item. Return false to stop streaming.
  virtual auto Consume(MaterialPipeline::WorkItem item) -> bool = 0;
};

//! Streaming sink for texture work items.
class TextureWorkItemSink {
public:
  virtual ~TextureWorkItemSink() = default;

  //! Consume one texture work item. Return false to stop streaming.
  virtual auto Consume(TexturePipeline::WorkItem item) -> bool = 0;
};

//! Result of streaming work item production.
struct WorkItemStreamResult final {
  size_t emitted = 0;
  std::vector<ImportDiagnostic> diagnostics;
  bool success = true;
};

} // namespace oxygen::content::import::adapters
