//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Import/ImportSourceObservation.h>
#include <Oxygen/Cooker/Import/ModelImportState.h>
#include <Oxygen/Cooker/Import/PhysicsImportSettings.h>
#include <Oxygen/Cooker/Import/TextureSourceAssembly.h>
#include <Oxygen/Cooker/Loose/LooseCookedLayout.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/SourceKey.h>

namespace oxygen::content::import {

class SurfaceProvenance;
class CapturedInputSet;

//! Supported authoring source formats.
enum class ImportFormat : uint8_t {
  kUnknown = 0,
  kGltf,
  kFbx,
  kTextureImage,
};

OXGN_COOK_NDAPI auto to_string(ImportFormat format) -> std::string_view;

//! A single source mapping for multi-source imports.
struct ImportSource final {
  std::filesystem::path path;
  SubresourceId subresource;
};

//! Request for importing a source file into a loose cooked container.
struct ImportRequest final {
  //! Buffer-container request payload.
  /*!
   Presence indicates this request must be handled by the
   * buffer-container
   descriptor domain.
  */
  struct BufferContainerPayload final {
    std::string normalized_descriptor_json;
  };

  //! Input-import request payload.
  /*!
   Presence indicates this request must be handled by the input-import domain.
  */
  struct InputPayload final { };

  //! Material-descriptor request payload.
  /*!
   Presence indicates this request must be handled by the
   * material-descriptor
   domain.
  */
  struct MaterialDescriptorPayload final {
    std::string normalized_descriptor_json;
  };

  //! Physics material-descriptor request payload.
  /*!
   Presence indicates this request must be handled by the
   *
   * physics-material-descriptor domain.
  */
  struct PhysicsMaterialDescriptorPayload final {
    std::string normalized_descriptor_json;
  };

  //! Collision shape-descriptor request payload.
  /*!
   Presence indicates this request must be handled by the
   *
   * collision-shape-descriptor domain.
  */
  struct CollisionShapeDescriptorPayload final {
    std::string normalized_descriptor_json;
  };

  //! Geometry-descriptor request payload.
  /*!
   Presence indicates this request must be handled by the
   *
   * geometry-descriptor
   domain.
  */
  struct GeometryDescriptorPayload final {
    std::string normalized_descriptor_json;
  };

  //! Scene-descriptor request payload.
  /*!
   Presence indicates this request must be handled by the
   *
   * scene-descriptor
   domain.
  */
  struct SceneDescriptorPayload final {
    std::string normalized_descriptor_json;
  };

  //! Inflight scene binding context for scripting-sidecar resolution.
  /*!
   These entries are orchestration-provided runtime context, not authored

   * import options. They are consumed only by sidecar imports to resolve and

   * patch a scene that may not yet be present in the cooked-root index.
  */
  struct InflightSceneContext final {
    data::AssetKey scene_key;
    std::string virtual_path;
    //! Virtual path of the source that produces the scene.
    std::string source_path;
    std::string descriptor_relpath;
    std::vector<std::byte> descriptor_bytes;
    data::AssetReferences references;
  };

  //! Source file (FBX, glTF, GLB, or primary texture).
  std::filesystem::path source_path;

  //! Optional frozen source inputs; source_path retains its authored identity.
  std::shared_ptr<const CapturedInputSet> captured_inputs {};

  //! Pending synchronous preparation reads, transferred to the job observer.
  std::vector<ImportSourceObservation> preparation_inputs {};

  //! Identity of the one asset a single-asset job cooks, or of the input
  //! source an input job cooks.
  /*!
   A material, geometry, scene, script, texture, physics material or collision
   shape job writes its descriptor at this virtual path, inside
   `loose_cooked_layout.virtual_mount_root`, and derives the asset key from it.
   An input job names each action and mapping context under it
   (`/Content/Input/Player.input/Jump.oiact`), and a model job each material,
   geometry, texture and scene (`/Content/Ship/Ship.glb/Paint.omat`).
  */
  std::string asset_virtual_path {};

  //! Optional additional source files for multi-source imports.
  std::vector<ImportSource> additional_sources {};

  //! Optional destination directory (the loose cooked root).
  /*!
   If set, this path MUST be absolute.

    If unset, the importer derives the cooked root from `source_path` and
    `loose_cooked_layout.virtual_mount_root`, ensuring the cooked root ends with
    the virtual mount root leaf directory (by default: `.cooked`).
  */
  std::optional<std::filesystem::path> cooked_root {};

  //! Loose cooked container layout conventions.
  /*!
   Destination container layout used by the cook pipeline: the virtual mount
   root, where a container's products go by type, and where bulk resource
   blobs (tables/data) are written. A single-asset job's descriptor is placed
   at `asset_virtual_path` instead.
  */
  LooseCookedLayout loose_cooked_layout = {};

  //! Optional explicit source GUID for the cooked container.
  std::optional<data::SourceKey> source_key {};

  //! Validated source identity, shared across mesh work items.
  std::shared_ptr<const SurfaceProvenance> surface_provenance {};

