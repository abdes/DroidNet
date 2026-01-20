//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Composition/TypedObject.h>
#include <Oxygen/Content/Import/Internal/ImportJob.h>

namespace oxygen::content::import {
class ImportSession;
} // namespace oxygen::content::import

namespace oxygen::content::import::detail {

//! Standalone audio import job.
/*!
 Imports a single audio asset using the async audio pipeline and emits it via
 a dedicated audio emitter (introduced in Phase 6).
*/
class AudioImportJob final : public ImportJob {
  OXYGEN_TYPED(AudioImportJob)
public:
  using ImportJob::ImportJob;

private:
  //! Placeholder for decoded audio source bytes.
  struct AudioSource {
    bool success = true;
  };

  [[nodiscard]] auto ExecuteAsync() -> co::Co<ImportReport> override;

  [[nodiscard]] auto LoadSource(ImportSession& session) -> co::Co<AudioSource>;

  [[nodiscard]] auto CookAudio(
    const AudioSource& source, ImportSession& session) -> co::Co<bool>;

  [[nodiscard]] auto EmitAudio(
    const AudioSource& source, ImportSession& session) -> co::Co<bool>;

  [[nodiscard]] auto FinalizeSession(ImportSession& session)
    -> co::Co<ImportReport>;
};

} // namespace oxygen::content::import::detail
