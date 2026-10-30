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
  VirtualPathError::kReferenceOutsideMount,
  VirtualPathError::kReferenceSelfAbsolute,
};
static_assert(kAllErrors.size()
  == static_cast<std::size_t>(VirtualPathError::kReferenceSelfAbsolute) + 1);

auto RulesOf(const nlohmann::json& entry) -> VirtualPathRuleSet
{
  return entry.at("rules").get<std::string>() == "authored"
    ? VirtualPathRuleSet::kAuthored
    : VirtualPathRuleSet::kCooked;
}

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
    const auto rules = RulesOf(entry);

    const auto actual
      = CodeOf(oxygen::content::ValidateVirtualPath(path, rules));

    EXPECT_EQ(actual, entry.at("refusal")) << entry.dump();
    EXPECT_EQ(oxygen::content::IsCanonicalVirtualPath(path, rules),
      entry.at("refusal").is_null())
      << entry.dump();
  }
}

//! Each reference of the table resolves to its path, or gets its refusal
//! code, and the resolved path is spelled back as the table's text.
NOLINT_TEST(VirtualPathCasesTest, ReferencesFollowTheCaseTable)
{
  for (const auto& entry : LoadCases().at("references")) {
    const auto anchor = entry.at("anchor").get<std::string>();

    const auto actual = oxygen::content::ResolveReference(
      anchor, entry.at("text").get<std::string>(), RulesOf(entry));

    EXPECT_EQ(actual.has_value() ? nlohmann::json(nullptr)
                                 : nlohmann::json(to_string(actual.error())),
      entry.at("refusal"))
      << entry.dump();
    if (actual.has_value()) {
      const auto spelled = entry.at("spelled").get<std::string>();
      EXPECT_EQ(*actual, entry.at("resolved").get<std::string>())
        << entry.dump();
      EXPECT_EQ(oxygen::content::RelativeReference(anchor, *actual), spelled)
        << entry.dump();
      EXPECT_EQ(
        oxygen::content::ResolveReference(anchor, spelled, RulesOf(entry)),
        actual)
        << entry.dump();
    }
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
  for (const auto* section : { "segments", "mounts", "paths", "references" }) {
    for (const auto& entry : LoadCases().at(section)) {
      if (!entry.at("refusal").is_null()) {
        used.insert(entry.at("refusal").get<std::string>());
      }
    }
  }

  EXPECT_EQ(used, known);
  EXPECT_EQ(LoadCases().at("unicode_version").get<std::string>(), "15.1.0");
}

//! A path's mount is its first segment; a path naming no mount, or only a
//! mount, has none.
NOLINT_TEST(VirtualPathTest, MountRootIsTheFirstSegment)
{
  using oxygen::content::MountRootOf;

  EXPECT_EQ(MountRootOf("/Content/Models/Ship.glb"), "/Content");
  EXPECT_EQ(MountRootOf("/Engine/Ship.omat"), "/Engine");
  EXPECT_EQ(MountRootOf("/Content"), "");
  EXPECT_EQ(MountRootOf("/Content/"), "");
  EXPECT_EQ(MountRootOf("Content/Ship.omat"), "");
  EXPECT_EQ(MountRootOf("//Ship.omat"), "");
  EXPECT_EQ(MountRootOf(""), "");
}

//! Text that is not UTF-8 has no NFC form and no folding.
NOLINT_TEST(VirtualPathTest, InvalidUtf8HasNoNormalizationOrFolding)
{
  const auto invalid = std::string { "a\xFF" };

  EXPECT_FALSE(oxygen::content::ToNfc(invalid).has_value());
  EXPECT_FALSE(oxygen::content::FoldCase(invalid).has_value());
}

} // namespace
