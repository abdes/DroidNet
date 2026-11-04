//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Cooker/Import/ImportCodes.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/ImportRequestCodes.h>
#include <Oxygen/Cooker/Import/ImportSourceDocument.h>
#include <Oxygen/Cooker/Import/Internal/ImportManifest_schema.h>
#include <Oxygen/Cooker/Import/Internal/TextureImportRequestBuilder.h>
#include <Oxygen/Cooker/Import/Internal/Utils/ImportSettingsUtils.h>
#include <Oxygen/Cooker/Import/Internal/Utils/JsonSchemaValidation.h>
#include <Oxygen/Cooker/Import/SourceMounts.h>
#include <Oxygen/Cooker/Import/TextureDescriptorImportRequestBuilder.h>
#include <Oxygen/Cooker/Import/TextureDescriptorImportSettings.h>
#include <Oxygen/Cooker/Import/TextureImportSettings.h>

namespace oxygen::content::import {

namespace {

  using internal::JsonSchemaValidationDiagnosticConfig;
  using internal::ValidateJsonSchemaWithDiagnostics;
  using nlohmann::json;
  using nlohmann::json_schema::json_validator;

  //! Locates a file the descriptor references, through the job's mounts.
  auto LocateReferenced(const MountTable& mounts,
    const std::filesystem::path& descriptor_path, const std::string& text,
    const std::string_view field, std::vector<ImportDiagnostic>& diagnostics)
    -> std::optional<std::string>
  {
    const auto located = mounts.LocateReference(descriptor_path,
      CommittedPath { .text = text, .json_pointer = field }, diagnostics);
    if (!located.has_value()) {
      return std::nullopt;
    }
    return located->file.string();
  }

  auto GetTextureDescriptorValidator() -> json_validator&
  {
    static auto validator = internal::MakeJsonSchemaValidator(
      json::parse(kTextureDescriptorSchema));
    return validator;
  }

  auto ValidateDescriptorSchema(const json& descriptor_doc,
    const std::filesystem::path& source_path,
    std::vector<ImportDiagnostic>& diagnostics) -> bool
  {
    const auto config = JsonSchemaValidationDiagnosticConfig {
      .validation_failed_code
      = import_codes::kTextureDescriptorSchemaValidationFailed,
      .validation_failed_prefix = "Texture descriptor validation failed: ",
      .validator_failure_code
      = import_codes::kTextureDescriptorSchemaValidatorFailure,
      .validator_failure_prefix
      = "Texture descriptor schema validator failed: ",
    };

    return ValidateJsonSchemaWithDiagnostics(GetTextureDescriptorValidator(),
      descriptor_doc, config,
      [&](const std::string_view code, const std::string& message,
        const std::string& json_pointer) -> void {
        diagnostics.push_back(ImportDiagnostic {
          .severity = ImportSeverity::kError,
          .code = std::string(code),
          .message = message,
          .source_path = source_path.string(),
          .json_pointer = json_pointer,
        });
      });
  }

  //! Applies the descriptor over `settings`; false when a file it references
  //! cannot be located.
  auto ApplyDescriptorSettings(const json& descriptor_doc,
    const std::filesystem::path& descriptor_path, const MountTable& mounts,
    TextureImportSettings& settings, std::vector<ImportDiagnostic>& diagnostics)
    -> bool
  {
    auto source = LocateReferenced(mounts, descriptor_path,
      descriptor_doc.at("source").get<std::string>(), "/source", diagnostics);
    if (!source.has_value()) {
      return false;
    }
    settings.source_path = std::move(*source);

    if (descriptor_doc.contains("sources")) {
      settings.sources.clear();
      const auto& sources = descriptor_doc.at("sources");
      for (std::size_t index = 0; index < sources.size(); ++index) {
        const auto& mapping_doc = sources.at(index);
        auto file = LocateReferenced(mounts, descriptor_path,
          mapping_doc.at("file").get<std::string>(),
          (json::json_pointer {} / "sources" / index / "file").to_string(),
          diagnostics);
        if (!file.has_value()) {
          return false;
        }
        auto mapping = TextureSourceMapping {};
        mapping.file = std::move(*file);
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
    return true;
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
      .code = std::string(import_codes::kTextureDescriptorInvalidJson),
      .message = failure.what(),
      .source_path = path.string(),
      .object_path = {},
    });
    return std::nullopt;
  }
  if (!ValidateDescriptorSchema(document, path, diagnostics)) {
    return std::nullopt;
  }
  CHECK_NOTNULL_F(mounts.get(),
    "A texture descriptor resolves its files through the job's mounts");
  auto effective = texture;
  if (!ApplyDescriptorSettings(
        document, path, *mounts, effective, diagnostics)) {
    return std::nullopt;
  }
  // The same destination-free option interpretation used by request
  // construction.
  auto tuning = ImportOptions::TextureTuning {};
  const auto first_refusal = diagnostics.size();
  if (!internal::MapSettingsToTuning(effective, tuning, diagnostics)) {
    // The schema validated what the descriptor states, which leaves its one
    // rule across fields; another refusal is about the job's own settings.
    for (auto index = first_refusal; index < diagnostics.size(); ++index) {
      auto& refusal = diagnostics[index];
      refusal.source_path = path.string();
      refusal.json_pointer
        = refusal.code == request_codes::kBc7QualityNeedsBc7Format
        ? "/output/bc7_quality"
        : "";
    }
    return std::nullopt;
  }
  return effective;
}

namespace internal {

  auto BuildTextureDescriptorRequest(
    const TextureDescriptorImportSettings& settings,
    std::vector<ImportDiagnostic>& diagnostics,
    std::shared_ptr<const CapturedInputSet> captured_inputs)
    -> std::optional<ImportRequest>
  {
    const auto path
      = std::filesystem::path(settings.descriptor_path).lexically_normal();
    auto document = ImportSourceDocument::Load(
      path, "texture", diagnostics, captured_inputs.get());
    if (!document.has_value()) {
      return std::nullopt;
    }
    const auto effective = settings.Prepare(document->text, diagnostics);
    if (!effective.has_value()) {
      return std::nullopt;
    }
    auto request = BuildTextureRequest(*effective, diagnostics);
    if (request.has_value()) {
      request->captured_inputs = std::move(captured_inputs);
      request->preparation_inputs.push_back(document->Observation());
    }
    return request;
  }

} // namespace internal
} // namespace oxygen::content::import
