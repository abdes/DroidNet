//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Import/PhysicsImportSettings.h>
#include <Oxygen/Cooker/Import/TextureSourceAssembly.h>
#include <Oxygen/Cooker/Loose/LooseCookedLayout.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/SourceKey.h>

namespace oxygen::content::import {

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

  //! Inflight scene binding context for scripting-sidecar resolution.
  /*!
   These entries are orchestration-provided runtime context, not authored

   * import options. They are consumed only by sidecar imports to resolve and

   * patch a scene that may not yet be present in the cooked-root index.
  */
  struct InflightSceneContext final {
    data::AssetKey scene_key {};
    std::string virtual_path;
    std::string descriptor_relpath;
    std::vector<std::byte> descriptor_bytes;
  };

  //! Source file (FBX, glTF, GLB, or primary texture).
  std::filesystem::path source_path;

  //! Optional additional source files for multi-source imports.
  std::vector<ImportSource> additional_sources;

  //! Optional destination directory (the loose cooked root).
  /*!
   If set, this path MUST be absolute.

    If unset, the importer derives the cooked root from `source_path` and
    `loose_cooked_layout.virtual_mount_root`, ensuring the cooked root ends with
    the virtual mount root leaf directory (by default: `.cooked`).
  */
  std::optional<std::filesystem::path> cooked_root;

  //! Loose cooked container layout conventions.
  /*!
   Destination container layout used by the cook pipeline.

   This controls where the importer should place descriptor files for
    different asset types (scene/geometry/materials) and where it
   should write bulk resource blobs (tables/data).

   @note To place all descriptors into a single folder, set `descriptors_dir`
    to that folder and set all *\*_subdir fields (for example, `scenes_subdir`,
    `geometry_subdir`, `materials_subdir`) to empty strings.
  */
  LooseCookedLayout loose_cooked_layout = {};

  //! Optional explicit source GUID for the cooked container.
  std::optional<data::SourceKey> source_key;

  //! Optional human-readable job name for logging and UI.
  std::optional<std::string> job_name;

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

  std::optional<OrchestrationMetadata> orchestration;

  //! Import options.
  ImportOptions options = {};

  //! Optional physics-sidecar request payload.
  /*!
   Presence indicates this request must be handled by the physics
   * sidecar

   * domain rather than format-based import routing.
  */
  std::optional<PhysicsImportSettings> physics;

  //! Optional buffer-container request payload.
  /*!
   Presence indicates this request must be handled by the
   * buffer-container
   domain rather than format-based import routing.
  */
  std::optional<BufferContainerPayload> buffer_container;

  //! Optional cooked roots mounted for resolver-only scene lookup context.
  /*!
   The sidecar pipeline mounts these roots in the listed order after the

   * request cooked root, so later entries have higher precedence in

   * `content::VirtualPathResolver`.
  */
  std::vector<std::filesystem::path> cooked_context_roots;

  //! Optional inflight scene contexts for sidecar target resolution.
  /*!
   The sidecar pipeline treats exact virtual-path matches as an inflight

   * fast-path. If multiple inflight entries match the target path, the request

   * fails with an ambiguity diagnostic.
  */
  std::vector<InflightSceneContext> inflight_scene_contexts;

  //! Derives a stable scene name from the source file stem.
  /*!
   Used as the default namespace for imported assets and for scene virtual path
   generation. Returns "Scene" if the source path has no stem.
  */
  OXGN_COOK_NDAPI auto GetSceneName() const -> std::string;

  //! Auto-detects the import format from the source path extension.
  OXGN_COOK_NDAPI auto GetFormat() const -> ImportFormat;
};

} // namespace oxygen::content::import
