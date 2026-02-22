//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <fstream>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Testing/GTest.h>

namespace oxygen::physics::test {
namespace {

  auto IsSourceFile(const std::filesystem::path& path) -> bool
  {
    const auto ext = path.extension().string();
    return ext == ".h" || ext == ".hpp" || ext == ".cpp";
  }

  auto IsUnderTests(const std::filesystem::path& path) -> bool
  {
    const auto normalized = path.generic_string();
    return normalized.find("/Test/") != std::string::npos;
  }

  auto HasRawUnitAxisLiteral(std::string_view line) -> bool
  {
    static const std::vector<std::regex> patterns {
      std::regex(
        R"((?:Vec3|JPH::Vec3)\s*\{\s*[-+]?1(?:\.0+)?F?\s*,\s*[-+]?0(?:\.0+)?F?\s*,\s*[-+]?0(?:\.0+)?F?\s*\})"),
      std::regex(
        R"((?:Vec3|JPH::Vec3)\s*\{\s*[-+]?0(?:\.0+)?F?\s*,\s*[-+]?1(?:\.0+)?F?\s*,\s*[-+]?0(?:\.0+)?F?\s*\})"),
      std::regex(
        R"((?:Vec3|JPH::Vec3)\s*\{\s*[-+]?0(?:\.0+)?F?\s*,\s*[-+]?0(?:\.0+)?F?\s*,\s*[-+]?1(?:\.0+)?F?\s*\})"),
    };

    const std::string s(line);
    for (const auto& pattern : patterns) {
      if (std::regex_search(s, pattern)) {
        return true;
      }
    }
    return false;
  }

} // namespace

NOLINT_TEST(PhysicsSourceContractTest, NoRawUnitAxisLiteralsInProductionSources)
{
  const auto test_dir = std::filesystem::path(__FILE__).parent_path();
  const auto physics_root = test_dir.parent_path();
  ASSERT_TRUE(std::filesystem::exists(physics_root));

  std::vector<std::string> violations {};
  for (const auto& entry :
    std::filesystem::recursive_directory_iterator(physics_root)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    const auto& path = entry.path();
    if (!IsSourceFile(path) || IsUnderTests(path)) {
      continue;
    }

    std::ifstream input(path);
    if (!input.is_open()) {
      continue;
    }

    std::string line {};
    size_t line_no = 0;
    while (std::getline(input, line)) {
      ++line_no;
      if (!HasRawUnitAxisLiteral(line)) {
        continue;
      }
      violations.push_back(
        path.generic_string() + ":" + std::to_string(line_no) + ": " + line);
    }
  }

  if (!violations.empty()) {
    for (const auto& violation : violations) {
      ADD_FAILURE() << violation;
    }
  }
}

} // namespace oxygen::physics::test
