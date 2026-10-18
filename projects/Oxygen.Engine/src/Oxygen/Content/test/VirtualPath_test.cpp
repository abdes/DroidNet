//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstddef>
#include <fstream>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <Oxygen/Content/VirtualPath.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::content::VirtualPathError;
using oxygen::content::VirtualPathRuleSet;

//! The case table shared with the managed implementation of the rules.
auto LoadCases() -> const nlohmann::json&
{
  static const auto cases = [] -> nlohmann::json {
    auto stream = std::ifstream { OXYGEN_CONTENT_VIRTUAL_PATH_CASES };
    if (!stream) {
      throw std::runtime_error("VirtualPathCases.json is missing");
    }
    return nlohmann::json::parse(stream);
  }();
  return cases;
}

auto FromHex(const std::string_view hex) -> std::string
{
  auto bytes = std::string {};
  for (auto index = std::size_t { 0 }; index + 1 < hex.size(); index += 2) {
    bytes.push_back(static_cast<char>(
      std::stoi(std::string { hex.substr(index, 2) }, nullptr, 16)));
  }
  return bytes;
}

auto CodeOf(const std::optional<VirtualPathError> error) -> nlohmann::json
{
  return error.has_value() ? nlohmann::json(to_string(*error))
                           : nlohmann::json(nullptr);
}

constexpr auto kAllErrors = std::array {
  VirtualPathError::kEmpty,
  VirtualPathError::kNotAbsolute,
  VirtualPathError::kTrailingSlash,
  VirtualPathError::kEmptySegment,
  VirtualPathError::kDotSegment,
  VirtualPathError::kInvalidUtf8,
  VirtualPathError::kControlCharacter,
  VirtualPathError::kReservedCharacter,
  VirtualPathError::kUnassignedCodePoint,
  VirtualPathError::kNotNfc,
  VirtualPathError::kEdgeSpace,
  VirtualPathError::kTrailingDot,
  VirtualPathError::kDeviceName,
  VirtualPathError::kSegmentTooLong,
  VirtualPathError::kInvalidMountName,
  VirtualPathError::kMountOnly,
  VirtualPathError::kMissingExtension,
  VirtualPathError::kDerivedContent,
};
static_assert(kAllErrors.size()
  == static_cast<std::size_t>(VirtualPathError::kDerivedContent) + 1);

//! Each segment of the table gets its refusal code, or none.
NOLINT_TEST(VirtualPathCasesTest, SegmentsFollowTheCaseTable)
{
  for (const auto& entry : LoadCases().at("segments")) {
    const auto segment = entry.contains("segment_hex")
      ? FromHex(entry.at("segment_hex").get<std::string>())
      : entry.at("segment").get<std::string>();

    const auto actual
      = CodeOf(oxygen::content::ValidateVirtualPathSegment(segment));

    EXPECT_EQ(actual, entry.at("refusal")) << entry.dump();
  }
}

//! Each mount name of the table gets its refusal code, or none.
NOLINT_TEST(VirtualPathCasesTest, MountsFollowTheCaseTable)
{
  for (const auto& entry : LoadCases().at("mounts")) {
    const auto mount = entry.at("mount").get<std::string>();

    const auto actual = CodeOf(oxygen::content::ValidateMountName(mount));

    EXPECT_EQ(actual, entry.at("refusal")) << entry.dump();
  }
}

//! Each path of the table gets its refusal code under its rule set.
NOLINT_TEST(VirtualPathCasesTest, PathsFollowTheCaseTable)
{
  for (const auto& entry : LoadCases().at("paths")) {
    const auto path = entry.at("path").get<std::string>();
    const auto rules = entry.at("rules").get<std::string>() == "authored"
      ? VirtualPathRuleSet::kAuthored
      : VirtualPathRuleSet::kCooked;

    const auto actual
      = CodeOf(oxygen::content::ValidateVirtualPath(path, rules));

    EXPECT_EQ(actual, entry.at("refusal")) << entry.dump();
    EXPECT_EQ(oxygen::content::IsCanonicalVirtualPath(path, rules),
      entry.at("refusal").is_null())
      << entry.dump();
  }
}

//! NFC normalization gives the table's result.
NOLINT_TEST(VirtualPathCasesTest, NfcFollowsTheCaseTable)
{
  for (const auto& entry : LoadCases().at("nfc")) {
    const auto actual
      = oxygen::content::ToNfc(entry.at("text").get<std::string>());

    EXPECT_EQ(actual, std::optional { entry.at("nfc").get<std::string>() })
      << entry.dump();
  }
}

//! Simple case folding gives the table's result.
NOLINT_TEST(VirtualPathCasesTest, FoldingFollowsTheCaseTable)
{
  for (const auto& entry : LoadCases().at("fold")) {
    const auto actual
      = oxygen::content::FoldCase(entry.at("text").get<std::string>());

    EXPECT_EQ(actual, std::optional { entry.at("folded").get<std::string>() })
      << entry.dump();
  }
}

//! The table refuses with every code at least once, and with no other code.
NOLINT_TEST(VirtualPathCasesTest, TableCoversEveryRefusalCode)
{
  auto known = std::set<std::string> {};
  for (const auto error : kAllErrors) {
    known.emplace(to_string(error));
  }
  auto used = std::set<std::string> {};
  for (const auto* section : { "segments", "mounts", "paths" }) {
    for (const auto& entry : LoadCases().at(section)) {
      if (!entry.at("refusal").is_null()) {
        used.insert(entry.at("refusal").get<std::string>());
      }
    }
  }

  EXPECT_EQ(used, known);
  EXPECT_EQ(LoadCases().at("unicode_version").get<std::string>(), "15.1.0");
}

//! Text that is not UTF-8 has no NFC form and no folding.
NOLINT_TEST(VirtualPathTest, InvalidUtf8HasNoNormalizationOrFolding)
{
  const auto invalid = std::string { "a\xFF" };

  EXPECT_FALSE(oxygen::content::ToNfc(invalid).has_value());
  EXPECT_FALSE(oxygen::content::FoldCase(invalid).has_value());
}

} // namespace
