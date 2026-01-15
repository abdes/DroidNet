//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/Import/Async/Detail/DefaultImportJob.h>
#include <Oxygen/Content/Import/Async/ImportSession.h>
#include <Oxygen/Content/Import/Layout.h>

namespace oxygen::content::import::detail {

namespace {

  [[nodiscard]] auto VirtualMountRootLeaf(const ImportRequest& request)
    -> std::filesystem::path
  {
    auto mount_root
      = std::filesystem::path(request.loose_cooked_layout.virtual_mount_root)
          .lexically_normal();
    auto leaf = mount_root.filename();
    if (!leaf.empty()) {
      return leaf;
    }

    // Defensive fallback: virtual mount roots are expected to end with a
    // directory name (e.g. "/.cooked").
    return std::filesystem::path(".cooked");
  }

  [[nodiscard]] auto ResolveCookedRootForRequest(const ImportRequest& request)
    -> std::filesystem::path
  {
    const auto mount_leaf = VirtualMountRootLeaf(request);

    std::filesystem::path base_root;
    if (request.cooked_root.has_value()) {
      base_root = *request.cooked_root;
    } else if (!request.source_path.empty()) {
      std::error_code ec;
      auto absolute_source = std::filesystem::absolute(request.source_path, ec);
      if (!ec) {
        base_root = absolute_source.parent_path();
      }
    }

    if (base_root.empty()) {
      base_root = std::filesystem::temp_directory_path();
    }

    // Ensure the cooked root ends with the virtual mount root leaf directory
    // (e.g. ".cooked"). This keeps incremental imports and updates stable.
    if (base_root.filename() == mount_leaf) {
      return base_root;
    }

    return base_root / mount_leaf;
  }

  auto EnsureCookedRoot(ImportRequest& request) -> void
  {
    auto cooked_root = ResolveCookedRootForRequest(request);
    request.cooked_root = cooked_root;

    std::error_code ec;
    std::filesystem::create_directories(cooked_root, ec);
    if (ec) {
      DLOG_F(WARNING, "Failed to create cooked root '{}': {}",
        cooked_root.string(), ec.message());
    }
  }

} // namespace

auto DefaultImportJob::ExecuteAsync() -> co::Co<ImportReport>
{
  DLOG_F(
    INFO, "Processing job {}: {}", JobId(), Request().source_path.string());

  // Ensure the job has a usable cooked root. Tests and callers may submit
  // requests without a cooked_root; the session needs a concrete directory
  // to write the container index.
  EnsureCookedRoot(Request());

  // Report starting progress.
  ReportProgress(ImportPhase::kParsing, 0.0f, "Starting import...");

  // Create per-job session.
  ImportSession session(Request(), FileWriter());

  // TODO: Phase 4.4+ - Backend integration.
  // For now, we only exercise session creation and finalization.

  ReportProgress(ImportPhase::kWriting, 0.9f, "Finalizing import...");
  auto report = co_await session.Finalize();

  ReportProgress(report.success ? ImportPhase::kComplete : ImportPhase::kFailed,
    1.0f, report.success ? "Import complete" : "Import failed");

  co_return report;
}

} // namespace oxygen::content::import::detail
