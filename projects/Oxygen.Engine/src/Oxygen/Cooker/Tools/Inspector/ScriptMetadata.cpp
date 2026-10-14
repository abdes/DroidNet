//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

#include "ScriptMetadata.h"
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Clap/Fluent/CommandBuilder.h>
#include <Oxygen/Clap/Fluent/DSL.h>
#include <Oxygen/Clap/Option.h>
#include <Oxygen/Content/LoaderContext.h>
#include <Oxygen/Content/Loaders/ScriptLoader.h>
#include <Oxygen/Cooker/Loose/Inspection.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/ScriptAsset.h>
#include <Oxygen/Serio/FileStream.h>
#include <Oxygen/Serio/Reader.h>

namespace oxygen::content::inspection {

namespace {

  //! Version of the `oxygen.cooked-scripts` report.
  constexpr int kScriptMetadataSchemaVersion = 1;

  //! One declared parameter: its name, type, and the default or asset kind
  //! the cooked declaration carries. Node parameters have neither.
  auto ToJson(const data::ScriptAsset::DefaultParameterEntry& entry)
    -> nlohmann::json
  {
    using nlohmann::json;
    auto row = json {
      { "name", std::string(entry.key) },
    };
    std::visit(
      [&row]<typename T>(const T& value) -> void {
        if constexpr (std::is_same_v<T, bool>) {
          row["type"] = "bool";
          row["default"] = value;
        } else if constexpr (std::is_same_v<T, int32_t>) {
          row["type"] = "int32";
          row["default"] = value;
        } else if constexpr (std::is_same_v<T, float>) {
          row["type"] = "float";
          row["default"] = value;
        } else if constexpr (std::is_same_v<T, std::string>) {
          row["type"] = "string";
          row["default"] = value;
        } else if constexpr (std::is_same_v<T, Vec2>) {
          row["type"] = "vec2";
          row["default"] = json::array({ value.x, value.y });
        } else if constexpr (std::is_same_v<T, Vec3>) {
          row["type"] = "vec3";
          row["default"] = json::array({ value.x, value.y, value.z });
        } else if constexpr (std::is_same_v<T, Vec4>) {
          row["type"] = "vec4";
          row["default"] = json::array({ value.x, value.y, value.z, value.w });
        } else if constexpr (std::is_same_v<T, data::ScriptAssetParam>) {
          row["type"] = "asset";
          row["asset_type"] = static_cast<uint8_t>(value.asset_type);
        } else {
          static_assert(std::is_same_v<T, data::ScriptNodeParam>);
          row["type"] = "node";
        }
      },
      entry.value.get());
    return row;
  }

} // namespace

auto BuildScriptMetadataCommand(ScriptMetadataOptions& options)
  -> std::shared_ptr<clap::Command>
{
  auto root = clap::Option::Positional("cooked_root")
                .About("Loose cooked root directory")
                .Required()
                .WithValue<std::string>()
                .StoreTo(&options.cooked_root)
                .Build();
  auto output = clap::Option::WithKey("output")
                  .Long("output")
                  .About("Destination JSON script declarations")
                  .Required()
                  .WithValue<std::string>()
                  .StoreTo(&options.output)
                  .Build();
  auto virtual_path = clap::Option::WithKey("virtual-path")
                        .Long("virtual-path")
                        .About("Inspect only this native script virtual path")
                        .WithValue<std::string>()
                        .StoreTo(&options.virtual_path)
                        .Build();
  return clap::CommandBuilder("scripts")
    .About("Inspect the parameters cooked scripts declare.")
    .WithPositionalArguments(root)
    .WithOption(std::move(output))
    .WithOption(std::move(virtual_path));
}

auto RunScriptMetadataReport(const ScriptMetadataOptions& options) -> int
{
  using nlohmann::json;
  try {
    const auto root
      = std::filesystem::absolute(options.cooked_root).lexically_normal();
    const auto output_path
      = std::filesystem::absolute(options.output).lexically_normal();
    if (output_path == root / "container.index.bin") {
      throw std::runtime_error(
        "Script metadata output must not replace its source index.");
    }
    lc::Inspection inspection;
    inspection.LoadFromRoot(root);
    auto scripts = json::array();
    for (const auto& entry : inspection.Assets()) {
      const auto relative
        = std::filesystem::path(entry.descriptor_relpath).lexically_normal();
      if (output_path == root / relative) {
        throw std::runtime_error(
          "Script metadata output must not replace a source descriptor.");
      }
      if (entry.asset_type != static_cast<uint8_t>(data::AssetType::kScript)) {
        continue;
      }
      if (!options.virtual_path.empty()
        && entry.virtual_path != options.virtual_path) {
        continue;
      }
      try {
        if (relative.empty() || relative.has_root_path()
          || *relative.begin() == "..") {
          throw std::runtime_error(
            "Descriptor path must stay within its cooked root.");
        }
        serio::FileStream<> stream(root / relative, std::ios::in);
        const auto size = stream.Size();
        if (!size || *size != entry.descriptor_size) {
          throw std::runtime_error(
            "Script descriptor size does not match its index entry.");
        }
        serio::Reader reader(stream);
        const LoaderContext context {
          .current_asset_key = entry.key,
          .desc_reader = &reader,
          .work_offline = true,
          .parse_only = true,
        };
        const auto script = loaders::LoadScriptAsset(context);
        auto parameters = json::array();
        for (const auto& parameter : script->Parameters()) {
          parameters.push_back(ToJson(parameter));
        }
        scripts.push_back({
          { "virtual_path", entry.virtual_path },
          { "asset_key", nostd::to_string(entry.key) },
          { "parameters", std::move(parameters) },
        });
      } catch (const std::exception& error) {
        throw std::runtime_error("invalid script '" + entry.virtual_path + "' ("
          + nostd::to_string(entry.key) + "): " + error.what());
      }
    }
    const auto report = json {
      { "schema_version", kScriptMetadataSchemaVersion },
      { "scripts", std::move(scripts) },
    };
    // An output stream truncates the file it opens.
    std::ofstream output(output_path, std::ios::binary);
    output << report.dump(2) << '\n';
    output.close();
    if (!output) {
      throw std::runtime_error("Could not write script metadata report.");
    }
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "ERROR: " << error.what() << '\n';
    return 2;
  }
}

} // namespace oxygen::content::inspection
