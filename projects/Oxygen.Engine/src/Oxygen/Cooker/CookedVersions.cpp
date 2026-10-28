//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Cooker/CookedVersions.h>
#include <Oxygen/Cooker/Import/Importer.h>
#include <Oxygen/Data/PakFormatVersions.h>

namespace oxygen::content {

namespace {

  using data::pak::version::NamedVersion;

  struct Group {
    std::string_view name;
    std::vector<NamedVersion> versions;
  };

  [[nodiscard]] auto GroupsOf(const CookedVersionsReport& report)
    -> std::vector<Group>
  {
    using data::pak::version::kAssets;
    using data::pak::version::kFormats;
    auto groups = std::vector<Group> {
      { .name = "formats", .versions = { kFormats.begin(), kFormats.end() } },
      { .name = "assets", .versions = { kAssets.begin(), kAssets.end() } },
    };
    if (report.importers) {
      auto& importers = groups.emplace_back(Group { .name = "importers" });
      for (const auto importer : import::kImporters) {
        importers.versions.push_back(NamedVersion {
          .name = nostd::to_string(importer),
          .value = import::RevisionOf(importer),
        });
      }
    }
    return groups;
  }

  auto PrintJson(std::ostream& out, const CookedVersionsReport& report,
    const std::span<const Group> groups) -> void
  {
    auto document = nlohmann::ordered_json {
      { "program", report.program },
      { "version", report.version },
    };
    for (const auto& group : groups) {
      auto versions = nlohmann::ordered_json::object();
      for (const auto& version : group.versions) {
        versions[std::string(version.name)] = version.value;
      }
      document[std::string(group.name)] = std::move(versions);
    }
    out << document.dump(2) << '\n';
  }

  auto PrintText(std::ostream& out, const CookedVersionsReport& report,
    const std::span<const Group> groups) -> void
  {
    out << fmt::format("{} version {}\n", report.program, report.version);
    for (const auto& group : groups) {
      auto width = std::size_t { 0 };
      for (const auto& version : group.versions) {
        width = std::max(width, version.name.size());
      }
      out << fmt::format("\n{}\n", group.name);
      for (const auto& version : group.versions) {
        out << fmt::format(
          "  {:<{}}  {}\n", version.name, width, version.value);
      }
    }
  }

} // namespace

auto PrintCookedVersions(std::ostream& out, const CookedVersionsReport& report)
  -> void
{
  const auto groups = GroupsOf(report);
  if (report.json) {
    PrintJson(out, report, groups);
  } else {
    PrintText(out, report, groups);
  }
}

} // namespace oxygen::content
