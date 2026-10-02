//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/BufferSource.h>
#include <Oxygen/Cooker/Import/Internal/GeometrySource.h>
#include <Oxygen/Cooker/Import/Internal/ImportManifest_schema.h>
#include <Oxygen/Cooker/Import/Internal/Utils/BufferDescriptorSidecar.h>
#include <Oxygen/Cooker/Import/Internal/Utils/JsonSchemaValidation.h>
#include <Oxygen/Data/MaterialSlotId.h>
#include <Oxygen/Data/ProceduralMeshDefaults.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Writer.h>

namespace oxygen::content::import::internal {
namespace {
  using nlohmann::json;
  using nlohmann::json_schema::json_validator;
  namespace recipe = oxygen::data::procedural;

  auto AddDiagnostic(std::vector<ImportDiagnostic>& diagnostics,
    const std::filesystem::path& source_path, const ImportSeverity severity,
    std::string code, std::string message, std::string object_path = {}) -> void
  {
    diagnostics.push_back({
      .severity = severity,
      .code = std::move(code),
      .message = std::move(message),
      .source_path = source_path.string(),
      .object_path = std::move(object_path),
    });
  }

  auto GetGeometryDescriptorValidator() -> json_validator&
  {
    static auto validator = [] -> json_validator {
      auto out = json_validator {};
      out.set_root_schema(json::parse(kGeometryDescriptorSchema));
      return out;
    }();
    return validator;
  }

  auto ValidateDescriptorSchema(std::vector<ImportDiagnostic>& diagnostics,
    const std::filesystem::path& source_path,
    const nlohmann::json& descriptor_doc) -> bool
  {
    const auto config = internal::JsonSchemaValidationDiagnosticConfig {
      .validation_failed_code = "geometry.descriptor.schema_validation_failed",
      .validation_failed_prefix = "Geometry descriptor validation failed: ",
      .validation_overflow_prefix = "Geometry descriptor validation emitted ",
      .validator_failure_code = "geometry.descriptor.schema_validator_failure",
      .validator_failure_prefix
      = "Geometry descriptor schema validator failed: ",
      .max_issues = 12,
    };

    return internal::ValidateJsonSchemaWithDiagnostics(
      GetGeometryDescriptorValidator(), descriptor_doc, config,
      [&](const std::string_view code, const std::string& message,
        const std::string& object_path) -> void {
        AddDiagnostic(diagnostics, source_path, ImportSeverity::kError,
          std::string(code), message, object_path);
      });
  }

  template <typename T>
  auto WritePod(serio::AnyWriter& writer, const T& value) -> bool
  {
    return static_cast<bool>(
      writer.WriteBlob(std::as_bytes(std::span<const T, 1>(&value, 1))));
  }

