//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/TexturePipeline.h>
#include <Oxygen/Cooker/Import/TextureImportDesc.h>

namespace oxygen::content::import::adapters {

//! One texture use, before reading its external file or decoding embedded
//! bytes.
struct ModelTextureSource final {
  std::string source_id;
  std::string external_texture_id;
  const void* source_key = nullptr;
  std::filesystem::path source_path;
  bool embedded = false;
  TextureImportDesc desc;
  std::string packing_policy_id;
  TexturePipeline::OutputFormatPolicy output_format_policy
    = TexturePipeline::OutputFormatPolicy::kMaterialPreset;
  TexturePipeline::FailurePolicy failure_policy
    = TexturePipeline::FailurePolicy::kStrict;
};

struct ModelTexturePreparation final {
  std::vector<ModelTextureSource> sources;
  std::vector<ImportDiagnostic> diagnostics;
  bool success = true;
};

} // namespace oxygen::content::import::adapters
