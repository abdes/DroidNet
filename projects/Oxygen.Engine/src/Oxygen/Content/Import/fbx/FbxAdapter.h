//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <memory>
#include <span>
#include <vector>

#include <Oxygen/Content/Import/Detail/AdapterTypes.h>
#include <Oxygen/Content/Import/ImportDiagnostics.h>
#include <Oxygen/Content/api_export.h>

namespace oxygen::content::import::adapters {

//! Format adapter that parses FBX once and emits pipeline work items.
class FbxAdapter final : public std::enable_shared_from_this<FbxAdapter> {
public:
  //! Result of parsing an FBX source.
  struct ParseResult final {
    std::vector<ImportDiagnostic> diagnostics;
    bool success = true;
  };

  FbxAdapter();
  ~FbxAdapter();

  //! Parse an FBX scene from a file path.
  OXGN_CNTT_NDAPI auto Parse(const std::filesystem::path& source_path,
    const AdapterInput& input) -> ParseResult;

  //! Parse an FBX scene from an in-memory buffer.
  OXGN_CNTT_NDAPI auto Parse(std::span<const std::byte> source_bytes,
    const AdapterInput& input) -> ParseResult;

  //! Stream work items for the requested pipeline type.
  OXGN_CNTT_NDAPI auto BuildWorkItems(
    GeometryWorkTag tag, GeometryWorkItemSink& sink, const AdapterInput& input)
    -> WorkItemStreamResult;

  //! Stream material work items.
  OXGN_CNTT_NDAPI auto BuildWorkItems(
    MaterialWorkTag tag, MaterialWorkItemSink& sink, const AdapterInput& input)
    -> WorkItemStreamResult;

  //! Stream texture work items.
  OXGN_CNTT_NDAPI auto BuildWorkItems(
    TextureWorkTag tag, TextureWorkItemSink& sink, const AdapterInput& input)
    -> WorkItemStreamResult;

  //! Stream scene work items.
  OXGN_CNTT_NDAPI auto BuildWorkItems(SceneWorkTag tag, SceneWorkItemSink& sink,
    const AdapterInput& input) -> WorkItemStreamResult;

  //! Build scene stage data for the scene pipeline.
  [[nodiscard]] auto BuildSceneStage(const SceneStageInput& input,
    std::vector<ImportDiagnostic>& diagnostics) const -> SceneStageResult;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace oxygen::content::import::adapters
