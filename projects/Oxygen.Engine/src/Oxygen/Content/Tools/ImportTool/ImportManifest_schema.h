//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

namespace oxygen::content::import::tool {

//! JSON Schema for ImportTool batch manifests (Draft-7 compatible).
constexpr const char* kImportManifestSchema = R"schema(
{
  "$schema": "http://json-schema.org/draft-07/schema#",
  "$id": "https://oxygen.engine/schemas/import-manifest.json",
  "title": "Oxygen Import Manifest",
  "type": "object",
  "additionalProperties": false,
  "required": ["version", "jobs"],
  "properties": {
    "version": {
      "type": "integer",
      "minimum": 1
    },
    "defaults": { "$ref": "#/definitions/Defaults" },
    "jobs": {
      "type": "array",
      "minItems": 1,
      "items": { "$ref": "#/definitions/Job" }
    }
  },
  "definitions": {
    "Defaults": {
      "type": "object",
      "additionalProperties": false,
      "properties": {
        "job_type": { "type": "string" },
        "cooked_root": { "type": "string" },
        "job_name": { "type": "string" },
        "verbose": { "type": "boolean" },
        "import_options": { "$ref": "#/definitions/ImportOptions" },
        "texture": { "$ref": "#/definitions/TextureSettings" },
        "fbx": { "$ref": "#/definitions/SceneSettings" },
        "gltf": { "$ref": "#/definitions/SceneSettings" }
      }
    },
    "Job": {
      "type": "object",
      "additionalProperties": false,
      "required": ["source"],
      "properties": {
        "source": { "type": "string" },
        "job_type": { "type": "string" },
        "cooked_root": { "type": "string" },
        "job_name": { "type": "string" },
        "verbose": { "type": "boolean" },
        "import_options": { "$ref": "#/definitions/ImportOptions" },
        "texture": { "$ref": "#/definitions/TextureSettings" },
        "fbx": { "$ref": "#/definitions/SceneSettings" },
        "gltf": { "$ref": "#/definitions/SceneSettings" }
      }
    },
    "ImportOptions": {
      "type": "object",
      "additionalProperties": false,
      "properties": {
        "content_flags": { "$ref": "#/definitions/ContentFlags" },
        "unit_normalization_policy": { "type": "string" },
        "custom_unit_scale": { "type": "number" },
        "bake_transforms": { "type": "boolean" },
        "normals_policy": { "type": "string" },
        "tangents_policy": { "type": "string" },
        "node_pruning_policy": { "type": "string" }
      }
    },
    "ContentFlags": {
      "type": "object",
      "additionalProperties": false,
      "properties": {
        "textures": { "type": "boolean" },
        "materials": { "type": "boolean" },
        "geometry": { "type": "boolean" },
        "scene": { "type": "boolean" }
      }
    },
    "SceneSettings": {
      "type": "object",
      "additionalProperties": false,
      "properties": {
        "content_flags": { "$ref": "#/definitions/ContentFlags" },
        "unit_normalization_policy": { "type": "string" },
        "custom_unit_scale": { "type": "number" },
        "bake_transforms": { "type": "boolean" },
        "normals_policy": { "type": "string" },
        "tangents_policy": { "type": "string" },
        "node_pruning_policy": { "type": "string" }
      }
    },
    "TextureSettings": {
      "type": "object",
      "additionalProperties": false,
      "properties": {
        "preset": { "type": "string" },
        "intent": { "type": "string" },
        "color_space": { "type": "string" },
        "output_format": { "type": "string" },
        "data_format": { "type": "string" },
        "mip_policy": { "type": "string" },
        "mip_filter": { "type": "string" },
        "bc7_quality": { "type": "string" },
        "packing_policy": { "type": "string" },
        "cube_layout": { "type": "string" },
        "max_mip_levels": { "type": "integer", "minimum": 0 },
        "cube_face_size": { "type": "integer", "minimum": 0 },
        "flip_y": { "type": "boolean" },
        "force_rgba": { "type": "boolean" },
        "cubemap": { "type": "boolean" },
        "equirect_to_cube": { "type": "boolean" }
      }
    }
  }
}
)schema";

} // namespace oxygen::content::import::tool
