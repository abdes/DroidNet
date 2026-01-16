//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <chrono>
#include <optional>
#include <string>

#include <Oxygen/Composition/TypedObject.h>
#include <Oxygen/Content/Import/Async/Detail/ImportJob.h>
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
    std::optional<ScratchImage> image;
    std::optional<TextureSourceSet> source_set;
    std::optional<ScratchImageMeta> meta;
    bool prevalidated = false;
    std::optional<std::chrono::microseconds> io_duration;
    std::optional<std::chrono::microseconds> decode_duration;
  };

  [[nodiscard]] auto ExecuteAsync() -> co::Co<ImportReport> override;

  [[nodiscard]] auto LoadSource(ImportSession& session)
    -> co::Co<TextureSource>;

  [[nodiscard]] auto CookTexture(TextureSource& source, ImportSession& session,
    TexturePipeline& pipeline) -> co::Co<std::optional<CookedTexturePayload>>;

  [[nodiscard]] auto EmitTexture(
    CookedTexturePayload cooked, ImportSession& session) -> co::Co<bool>;

  [[nodiscard]] auto FinalizeSession(ImportSession& session)
    -> co::Co<ImportReport>;
};

} // namespace oxygen::content::import::detail
