//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <Oxygen/Composition/TypedObject.h>
#include <Oxygen/Content/Import/Internal/ImportJob.h>
#include <Oxygen/Content/Import/ScratchImage.h>
#include <Oxygen/Content/Import/TextureImportTypes.h>
#include <Oxygen/Content/Import/TextureSourceAssembly.h>

namespace oxygen::content::import {
class ImportSession;
class TexturePipeline;
} // namespace oxygen::content::import

namespace oxygen::content::import::detail {

//! Standalone texture import job.
/*!
 Imports a single texture asset using the async texture pipeline and emits it
 via the texture emitter. This job is intended for direct texture imports
 outside scene formats.
*/
class TextureImportJob final : public ImportJob {
  OXYGEN_TYPED(TextureImportJob)
public:
  using ImportJob::ImportJob;

private:
  //! Decoded texture source data.
  struct TextureSource {
    bool success = false;
    std::string source_id;
    std::shared_ptr<std::vector<std::byte>> bytes;
    std::optional<ScratchImage> image;
    std::optional<TextureSourceSet> source_set;
    std::optional<ScratchImageMeta> meta;
    std::optional<bool> is_hdr_input;
    bool prevalidated = false;
    std::optional<std::chrono::microseconds> io_duration;
    std::optional<std::chrono::microseconds> decode_duration;
  };

  struct CookedTextureResult {
    std::optional<CookedTexturePayload> payload;
    std::optional<std::chrono::microseconds> decode_duration;
    bool used_fallback = false;
  };

  [[nodiscard]] auto ExecuteAsync() -> co::Co<ImportReport> override;

  [[nodiscard]] auto LoadSource(ImportSession& session)
    -> co::Co<TextureSource>;

  [[nodiscard]] auto CookTexture(TextureSource& source, ImportSession& session,
    TexturePipeline& pipeline) -> co::Co<CookedTextureResult>;

  [[nodiscard]] auto EmitTexture(
    CookedTexturePayload cooked, ImportSession& session) -> co::Co<bool>;

  [[nodiscard]] auto FinalizeSession(ImportSession& session)
    -> co::Co<ImportReport>;
};

} // namespace oxygen::content::import::detail
