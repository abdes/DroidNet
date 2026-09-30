//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <filesystem>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/Internal/ImportManifest_schema.h>
#include <Oxygen/Cooker/Import/Internal/TextureImportRequestBuilder.h>
#include <Oxygen/Cooker/Import/Internal/Utils/DescriptorDocument.h>
#include <Oxygen/Cooker/Import/Internal/Utils/ImportSettingsUtils.h>
#include <Oxygen/Cooker/Import/Internal/Utils/JsonSchemaValidation.h>
#include <Oxygen/Cooker/Import/TextureDescriptorImportRequestBuilder.h>
#include <Oxygen/Cooker/Import/TextureDescriptorImportSettings.h>
#include <Oxygen/Cooker/Import/TextureImportSettings.h>

namespace oxygen::content::import {

namespace {

  using internal::JsonSchemaValidationDiagnosticConfig;
  using internal::ValidateJsonSchemaWithDiagnostics;
  using nlohmann::json;
  using nlohmann::json_schema::json_validator;

  auto ResolveRelativePath(const std::filesystem::path& base_dir,
    const std::string& path_text) -> std::string
  {
    auto path = std::filesystem::path(path_text);
    if (path.is_relative() && !base_dir.empty()) {
      path = (base_dir / path).lexically_normal();
    }
    return path.string();
  }

  auto GetTextureDescriptorValidator() -> json_validator&
  {
    static auto validator = []() {
      auto out = json_validator {};
      out.set_root_schema(json::parse(kTextureDescriptorSchema));
      return out;
    }();
    return validator;
  }

  auto ValidateDescriptorSchema(const json& descriptor_doc,
    const std::filesystem::path& source_path,
    std::vector<ImportDiagnostic>& diagnostics) -> bool
  {
    const auto config = JsonSchemaValidationDiagnosticConfig {
      .validation_failed_code = "texture.descriptor.schema_validation_failed",
      .validation_failed_prefix = "Texture descriptor validation failed: ",
      .validation_overflow_prefix = "Texture descriptor validation emitted ",
      .validator_failure_code = "texture.descriptor.schema_validator_failure",
      .validator_failure_prefix
      = "Texture descriptor schema validator failed: ",
      .max_issues = 12,
    };

    return ValidateJsonSchemaWithDiagnostics(GetTextureDescriptorValidator(),
      descriptor_doc, config,
      [&](const std::string_view code, const std::string& message,
        const std::string& object_path) {
        diagnostics.push_back(ImportDiagnostic {
          .severity = ImportSeverity::kError,
          .code = std::string(code),
          .message = message,
          .source_path = source_path.string(),
          .object_path = object_path,
        });
      });
  }

