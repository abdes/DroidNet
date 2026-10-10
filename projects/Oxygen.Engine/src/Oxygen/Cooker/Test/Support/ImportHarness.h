//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>

#include <Oxygen/Cooker/Import/AsyncImportService.h>
#include <Oxygen/Cooker/Import/ImportReport.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Loose/Inspection.h>
#include <Oxygen/Cooker/Test/Support/TempDir.h>

namespace oxygen::cooker::test {

//! Submits and waits at most `timeout`. Returns nullopt and records a
//! non-fatal failure if submission is rejected or the timeout expires.
//! State shared with the completion callback lives in a shared_ptr, so a late
//! callback after a timeout never touches a dead stack frame.
[[nodiscard]] auto SubmitAndWait(content::import::AsyncImportService& service,
  content::import::ImportRequest request,
  std::chrono::seconds timeout = std::chrono::seconds { 120 })
  -> std::optional<content::import::ImportReport>;

//! TempDir-backed fixture owning a lazily created AsyncImportService.
class ImportServiceTest : public TempDirTest {
protected:
  //! Override to change worker counts etc. Default: the values most job tests
  //! use today.
  [[nodiscard]] virtual auto ServiceConfig() const
    -> content::import::AsyncImportService::Config;
  //! The fixture service, created on first use and stopped on teardown.
  [[nodiscard]] auto Service() -> content::import::AsyncImportService&;
  //! TempDir() / "cooked".
  [[nodiscard]] auto CookedRoot() const -> std::filesystem::path;
  //! SubmitAndWait(Service(), ...).
  [[nodiscard]] auto Import(content::import::ImportRequest request)
    -> std::optional<content::import::ImportReport>;
  //! Loads the loose-cooked index at `cooked_root`.
  [[nodiscard]] static auto LoadInspection(
    const std::filesystem::path& cooked_root) -> content::lc::Inspection;

  void TearDown() override;

private:
  std::unique_ptr<content::import::AsyncImportService> service_;
};

} // namespace oxygen::cooker::test
