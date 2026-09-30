//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <chrono>
#include <exception>
#include <filesystem>
#include <string>

#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Cooker/Import/CapturedInputSet.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::import::test {
namespace {
  auto PathText(const std::filesystem::path& path) -> std::string
  {
    const auto text = path.generic_u8string();
    return { text.begin(), text.end() };
  }

  auto MakeDocument() -> nlohmann::json
  {
    return {
      { "schema_version", 1 },
      { "inputs",
        nlohmann::json::array({
          {
            { "logical_path",
              PathText(std::filesystem::absolute("model.gltf")) },
            { "exists", true },
            { "metadata",
              {
                { "size", 6 },
                { "is_directory", false },
                { "is_symlink", true },
                { "last_modified_seconds", -1 },
                { "last_modified_nanoseconds", 123456700 },
              } },
            { "file",
              {
                { "path",
                  PathText(std::filesystem::absolute("model.capture")) },
                { "size", 6 },
                { "sha256", std::string(64, '0') },
              } },
          },
        }) },
    };
  }

  NOLINT_TEST(CapturedInputSetTest, PreservesPathsAndExactSourceMetadata)
  {
    auto document = MakeDocument();
    const auto logical = std::filesystem::absolute(
      std::filesystem::path(u8"authoring/模型.gltf"));
    document.at("inputs").at(0).at("logical_path") = PathText(logical);
    const auto inputs = CapturedInputSet::Parse(document.dump());
    const auto* input = inputs->Find(logical);
    ASSERT_NE(input, nullptr);
    if (!input->metadata.has_value() || !input->file.has_value()) {
      FAIL() << "Expected captured bytes and source metadata";
    }
    EXPECT_TRUE(input->metadata->is_symlink);
    EXPECT_EQ(input->file->size, 6U);
    EXPECT_EQ(input->file->path, std::filesystem::absolute("model.capture"));
    const auto timestamp = std::chrono::clock_cast<std::chrono::system_clock>(
      input->metadata->last_modified);
    EXPECT_EQ(
      timestamp.time_since_epoch(), std::chrono::nanoseconds(-876543300));
  }

  NOLINT_TEST(
    CapturedInputSetTest, AcceptsExplicitAbsenceWithoutTouchingLiveFiles)
  {
    auto document = MakeDocument();
    auto& entry = document.at("inputs").at(0);
    entry.at("exists") = false;
    entry.at("metadata") = nullptr;
    entry.at("file") = nullptr;
    const auto inputs = CapturedInputSet::Parse(document.dump());
    const auto* input = inputs->Find(std::filesystem::absolute("model.gltf"));
    ASSERT_NE(input, nullptr);
    EXPECT_FALSE(input->exists);
    EXPECT_FALSE(input->metadata.has_value());
    EXPECT_FALSE(input->file.has_value());
  }

  NOLINT_TEST(CapturedInputSetTest, RejectsSchemaAndDigestErrors)
  {
    auto document = MakeDocument();
    document.at("schema_version") = 2;
    EXPECT_THROW(static_cast<void>(CapturedInputSet::Parse(document.dump())),
      std::exception);
    document = MakeDocument();
    document.at("inputs").at(0).at("file").at("sha256") = "0";
    EXPECT_THROW(static_cast<void>(CapturedInputSet::Parse(document.dump())),
      std::exception);
    document = MakeDocument();
    document.emplace("unknown", true);
    EXPECT_THROW(static_cast<void>(CapturedInputSet::Parse(document.dump())),
      std::exception);
  }

  NOLINT_TEST(CapturedInputSetTest, RejectsContradictoryAndDuplicateInputs)
  {
    auto document = MakeDocument();
    document.at("inputs").at(0).at("exists") = false;
    EXPECT_THROW(static_cast<void>(CapturedInputSet::Parse(document.dump())),
      std::exception);
    document = MakeDocument();
    document.at("inputs").push_back(document.at("inputs").front());
    EXPECT_THROW(static_cast<void>(CapturedInputSet::Parse(document.dump())),
      std::exception);
    document = MakeDocument();
    document.at("inputs").at(0).at("metadata").at("size") = 7;
    EXPECT_THROW(static_cast<void>(CapturedInputSet::Parse(document.dump())),
      std::exception);
  }

  NOLINT_TEST(CapturedInputSetTest, RejectsRelativeAndNullTerminatedPaths)
  {
    auto document = MakeDocument();
    document.at("inputs").at(0).at("logical_path") = "relative.gltf";
    EXPECT_THROW(static_cast<void>(CapturedInputSet::Parse(document.dump())),
      std::exception);
    document = MakeDocument();
    document.at("inputs").at(0).at("file").at("path")
      = std::string("capture\0suffix", 14);
    EXPECT_THROW(static_cast<void>(CapturedInputSet::Parse(document.dump())),
      std::exception);
  }

  NOLINT_TEST(
    CapturedInputSetTest, RejectsNumericOverflowBeforeNativeConversion)
  {
    auto document = MakeDocument();
    document.at("inputs").at(0).at("file").at("size") = 1.0e30;
    EXPECT_THROW(static_cast<void>(CapturedInputSet::Parse(document.dump())),
      std::exception);
    document = MakeDocument();
    document.at("inputs").at(0).at("metadata").at("last_modified_seconds")
      = 1.0e30;
    EXPECT_THROW(static_cast<void>(CapturedInputSet::Parse(document.dump())),
      std::exception);
  }
} // namespace
} // namespace oxygen::content::import::test
