//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "AssetKeyMap.h"

#include <fstream>
#include <iostream>
#include <stdexcept>

#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>

#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Clap/Fluent/CommandBuilder.h>
#include <Oxygen/Clap/Fluent/DSL.h>
#include <Oxygen/Clap/Option.h>
#include <Oxygen/Cooker/Tools/Inspector/AssetKeyMap_schema.h>
#include <Oxygen/Data/AssetKey.h>

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

auto RunAssetKeyMap(const AssetKeyMapOptions& options) -> int
{
  try {
    std::ifstream input(options.input, std::ios::binary);
    const auto request = nlohmann::json::parse(input);
    nlohmann::json_schema::json_validator validator;
    validator.set_root_schema(nlohmann::json::parse(kAssetKeyRequestSchema));
    validator.validate(request);
    auto assets = nlohmann::json::array();
    for (const auto& entry : request.at("virtual_paths")) {
      const auto path = entry.get<std::string>();
      assets.push_back({ { "virtual_path", path },
        { "asset_key",
          nostd::to_string(data::AssetKey::FromVirtualPath(path)) } });
    }
    const nlohmann::json report = { { "schema", "oxygen.asset-key-map.v1" },
      { "assets", std::move(assets) } };
    std::ofstream output(options.output, std::ios::binary | std::ios::trunc);
    output << report.dump(2);
    output.close();
    if (!output) {
      throw std::runtime_error("Could not write asset key map.");
    }
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "ERROR: " << error.what() << '\n';
    return 2;
  }
}

} // namespace oxygen::content::inspection