  auto ApplyDescriptorSettings(const json& descriptor_doc,
    const std::filesystem::path& descriptor_path,
    TextureImportSettings& settings) -> void
  {
    const auto descriptor_dir = descriptor_path.parent_path();

    settings.source_path = ResolveRelativePath(
      descriptor_dir, descriptor_doc.at("source").get<std::string>());

    if (descriptor_doc.contains("sources")) {
      settings.sources.clear();
      for (const auto& mapping_doc : descriptor_doc.at("sources")) {
        auto mapping = TextureSourceMapping {};
        mapping.file = ResolveRelativePath(
          descriptor_dir, mapping_doc.at("file").get<std::string>());
        if (mapping_doc.contains("layer")) {
          mapping.layer = mapping_doc.at("layer").get<uint16_t>();
        }
        if (mapping_doc.contains("mip")) {
          mapping.mip = mapping_doc.at("mip").get<uint16_t>();
        }
        if (mapping_doc.contains("slice")) {
          mapping.slice = mapping_doc.at("slice").get<uint16_t>();
        }
        settings.sources.push_back(std::move(mapping));
      }
    }

    if (descriptor_doc.contains("name")) {
      settings.job_name = descriptor_doc.at("name").get<std::string>();
    }
    if (descriptor_doc.contains("virtual_path")) {
      settings.virtual_path
        = descriptor_doc.at("virtual_path").get<std::string>();
    }
    if (descriptor_doc.contains("content_hashing")) {
      settings.with_content_hashing
        = descriptor_doc.at("content_hashing").get<bool>();
    }
    if (descriptor_doc.contains("preset")) {
      settings.preset = descriptor_doc.at("preset").get<std::string>();
    }
    if (descriptor_doc.contains("intent")) {
      settings.intent = descriptor_doc.at("intent").get<std::string>();
    }

    if (descriptor_doc.contains("decode")) {
      const auto& decode = descriptor_doc.at("decode");
      if (decode.contains("color_space")) {
        settings.color_space = decode.at("color_space").get<std::string>();
      }
      if (decode.contains("flip_y")) {
        settings.flip_y = decode.at("flip_y").get<bool>();
      }
      if (decode.contains("force_rgba")) {
        settings.force_rgba = decode.at("force_rgba").get<bool>();
      }
      if (decode.contains("flip_normal_green")) {
        settings.flip_normal_green = decode.at("flip_normal_green").get<bool>();
      }
    }

    if (descriptor_doc.contains("mips")) {
      const auto& mips = descriptor_doc.at("mips");
      if (mips.contains("policy")) {
        settings.mip_policy = mips.at("policy").get<std::string>();
      }
      if (mips.contains("max_mips")) {
        settings.max_mip_levels = mips.at("max_mips").get<uint32_t>();
      }
      if (mips.contains("filter")) {
        settings.mip_filter = mips.at("filter").get<std::string>();
      }
      if (mips.contains("renormalize")) {
        settings.renormalize_normals = mips.at("renormalize").get<bool>();
      }
    }

    if (descriptor_doc.contains("output")) {
      const auto& output = descriptor_doc.at("output");
      if (output.contains("format")) {
        settings.output_format = output.at("format").get<std::string>();
      }
      if (output.contains("data_format")) {
        settings.data_format = output.at("data_format").get<std::string>();
      }
      if (output.contains("bc7_quality")) {
        settings.bc7_quality = output.at("bc7_quality").get<std::string>();
      }
      if (output.contains("packing_policy")) {
        settings.packing_policy
          = output.at("packing_policy").get<std::string>();
      }
    }

    if (descriptor_doc.contains("hdr")) {
      const auto& hdr = descriptor_doc.at("hdr");
      if (hdr.contains("handling")) {
        settings.hdr_handling = hdr.at("handling").get<std::string>();
      }
      if (hdr.contains("exposure_ev")) {
        settings.exposure_ev = hdr.at("exposure_ev").get<float>();
      }
      if (hdr.contains("bake_hdr")) {
        settings.bake_hdr_to_ldr = hdr.at("bake_hdr").get<bool>();
      }
    }

    if (descriptor_doc.contains("cube")) {
      const auto& cube = descriptor_doc.at("cube");
      if (cube.contains("cubemap")) {
        settings.cubemap = cube.at("cubemap").get<bool>();
      }
      if (cube.contains("equirect_to_cube")) {
        settings.equirect_to_cube = cube.at("equirect_to_cube").get<bool>();
      }
      if (cube.contains("cube_face_size")) {
        settings.cube_face_size = cube.at("cube_face_size").get<uint32_t>();
      }
      if (cube.contains("cube_layout")) {
        settings.cube_layout = cube.at("cube_layout").get<std::string>();
      }
    }
  }

} // namespace

auto TextureDescriptorImportSettings::Prepare(const std::string_view bytes,
  std::vector<ImportDiagnostic>& diagnostics) const
  -> std::optional<TextureImportSettings>
{
  const auto path = std::filesystem::path(descriptor_path).lexically_normal();
  json document;
  try {
    document = json::parse(bytes);
  } catch (const json::exception& failure) {
    diagnostics.push_back(ImportDiagnostic {
      .severity = ImportSeverity::kError,
      .code = "texture.descriptor.invalid_json",
      .message = failure.what(),
      .source_path = path.string(),
      .object_path = {},
    });
    return std::nullopt;
  }
  if (!ValidateDescriptorSchema(document, path, diagnostics)) {
    return std::nullopt;
  }
  auto effective = texture;
  ApplyDescriptorSettings(document, path, effective);
  // The same destination-free option interpretation used by request
  // construction.
  auto tuning = ImportOptions::TextureTuning {};
  auto errors = std::ostringstream {};
  if (!internal::MapSettingsToTuning(effective, tuning, errors)) {
    diagnostics.push_back(ImportDiagnostic {
      .severity = ImportSeverity::kError,
      .code = "texture.descriptor.recipe_invalid",
      .message = errors.str(),
      .source_path = path.string(),
      .object_path = {},
    });
    return std::nullopt;
  }
  return effective;
}

namespace internal {

  auto BuildTextureDescriptorRequest(
    const TextureDescriptorImportSettings& settings, std::ostream& error_stream)
    -> std::optional<ImportRequest>
  {
    if (settings.descriptor_path.empty()) {
      error_stream << "ERROR: descriptor_path is required\n";
      return std::nullopt;
    }
    const auto path
      = std::filesystem::path(settings.descriptor_path).lexically_normal();
    const auto text = LoadDescriptorText(path, "texture", error_stream);
    if (!text) {
      return std::nullopt;
    }
    auto diagnostics = std::vector<ImportDiagnostic> {};
    const auto effective = settings.Prepare(*text, diagnostics);
    for (const auto& diagnostic : diagnostics) {
      error_stream << "ERROR [" << diagnostic.code
                   << "]: " << diagnostic.message;
      if (!diagnostic.object_path.empty()) {
        error_stream << " (" << diagnostic.object_path << ")";
      }
      error_stream << '\n';
    }
    return effective ? BuildTextureRequest(*effective, error_stream)
                     : std::nullopt;
  }

} // namespace internal
} // namespace oxygen::content::import
