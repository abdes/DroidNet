//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <exception>
#include <expected>
#include <filesystem>
#include <fstream>
#include <ios>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include <Oxygen/Base/Filesystem.h>
#include <Oxygen/Clap/Command.h>
#include <Oxygen/Clap/Fluent/CommandBuilder.h>
#include <Oxygen/Clap/Fluent/DSL.h>
#include <Oxygen/Clap/Option.h>
#include <Oxygen/Cooker/Import/CapturedInputSet.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportManifest.h>
#include <Oxygen/Cooker/Import/ImportSourceAnalysis.h>
#include <Oxygen/Cooker/Import/ImportSourceDocument.h>
#include <Oxygen/Cooker/Import/Internal/Utils/DescriptorDocument.h>
#include <Oxygen/Cooker/Tools/ImportTool/MessageWriter.h>
#include <Oxygen/Cooker/Tools/ImportTool/SourceAnalysisCommand.h>
#include <Oxygen/Core/Version.h>

namespace oxygen::content::import::tool {

auto SourceAnalysisCommand::Name() const -> std::string_view
{
  return "analyze-sources";
}

auto SourceAnalysisCommand::BuildCommand() -> std::shared_ptr<clap::Command>
{
  auto manifest = clap::Option::WithKey("manifest")
                    .About("Native import manifest to analyze")
                    .Long("manifest")
                    .Required()
                    .WithValue<std::string>()
                    .StoreTo(&manifest_path_)
                    .Build();
  auto root = clap::Option::WithKey("root")
                .About("The project root, replacing the manifest's "
                       "project_root")
                .Long("root")
                .WithValue<std::string>()
                .StoreTo(&root_path_)
                .Build();
  auto mounts = clap::Option::WithKey("mounts")
                  .About("A mounts file applied above every .mounts.json")
                  .Long("mounts")
                  .WithValue<std::string>()
                  .StoreTo(&mounts_path_)
                  .Build();
  auto report = clap::Option::WithKey("report")
                  .About("Destination JSON analysis report")
                  .Long("report")
                  .Required()
                  .WithValue<std::string>()
                  .StoreTo(&report_path_)
                  .Build();
  auto captures
    = clap::Option::WithKey("captured-inputs")
        .About("Captured input map; logical source identities remain unchanged")
        .Long("captured-inputs")
        .WithValue<std::string>()
        .StoreTo(&captured_inputs_path_)
        .Build();
  return clap::CommandBuilder("analyze-sources")
    .About("Discover native source dependencies and outputs without cooking")
    .WithOption(std::move(manifest))
    .WithOption(std::move(root))
    .WithOption(std::move(mounts))
    .WithOption(std::move(report))
    .WithOption(std::move(captures));
}

auto SourceAnalysisCommand::Execute() -> std::expected<void, std::error_code>
{
  const auto manifest_path = std::filesystem::path(manifest_path_);
  auto options = ImportManifestLoadOptions {};
  if (!root_path_.empty()) {
    options.project_root = std::filesystem::path(root_path_);
  }
  if (!mounts_path_.empty()) {
    options.mounts_file = std::filesystem::path(mounts_path_);
  }
  const auto report_error = [this](const std::string_view message) -> void {
    if (!message.empty() && global_options_->writer != nullptr) {
      global_options_->writer->Error(message);
    }
  };
  const auto destination = std::filesystem::path(report_path_);
  const auto aliases = [&](const std::filesystem::path& source) -> bool {
    if (source.empty()) {
      return false;
    }
    return base::PathIdentityKey(std::filesystem::weakly_canonical(source))
      == base::PathIdentityKey(std::filesystem::weakly_canonical(destination))
      || (std::filesystem::exists(source)
        && std::filesystem::exists(destination)
        && std::filesystem::equivalent(source, destination));
  };
  if (aliases(manifest_path) || aliases(captured_inputs_path_)) {
    return std::unexpected(std::make_error_code(std::errc::invalid_argument));
  }
  // Writes the report, refusing one that would overwrite a file it read.
  const auto write_report = [&](const ImportSourceAnalysis& report)
    -> std::expected<void, std::error_code> {
    for (const auto& job : report.jobs) {
      if (aliases(job.source_path)
        || std::ranges::any_of(job.accessed_paths, aliases)
        || std::ranges::any_of(job.files,
          [&](const auto& file) -> bool { return aliases(file.path); })) {
        return std::unexpected(
          std::make_error_code(std::errc::invalid_argument));
      }
    }
    if (!destination.parent_path().empty()) {
      std::filesystem::create_directories(destination.parent_path());
    }
    auto output
      = std::ofstream(base::ToNativePath(destination), std::ios::binary);
    output << report.ToJson() << '\n';
    output.close();
    if (!output) {
      return std::unexpected(std::make_error_code(std::errc::io_error));
    }
    return {};
  };

  auto manifest_diagnostics = std::vector<ImportDiagnostic> {};
  const auto manifest
    = ImportManifest::Load(manifest_path, options, manifest_diagnostics);
  if (!manifest.has_value()) {
    // A refused manifest is reported too, with its errors and no jobs.
    auto errors = std::ostringstream {};
    internal::WriteDiagnostics(manifest_diagnostics, errors);
    report_error(errors.view());
    const auto written = write_report(ImportSourceAnalysis {
      .producer_version = std::string { oxygen::version::Version() },
      .jobs = {},
      .diagnostics = std::move(manifest_diagnostics),
      .complete = false,
    });
    if (!written.has_value()) {
      return written;
    }
    return std::unexpected(std::make_error_code(std::errc::invalid_argument));
  }
  auto captured_inputs = std::shared_ptr<const CapturedInputSet> {};
  if (!captured_inputs_path_.empty()) {
    auto errors = std::ostringstream {};
    try {
      const auto document = ImportSourceDocument::Load(
        captured_inputs_path_, "capture map", errors);
      if (!document.has_value()) {
        report_error(errors.view());
        return std::unexpected(
          std::make_error_code(std::errc::invalid_argument));
      }
      captured_inputs = CapturedInputSet::Parse(document->text);
    } catch (const std::exception& error) {
      report_error(fmt::format("Invalid captured inputs: {}", error.what()));
      return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    }
  }
  if (captured_inputs) {
    for (const auto& input : captured_inputs->Inputs()) {
      if (aliases(input.logical_path)
        || (input.file.has_value() && aliases(input.file->path))) {
        return std::unexpected(
          std::make_error_code(std::errc::invalid_argument));
      }
    }
  }
  for (const auto& job : manifest->jobs) {
    if (aliases(job.SourcePath())) {
      return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    }
  }
  auto report = manifest->AnalyzeSources({}, captured_inputs);
  report.diagnostics = std::move(manifest_diagnostics);
  if (const auto written = write_report(report); !written.has_value()) {
    return written;
  }
  if (!report.complete) {
    return std::unexpected(std::make_error_code(std::errc::invalid_argument));
  }
  return {};
}

} // namespace oxygen::content::import::tool