  //! The node continuity and published nodes of the model's import record;
  //! set when a model import runs from its record.
  std::optional<ModelImportInputs> model_import {};

  //! Optional human-readable job name for logging and UI.
  std::optional<std::string> job_name {};

  //! Optional orchestration metadata for batch scheduling.
  /*!
   This data is not consumed by import pipelines directly. It is carried
   * by
   tooling layers (manifest/batch execution) to model explicit
   * dependency edges
   between requests.
  */
  struct OrchestrationMetadata final {
    std::string job_id;
    std::vector<std::string> depends_on;
  };

  std::optional<OrchestrationMetadata> orchestration {};

  //! Import options.
  ImportOptions options = {};

  //! Optional physics-sidecar request payload.
  /*!
   Presence indicates this request must be handled by the physics
   * sidecar

   * domain rather than format-based import routing.
  */
  std::optional<PhysicsImportSettings> physics {};

  //! Optional buffer-container request payload.
  /*!
   Presence indicates this request must be handled by the
   * buffer-container
   domain rather than format-based import routing.
  */
  std::optional<BufferContainerPayload> buffer_container {};

  //! Optional input-import request payload.
  /*!
   Presence indicates this request must be handled by the input-import domain
   rather than format-based import routing.
  */
  std::optional<InputPayload> input {};

  //! Optional material-descriptor request payload.
  /*!
   Presence indicates this request must be handled by the
   * material-descriptor
   domain rather than format-based import routing.
  */
  std::optional<MaterialDescriptorPayload> material_descriptor {};

  //! Optional physics material-descriptor request payload.
  /*!
   Presence indicates this request must be handled by the
   *
   * physics-material-descriptor domain rather than format-based import

   * routing.
  */
  std::optional<PhysicsMaterialDescriptorPayload>
    physics_material_descriptor {};

  //! Optional collision-shape-descriptor request payload.
  /*!
   Presence indicates this request must be handled by the
   *
   * collision-shape-descriptor domain rather than format-based import routing.

   */
  std::optional<CollisionShapeDescriptorPayload> collision_shape_descriptor {};

  //! Optional geometry-descriptor request payload.
  /*!
   Presence indicates this request must be handled by the
   *
   * geometry-descriptor
   domain rather than format-based import routing.
  */
  std::optional<GeometryDescriptorPayload> geometry_descriptor {};

  //! Optional scene-descriptor request payload.
  /*!
   Presence indicates this request must be handled by the
   *
   * scene-descriptor
   domain rather than format-based import routing.
  */
  std::optional<SceneDescriptorPayload> scene_descriptor {};

  //! Optional cooked roots mounted for resolver-only scene lookup context.
  /*!
   The sidecar pipeline mounts these roots in the listed order after the

   * request cooked root, so later entries have higher precedence in

   * `content::VirtualPathResolver`. A repeated root keeps its last position,

   * allowing callers to explicitly place the destination root in this order.

 */
  std::vector<std::filesystem::path> cooked_context_roots {};

  //! Optional inflight scene contexts for sidecar target resolution.
  /*!
   The sidecar pipeline treats exact virtual-path matches as an inflight

   * fast-path. If multiple inflight entries match the target path, the request

   * fails with an ambiguity diagnostic.
  */
  std::vector<InflightSceneContext> inflight_scene_contexts {};

  //! The name of a model's scene: its identity's leaf without the extension.
  /*!
   `/Content/Ship/Ship.glb` names its scene `Ship`, so the scene is
   `/Content/Ship/Ship.glb/Ship.oscene`. Empty without an identity.
  */
  OXGN_COOK_NDAPI auto GetSceneName() const -> std::string;

  //! The identity of a model's product: `<model identity>/<name><extension>`,
  //! flat under the model.
  /*!
   Analysis, production and tools name every material, geometry, texture and
   scene of a model with this function; `name` is a product name the naming
   service assigned.
  */
  OXGN_COOK_NDAPI auto ModelProductVirtualPath(
    std::string_view name, std::string_view extension) const -> std::string;

  //! The identity of the source the job cooks: its identity's folder and the
  //! source file's name.
  /*!
   A source sits in the folder of what it cooks, so `Red.omat.json` cooking
   `/Content/Materials/Red.omat` is `/Content/Materials/Red.omat.json`,
   `Player.input.json` cooking `/Content/Player.input` is
   `/Content/Player.input.json`, and a model is its own identity. The loose
   cooked index records it as the producer of every entry the job writes.
   Empty without an identity.
  */
  OXGN_COOK_NDAPI auto SourceVirtualPath() const -> std::string;

  //! The descriptor path of a texture job's identity, without emitting or
  //! reading files.
  //! A missing or invalid identity throws std::invalid_argument.
  OXGN_COOK_NDAPI auto GetTextureDescriptorRelPath() const -> std::string;

  //! Resolve the explicit or source-derived output root without creating it.
  OXGN_COOK_NDAPI auto ResolveCookedRoot() const -> std::filesystem::path;

  //! Auto-detects the import format from the source path extension.
  OXGN_COOK_NDAPI auto GetFormat() const -> ImportFormat;
};

} // namespace oxygen::content::import