  auto BuildProceduralParamBlob(const json& lod_doc,
    std::string& full_mesh_name, std::vector<std::byte>& out_blob) -> bool
  {
    if (!lod_doc.contains("procedural")
      || !lod_doc.at("procedural").is_object()) {
      return false;
    }
    const auto& proc = lod_doc.at("procedural");
    const auto generator = proc.at("generator").get<std::string>();
    const auto mesh_name = proc.at("mesh_name").get<std::string>();
    full_mesh_name = generator + "/" + mesh_name;

    out_blob.clear();
    auto stream = serio::MemoryStream {};
    auto writer = serio::Writer(stream);
    const auto pack = writer.ScopedAlignment(1);

    const auto has_params
      = proc.contains("params") && proc.at("params").is_object();
    const auto& params = has_params ? proc.at("params") : json::object();

    const auto write_u32 = [&writer](const std::string& key, const json& obj,
                             const uint32_t default_value) -> bool {
      const auto value
        = obj.contains(key) ? obj.at(key).get<uint32_t>() : default_value;
      return WritePod(writer, value);
    };
    const auto write_f32 = [&writer](const std::string& key, const json& obj,
                             const float default_value) -> bool {
      const auto value
        = obj.contains(key) ? obj.at(key).get<float>() : default_value;
      return WritePod(writer, value);
    };

    auto ok = true;
    if (generator == "Sphere") {
      ok = write_u32(
             "latitude_segments", params, recipe::kSphereLatitudeSegments)
        && write_u32(
          "longitude_segments", params, recipe::kSphereLongitudeSegments);
    } else if (generator == "SubdividedCube") {
      ok = write_u32("segments", params, recipe::kSubdividedCubeSegments);
    } else if (generator == "IcoSphere") {
      ok = write_u32(
        "subdivision_level", params, recipe::kIcoSphereSubdivisionLevel);
    } else if (generator == "Plane") {
      ok = write_u32("x_segments", params, recipe::kPlaneXSegments)
        && write_u32("z_segments", params, recipe::kPlaneZSegments)
        && write_f32("size", params, recipe::kPlaneSize);
    } else if (generator == "Cylinder") {
      ok = write_u32("segments", params, recipe::kCylinderSegments)
        && write_f32("height", params, recipe::kCylinderHeight)
        && write_f32("radius", params, recipe::kCylinderRadius);
    } else if (generator == "Cone") {
      ok = write_u32("segments", params, recipe::kConeSegments)
        && write_f32("height", params, recipe::kConeHeight)
        && write_f32("radius", params, recipe::kConeRadius);
    } else if (generator == "Capsule") {
      ok = write_u32(
             "hemisphere_segments", params, recipe::kCapsuleHemisphereSegments)
        && write_u32("radial_segments", params, recipe::kCapsuleRadialSegments)
        && write_f32("height", params, recipe::kCapsuleHeight)
        && write_f32("radius", params, recipe::kCapsuleRadius);
    } else if (generator == "Torus") {
      ok = write_u32("major_segments", params, recipe::kTorusMajorSegments)
        && write_u32("minor_segments", params, recipe::kTorusMinorSegments)
        && write_f32("major_radius", params, recipe::kTorusMajorRadius)
        && write_f32("minor_radius", params, recipe::kTorusMinorRadius);
    } else if (generator == "Quad") {
      ok = write_f32("width", params, recipe::kQuadWidth)
        && write_f32("height", params, recipe::kQuadHeight);
    } else {
      // Cube and ArrowGizmo do not consume params.
      ok = true;
    }

    if (!ok) {
      return false;
    }

    const auto bytes = stream.Data();
    out_blob.assign(bytes.begin(), bytes.end());
    return true;
  }

  auto BuildSubmitterBufferChunks(const json& descriptor_doc) -> json
  {
    if (!descriptor_doc.contains("buffers")) {
      return json {};
    }

    auto chunks = json::array();
    for (const auto& authored : descriptor_doc.at("buffers")) {
      auto chunk = authored;
      chunk.update({ { "source", authored.at("uri") } });
      chunk.erase("uri");
      chunks.push_back(std::move(chunk));
    }
    return chunks;
  }

  auto ReadBounds(const json& document) -> GeometrySource::Bounds
  {
    auto bounds = GeometrySource::Bounds {};
    for (size_t axis = 0; axis < bounds.min.size(); ++axis) {
      bounds.min.at(axis) = document.at("min").at(axis).get<float>();
      bounds.max.at(axis) = document.at("max").at(axis).get<float>();
    }
    return bounds;
  }

