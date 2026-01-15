//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Composition/TypedObject.h>
#include <Oxygen/Content/Import/Async/Detail/ImportJob.h>

namespace oxygen::content::import {
class ImportSession;
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
  //! Placeholder for decoded texture source bytes.
  struct TextureSource {
    bool success = true;
  };

  [[nodiscard]] auto ExecuteAsync() -> co::Co<ImportReport> override;

  [[nodiscard]] auto LoadSource(ImportSession& session)
    -> co::Co<TextureSource>;

  [[nodiscard]] auto CookTexture(
    const TextureSource& source, ImportSession& session) -> co::Co<bool>;

  [[nodiscard]] auto EmitTexture(
    const TextureSource& source, ImportSession& session) -> co::Co<bool>;

  [[nodiscard]] auto FinalizeSession(ImportSession& session)
    -> co::Co<ImportReport>;
};

} // namespace oxygen::content::import::detail
