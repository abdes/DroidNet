//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Cooker/Import/ImportReport.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/SceneImportSettings.h>
#include <Oxygen/Cooker/api_export.h>

namespace oxygen::content::import {

namespace detail {
  class ImportJob;
}

//! One isolated attempt to publish a retained, self-contained model import.
//! The authored record selects provenance and generation in one atomic commit.
class RetainedModelImport final {
public:
  //! Native recipe serialization shared by UI and command-line hosts.
  OXGN_COOK_NDAPI static auto MakeRecipe(const SceneImportSettings& settings,
    const LooseCookedLayout& layout) -> std::string;

  //! Stable record location inside Content/imports for the selected source.
  OXGN_COOK_NDAPI static auto RecordPath(
    const std::filesystem::path& content_root,
    const std::filesystem::path& source_path) -> std::filesystem::path;

  //! Saves current authored recipe values, retaining existing identity and
  //! selected generation. All writes stay beneath content_root.
  OXGN_COOK_API static auto SaveRecipe(const std::filesystem::path& record_path,
    const std::filesystem::path& content_root, std::string_view recipe_json)
    -> void;

  //! Apply a single-job native manifest. Relative sources resolve beside that
  //! manifest; format and recipe validation precede any authored-record write.
  OXGN_COOK_API static auto SaveRecipeFile(
    const std::filesystem::path& record_path,
    const std::filesystem::path& content_root,
    const std::filesystem::path& recipe_path, ImportFormat expected_format)
    -> void;

  //! Reads a baseline and reserves a private generation. No source bytes or
  //! prior cooked output are copied. Concurrent attempts have distinct roots.
  OXGN_COOK_NDAPI static auto Prepare(const std::filesystem::path& record_path)
    -> std::shared_ptr<RetainedModelImport>;

  OXGN_COOK_API ~RetainedModelImport();
  OXYGEN_MAKE_NON_COPYABLE(RetainedModelImport)
  OXYGEN_MAKE_NON_MOVABLE(RetainedModelImport)

  OXGN_COOK_NDAPI auto Request() const -> const ImportRequest&;
  OXGN_COOK_NDAPI auto PreviousGeneration() const
    -> std::optional<std::filesystem::path>;

  //! Performs native root and provenance validation before the short commit
  //! section. Call from the import worker pool, never a rendering callback.
  OXGN_COOK_API auto ValidateCandidate(const ImportReport& report) -> void;

  //! Atomically selects the validated candidate if the authored
  //! record still matches the baseline. Throws without changing the selected
  //! record on validation, cancellation, lock or baseline failure.
  OXGN_COOK_API auto Publish(
    ImportReport& report, std::stop_token stop_token = {}) -> void;

  //! Removes unselected generations whose writer/reader leases have expired.
  //! Call on a worker; selected generations and active attempts are retained.
  OXGN_COOK_NDAPI static auto ReclaimUnusedGenerations(
    const std::filesystem::path& record_path) -> size_t;

  //! Resolves the selected immutable root without creating another attempt.
  OXGN_COOK_NDAPI static auto SelectedGeneration(
    const std::filesystem::path& record_path)
    -> std::optional<std::filesystem::path>;

private:
  friend class AsyncImportService;
  friend class detail::ImportJob;
  auto ClaimSubmission() -> void;
  auto ReclaimUnusedGenerations() const -> size_t;
  [[nodiscard]] auto AllowsWriting(const std::filesystem::path& root) const
    -> bool;
  struct State;
  explicit RetainedModelImport(std::unique_ptr<State> state);
  std::unique_ptr<State> state_;
};

} // namespace oxygen::content::import