  auto ReadSource(const json& document,
    const std::filesystem::path& source_path,
    std::vector<ImportDiagnostic>& diagnostics) -> std::optional<GeometrySource>
  {
    auto source = GeometrySource {};
    source.name = document.at("name").get<std::string>();
    source.bounds = ReadBounds(document.at("bounds"));
    if (document.contains("content_hashing")) {
      source.content_hashing = document.at("content_hashing").get<bool>();
    }
    if (document.contains("buffers")) {
      auto buffers = BufferSource::FromDeclarations(
        BuildSubmitterBufferChunks(document), source_path, diagnostics);
      if (!buffers.has_value()) {
        return std::nullopt;
      }
      source.buffers = std::move(*buffers);
    }
    const auto& lods = document.at("lods");
    source.lods.reserve(lods.size());
    for (size_t lod_index = 0; lod_index < lods.size(); ++lod_index) {
      const auto& lod_doc = lods.at(lod_index);
      const auto lod_path = "lods[" + std::to_string(lod_index) + "]";
      auto lod = GeometrySource::Lod {};
      lod.name = lod_doc.at("name").get<std::string>();
      lod.bounds = ReadBounds(lod_doc.at("bounds"));
      const auto type = lod_doc.at("mesh_type").get<std::string>();
      if (type == "standard" || type == "skinned") {
        auto buffers = GeometrySource::Buffers {
          .vertex = lod_doc.at("buffers").at("vb_ref").get<std::string>(),
          .index = lod_doc.at("buffers").at("ib_ref").get<std::string>(),
        };
        if (type == "skinned") {
          const auto& skin = lod_doc.at("skinning");
          auto skinned = GeometrySource::Skinned {
            .buffers = std::move(buffers),
            .joint_index = skin.at("joint_index_ref").get<std::string>(),
            .joint_weight = skin.at("joint_weight_ref").get<std::string>(),
            .inverse_bind = skin.at("inverse_bind_ref").get<std::string>(),
            .joint_remap = skin.at("joint_remap_ref").get<std::string>(),
            .skeleton = std::nullopt,
            .joint_count = skin.at("joint_count").get<uint16_t>(),
            .influences_per_vertex
            = skin.at("influences_per_vertex").get<uint16_t>(),
            .flags = skin.value("flags", uint32_t { 0 }),
          };
          if (skin.contains("skeleton_ref")) {
            skinned.skeleton = skin.at("skeleton_ref").get<std::string>();
          }
          lod.mesh = std::move(skinned);
        } else {
          lod.mesh = std::move(buffers);
        }
      } else {
        auto procedural = GeometrySource::Procedural {};
        if (!BuildProceduralParamBlob(
              lod_doc, procedural.name, procedural.parameters)) {
          AddDiagnostic(diagnostics, source_path, ImportSeverity::kError,
            "geometry.procedural.params_invalid",
            "Failed encoding procedural parameter blob",
            lod_path + ".procedural.params");
          return std::nullopt;
        }
        lod.mesh = std::move(procedural);
      }
      const auto& submeshes = lod_doc.at("submeshes");
      lod.submeshes.reserve(submeshes.size());
      for (size_t submesh_index = 0; submesh_index < submeshes.size();
        ++submesh_index) {
        const auto& submesh_doc = submeshes.at(submesh_index);
        const auto submesh_path
          = lod_path + ".submeshes[" + std::to_string(submesh_index) + "]";
        const auto slot = data::MaterialSlotId::FromString(
          submesh_doc.at("slot_id").get<std::string>());
        if (!slot || slot.value().IsNil()) {
          AddDiagnostic(diagnostics, source_path, ImportSeverity::kError,
            "geometry.descriptor.slot_id_invalid",
            "Submesh requires a non-nil canonical material slot identity",
            submesh_path + ".slot_id");
          return std::nullopt;
        }
        auto submesh = GeometrySource::Submesh {
          .name = submesh_doc.value(
            "name", "submesh_" + std::to_string(submesh_index)),
          .slot_id = slot.value(),
          .material = submesh_doc.at("material_ref").get<std::string>(),
          .bounds = submesh_doc.contains("bounds")
            ? ReadBounds(submesh_doc.at("bounds"))
            : lod.bounds,
          .views = {},
        };
        const auto& views = submesh_doc.at("views");
        submesh.views.reserve(views.size());
        for (size_t view_index = 0; view_index < views.size(); ++view_index) {
          auto view = views.at(view_index).at("view_ref").get<std::string>();
          if (std::holds_alternative<GeometrySource::Procedural>(lod.mesh)
            && view != kImplicitBufferViewName) {
            AddDiagnostic(diagnostics, source_path, ImportSeverity::kError,
              "geometry.procedural.view_ref_invalid",
              "Procedural submesh view_ref must be '__all__'",
              submesh_path + ".views[" + std::to_string(view_index)
                + "].view_ref");
            return std::nullopt;
          }
          submesh.views.push_back(std::move(view));
        }
        lod.submeshes.push_back(std::move(submesh));
      }
      source.lods.push_back(std::move(lod));
    }
    return source;
  }
} // namespace

auto GeometrySource::FromDescriptor(const std::string_view bytes,
  const std::filesystem::path& source_path,
  std::vector<ImportDiagnostic>& diagnostics) -> std::optional<GeometrySource>
{
  try {
    const auto document = json::parse(bytes);
    if (!ValidateDescriptorSchema(diagnostics, source_path, document)) {
      return std::nullopt;
    }
    return ReadSource(document, source_path, diagnostics);
  } catch (const json::exception& error) {
    AddDiagnostic(diagnostics, source_path, ImportSeverity::kError,
      "geometry.descriptor.request_invalid_json", error.what());
    return std::nullopt;
  }
}

} // namespace oxygen::content::import::internal
