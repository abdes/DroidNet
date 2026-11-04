//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include "AssetKeyMap.h"
#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Clap/Fluent/CommandBuilder.h>
#include <Oxygen/Clap/Fluent/DSL.h>
#include <Oxygen/Clap/Option.h>
#include <Oxygen/Cooker/Import/ImportLimits.h>
#include <Oxygen/Cooker/Import/Internal/Utils/JsonSchemaValidation.h>
#include <Oxygen/Cooker/Tools/Common/ReportFile.h>
#include <Oxygen/Cooker/Tools/Common/ToolCommand.h>
#include <Oxygen/Cooker/Tools/Inspector/AssetKeyMap_schema.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Serio/AtomicFile.h>
#include <Oxygen/Serio/ByteBuffer.h>

namespace oxygen::content::inspection {

auto BuildAssetKeyMapCommand(AssetKeyMapOptions& options)
  -> std::shared_ptr<clap::Command>
{
  auto input = clap::Option::WithKey("input")
                 .Long("input")
                 .About("JSON request containing canonical virtual paths")
                 .Required()
                 .WithValue<std::string>()
                 .StoreTo(&options.input)
                 .Build();
  auto output = clap::Option::WithKey("output")
                  .Long("output")
                  .About("Destination JSON asset key map")
                  .Required()
                  .WithValue<std::string>()
                  .StoreTo(&options.output)
                  .Build();
  return clap::CommandBuilder("asset-keys")
    .About(
      "Resolve virtual asset identities without cooking or starting an engine.")
    .WithOption(std::move(input))
    .WithOption(std::move(output));
}

auto RunAssetKeyMap(const AssetKeyMapOptions& options) -> toolkit::ToolResult
{
  const auto output_path = toolkit::ResolveReportPath(options.output);
  if (auto safe = toolkit::CheckToolReportDestination(output_path,
        {
          .inputs = { std::filesystem::path(options.input) },
          .cooked_roots = {},
        });
    !safe) {
    return safe;
  }
  const auto bytes = serio::ReadPublishedFile(
    std::filesystem::path(options.input), import::kMaxImportMetadataBytes);
  if (!bytes) {
    throw std::invalid_argument("Could not read asset key request "
      + options.input + ": " + bytes.error().message());
  }
  const auto text = bytes->Unsigned();
  const auto request
    = nlohmann::json::parse(text.begin(), text.end(), nullptr, false);
  if (request.is_discarded()) {
    throw std::invalid_argument(
      "Asset key request " + options.input + " is not JSON");
  }
  nlohmann::json_schema::json_validator validator;
  validator.set_root_schema(nlohmann::json::parse(kAssetKeyRequestSchema));
  import::internal::ValidateJsonSchema(
    validator, request, "Asset key request " + options.input);
  auto assets = nlohmann::json::array();
  for (const auto& entry : request.at("virtual_paths")) {
    const auto path = entry.get<std::string>();
    assets.push_back({
      { "virtual_path", path },
      {
        "asset_key",
        nostd::to_string(data::AssetKey::FromVirtualPath(path)),
      },
    });
  }
  const nlohmann::json report = {
    { "schema", kAssetKeyMapSchema },
    { "assets", std::move(assets) },
  };
  return toolkit::PublishToolReport(output_path, report.dump(2) + '\n');
}

} // namespace oxygen::content::inspection
