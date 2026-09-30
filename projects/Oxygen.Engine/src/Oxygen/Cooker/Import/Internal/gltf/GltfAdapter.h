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

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/AdapterTypes.h>
#include <Oxygen/Cooker/Import/Internal/ModelGeometrySource.h>
#include <Oxygen/Cooker/Import/Internal/ModelMaterialSource.h>
#include <Oxygen/Cooker/Import/Internal/ModelTextureSource.h>
#include <Oxygen/Cooker/Import/SceneSourceInspection.h>
#include <Oxygen/Cooker/api_export.h>

namespace oxygen::content::import::adapters {

//! Format adapter that parses glTF once and emits pipeline work items.
class GltfAdapter final : public std::enable_shared_from_this<GltfAdapter> {
public:
  //! Result of parsing a glTF source.
  struct ParseResult final {
    std::vector<ImportDiagnostic> diagnostics;
    bool success = true;
  };

  //! Descriptor for an external texture source discovered in a glTF.
  struct ExternalTextureSource final {
    std::string texture_id;
    std::filesystem::path resolved_path;
  };

  //! Inspect source metadata without generating any pipeline work.
  OXGN_COOK_NDAPI static auto InspectSource(
    const std::filesystem::path& source_path, const AdapterInput& input)
    -> SceneSourceInspection;

  OXGN_COOK_API GltfAdapter();
  OXGN_COOK_API ~GltfAdapter();

  //! Parse a glTF document from a file path.
  OXGN_COOK_NDAPI auto Parse(const std::filesystem::path& source_path,
    const AdapterInput& input, ModelParseMode mode = ModelParseMode::kGeometry)
    -> ParseResult;

  //! Parse a glTF document from an in-memory buffer.
  OXGN_COOK_NDAPI auto Parse(std::span<const std::byte> source_bytes,
    const AdapterInput& input, ModelParseMode mode = ModelParseMode::kGeometry)
    -> ParseResult;

  //! Assign geometry identities using the same bake variants as production.
  OXGN_COOK_NDAPI auto PrepareGeometry(const AdapterInput& input) const
    -> ModelGeometryPreparation;

  //! Stream work items for the requested pipeline type.
  OXGN_COOK_NDAPI auto BuildWorkItems(
    GeometryWorkTag tag, GeometryWorkItemSink& sink, const AdapterInput& input)
    -> WorkItemStreamResult;

  //! Prepare material names, values and references without loading texture
  //! bytes. Use a fresh naming service for each analysis or import operation.
  OXGN_COOK_NDAPI auto PrepareMaterials(const AdapterInput& input)
    -> ModelMaterialPreparation;

  //! Stream material work items.
  OXGN_COOK_NDAPI auto BuildWorkItems(
    MaterialWorkTag tag, MaterialWorkItemSink& sink, const AdapterInput& input)
    -> WorkItemStreamResult;

  //! Prepare texture uses, effective recipes and external paths without byte
  //! I/O.
  OXGN_COOK_NDAPI auto PrepareTextures(const AdapterInput& input) const
    -> ModelTexturePreparation;

  //! Stream texture work items.
  OXGN_COOK_NDAPI auto BuildWorkItems(
    TextureWorkTag tag, TextureWorkItemSink& sink, const AdapterInput& input)
    -> WorkItemStreamResult;

  //! External geometry buffers declared by the parsed model, without loading
  //! them.
  OXGN_COOK_NDAPI auto CollectExternalBufferSources(
    const AdapterInput& input) const -> std::vector<std::filesystem::path>;

  //! Collect external texture sources referenced by the parsed document.
  OXGN_COOK_NDAPI auto CollectExternalTextureSources(
    const AdapterInput& input, std::vector<ImportDiagnostic>& diagnostics) const
    -> std::vector<ExternalTextureSource>;

  //! Stream scene work items.
  OXGN_COOK_NDAPI auto BuildWorkItems(SceneWorkTag tag, SceneWorkItemSink& sink,
    const AdapterInput& input) -> WorkItemStreamResult;

  //! Build scene stage data for the scene pipeline.
  [[nodiscard]] auto BuildSceneStage(const SceneStageInput& input,
    std::vector<ImportDiagnostic>& diagnostics) const -> SceneStageResult;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace oxygen::content::import::adapters
