//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <expected>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <fmt/base.h>
#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <Oxygen/Base/Filesystem.h>
#include <Oxygen/Clap/Command.h>
#include <Oxygen/Clap/Fluent/CommandBuilder.h>
#include <Oxygen/Clap/Fluent/DSL.h>
#include <Oxygen/Clap/Option.h>
#include <Oxygen/Cooker/Import/Internal/LooseCookedWriter.h>
#include <Oxygen/Cooker/Tools/ImportTool/RemoveCookedCommand.h>
#include <Oxygen/Data/SourceKey.h>

namespace oxygen::content::import::tool {

namespace {

  auto ReadStrings(const nlohmann::json& request, const char* name)
    -> std::vector<std::string>
  {
    const auto values = request.find(name);
    if (values == request.end()) {
      return {};
    }
    if (!values->is_array()) {
      throw std::runtime_error(
        fmt::format("Removal request '{}' must be an array", name));
    }
    return values->get<std::vector<std::string>>();
  }

  //! The index identity to publish, when the request names one.
  auto ReadSourceKey(const nlohmann::json& request)
    -> std::optional<data::SourceKey>
  {
    const auto value = request.find("source_key");
    if (value == request.end()) {
      return std::nullopt;
    }
    const auto parsed = data::SourceKey::FromString(value->get<std::string>());
    if (!parsed) {
      throw std::runtime_error(
        "Removal request 'source_key' must be a canonical UUIDv7");
    }
    return parsed.value();
  }

} // namespace

auto RemoveCookedCommand::Name() const -> std::string_view
{
  return "remove-cooked";
}

auto RemoveCookedCommand::BuildCommand() -> std::shared_ptr<clap::Command>
{
  auto root = clap::Option::Positional("cooked_root")
                .About("Loose cooked root directory")
                .Required()
                .WithValue<std::string>()
                .StoreTo(&cooked_root_)
                .Build();
  auto input
    = clap::Option::WithKey("input")
        .Long("input")
        .About("JSON request listing 'virtual_paths' and 'auxiliary_files', "
               "with an optional 'source_key' for the updated index")
        .Required()
        .WithValue<std::string>()
        .StoreTo(&request_path_)
        .Build();
  return clap::CommandBuilder("remove-cooked")
    .About("Remove cooked assets and auxiliary files without cooking")
    .WithPositionalArguments(root)
    .WithOption(std::move(input));
}

auto RemoveCookedCommand::Execute() -> std::expected<void, std::error_code>
{
  auto stream = std::ifstream(base::ToNativePath(request_path_));
  if (!stream) {
    throw std::runtime_error(
      "Failed to open the removal request: " + request_path_);
  }
  const auto request = nlohmann::json::parse(stream);
  const auto virtual_paths = ReadStrings(request, "virtual_paths");
  const auto auxiliary_files = ReadStrings(request, "auxiliary_files");
  if (virtual_paths.empty() && auxiliary_files.empty()) {
    throw std::runtime_error("The removal request lists nothing to remove");
  }

  LooseCookedWriter writer { std::filesystem::path(cooked_root_) };
  writer.SetSourceKey(ReadSourceKey(request));
  for (const auto& virtual_path : virtual_paths) {
    writer.RemoveAssetDescriptor(virtual_path);
  }
  for (const auto& relpath : auxiliary_files) {
    writer.RemoveAuxiliaryFile(relpath);
  }
  const auto result = writer.Finish();
  fmt::print("Removed {} cooked asset(s) and {} auxiliary file(s), deleting "
             "{} file(s): {}\n",
    virtual_paths.size(), auxiliary_files.size(), result.removed_files.size(),
    cooked_root_);
  return {};
}

} // namespace oxygen::content::import::tool
