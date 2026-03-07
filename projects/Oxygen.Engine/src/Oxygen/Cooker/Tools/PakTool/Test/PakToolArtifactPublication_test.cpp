//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>

#include <Oxygen/Cooker/Tools/PakTool/ArtifactPublication.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::content::pak::tool::ArtifactPublicationIntent;
using oxygen::content::pak::tool::IArtifactFileSystem;
using oxygen::content::pak::tool::MakeArtifactPublicationPlan;
using oxygen::content::pak::tool::PublishArtifacts;
using oxygen::content::pak::tool::RealArtifactFileSystem;

class PublishingFileSystem final : public IArtifactFileSystem {
public:
  explicit PublishingFileSystem(
    std::optional<std::filesystem::path> fail_publish_target = std::nullopt)
    : fail_publish_target_(std::move(fail_publish_target))
  {
  }

  [[nodiscard]] auto Exists(const std::filesystem::path& path) const
    -> bool override
  {
    return std::filesystem::exists(path);
  }

  auto CreateDirectories(const std::filesystem::path& path)
    -> std::error_code override
  {
    auto ec = std::error_code {};
    if (!path.empty()) {
      std::filesystem::create_directories(path, ec);
    }
    return ec;
  }

  auto RemoveFile(const std::filesystem::path& path) -> std::error_code override
  {
    auto ec = std::error_code {};
    if (!path.empty()) {
      std::filesystem::remove(path, ec);
    }
    return ec;
  }

  auto Rename(const std::filesystem::path& from,
    const std::filesystem::path& to) -> std::error_code override
  {
    if (fail_publish_target_.has_value() && to == *fail_publish_target_
      && from.filename().string().ends_with(".staged")
      && std::filesystem::exists(from)) {
      return std::make_error_code(std::errc::permission_denied);
    }

    auto ec = std::error_code {};
    std::filesystem::rename(from, to, ec);
    return ec;
  }

private:
  std::optional<std::filesystem::path> fail_publish_target_;
};

class PakToolArtifactPublicationTest : public testing::Test {
protected:
  void SetUp() override
  {
    static auto counter = std::atomic_uint64_t { 0 };
    const auto pid = static_cast<unsigned long long>(_getpid());
    const auto id = counter.fetch_add(1, std::memory_order_relaxed);
    root_ = std::filesystem::temp_directory_path() / "oxygen_paktool_publish"
      / ("pid-" + std::to_string(pid) + "-case-" + std::to_string(id));
    std::filesystem::remove_all(root_);
    std::filesystem::create_directories(root_);
  }

  void TearDown() override
  {
    std::error_code ec {};
    std::filesystem::remove_all(root_, ec);
  }

  [[nodiscard]] auto Root() const -> const std::filesystem::path&
  {
    return root_;
  }

  static auto WriteTextFile(
    const std::filesystem::path& path, const std::string_view content) -> void
  {
    std::filesystem::create_directories(path.parent_path());
    auto out = std::ofstream(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(out.is_open()) << path.string();
    out << content;
  }

  [[nodiscard]] static auto ReadTextFile(const std::filesystem::path& path)
    -> std::string
  {
    auto in = std::ifstream(path, std::ios::binary);
    EXPECT_TRUE(in.is_open()) << path.string();
    return std::string(
      std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  }

private:
  std::filesystem::path root_;
};

NOLINT_TEST_F(PakToolArtifactPublicationTest,
  MakeArtifactPublicationPlanUsesDeterministicSiblingPaths)
{
  const auto plan = MakeArtifactPublicationPlan(Root() / "release" / "game.pak",
    Root() / "release" / "game.catalog.json",
    Root() / "release" / "game.manifest.json",
    Root() / "release" / "game.report.json");

  EXPECT_EQ(plan.pak.staged_path, Root() / "release" / "game.pak.staged");
  EXPECT_EQ(plan.pak.backup_path, Root() / "release" / "game.pak.previous");
  ASSERT_TRUE(plan.manifest.has_value());
  EXPECT_EQ(plan.manifest->staged_path,
    Root() / "release" / "game.manifest.json.staged");
  ASSERT_TRUE(plan.report.has_value());
  EXPECT_EQ(
    plan.report->backup_path, Root() / "release" / "game.report.json.previous");
}

NOLINT_TEST_F(PakToolArtifactPublicationTest,
  PublishArtifactsPromotesAllRequestedOutputsOnSuccess)
{
  const auto plan = MakeArtifactPublicationPlan(Root() / "release" / "game.pak",
    Root() / "release" / "game.catalog.json",
    Root() / "release" / "game.manifest.json",
    Root() / "release" / "game.report.json");

  WriteTextFile(plan.pak.staged_path, "pak-new");
  WriteTextFile(plan.catalog.staged_path, "catalog-new");
  WriteTextFile(plan.manifest->staged_path, "manifest-new");
  WriteTextFile(plan.report->staged_path, "report-new");

  auto intent = ArtifactPublicationIntent {
    .create_parent_directories = true,
    .publish_pak = true,
    .publish_catalog = true,
    .publish_manifest = true,
    .publish_report = true,
  };
  auto fs = RealArtifactFileSystem {};

  const auto result = PublishArtifacts(plan, intent, fs);

  EXPECT_TRUE(result.success)
    << result.error_code << ": " << result.error_message;
  EXPECT_TRUE(std::filesystem::exists(plan.pak.final_path));
  EXPECT_TRUE(std::filesystem::exists(plan.catalog.final_path));
  EXPECT_TRUE(std::filesystem::exists(plan.manifest->final_path));
  EXPECT_TRUE(std::filesystem::exists(plan.report->final_path));
  EXPECT_EQ(ReadTextFile(plan.pak.final_path), "pak-new");
  EXPECT_EQ(ReadTextFile(plan.catalog.final_path), "catalog-new");
  EXPECT_EQ(ReadTextFile(plan.manifest->final_path), "manifest-new");
  EXPECT_EQ(ReadTextFile(plan.report->final_path), "report-new");
  EXPECT_FALSE(std::filesystem::exists(plan.pak.staged_path));
  EXPECT_FALSE(std::filesystem::exists(plan.catalog.staged_path));
  EXPECT_FALSE(std::filesystem::exists(plan.manifest->staged_path));
  EXPECT_FALSE(std::filesystem::exists(plan.report->staged_path));
  EXPECT_FALSE(std::filesystem::exists(plan.pak.backup_path));
  EXPECT_FALSE(std::filesystem::exists(plan.catalog.backup_path));
}

NOLINT_TEST_F(PakToolArtifactPublicationTest,
  PublishArtifactsSuppressesStaleAuthoritativeSidecarsOnBuildFailure)
{
  const auto plan = MakeArtifactPublicationPlan(Root() / "release" / "game.pak",
    Root() / "release" / "game.catalog.json",
    Root() / "release" / "game.manifest.json",
    Root() / "release" / "game.report.json");

  WriteTextFile(plan.pak.final_path, "pak-old");
  WriteTextFile(plan.catalog.final_path, "catalog-old");
  WriteTextFile(plan.manifest->final_path, "manifest-old");

  WriteTextFile(plan.pak.staged_path, "pak-new");
  WriteTextFile(plan.catalog.staged_path, "catalog-new");
  WriteTextFile(plan.manifest->staged_path, "manifest-new");
  WriteTextFile(plan.report->staged_path, "report-failure");

  auto intent = ArtifactPublicationIntent {
    .create_parent_directories = true,
    .publish_pak = false,
    .publish_catalog = false,
    .publish_manifest = false,
    .publish_report = true,
    .suppress_stale_catalog_on_skip = true,
    .suppress_stale_manifest_on_skip = true,
  };
  auto fs = RealArtifactFileSystem {};

  const auto result = PublishArtifacts(plan, intent, fs);

  EXPECT_TRUE(result.success)
    << result.error_code << ": " << result.error_message;
  EXPECT_TRUE(std::filesystem::exists(plan.pak.final_path));
  EXPECT_EQ(ReadTextFile(plan.pak.final_path), "pak-old");
  EXPECT_FALSE(std::filesystem::exists(plan.catalog.final_path));
  EXPECT_FALSE(std::filesystem::exists(plan.manifest->final_path));
  EXPECT_TRUE(std::filesystem::exists(plan.report->final_path));
  EXPECT_EQ(ReadTextFile(plan.report->final_path), "report-failure");
  EXPECT_FALSE(std::filesystem::exists(plan.pak.staged_path));
  EXPECT_FALSE(std::filesystem::exists(plan.catalog.staged_path));
  EXPECT_FALSE(std::filesystem::exists(plan.manifest->staged_path));
  EXPECT_FALSE(std::filesystem::exists(plan.report->staged_path));
}

NOLINT_TEST_F(PakToolArtifactPublicationTest,
  PublishArtifactsRollsBackEarlierPromotionsOnPublishFailure)
{
  const auto plan = MakeArtifactPublicationPlan(Root() / "release" / "game.pak",
    Root() / "release" / "game.catalog.json",
    Root() / "release" / "game.manifest.json",
    Root() / "release" / "game.report.json");

  WriteTextFile(plan.pak.final_path, "pak-old");
  WriteTextFile(plan.catalog.final_path, "catalog-old");
  WriteTextFile(plan.manifest->final_path, "manifest-old");

  WriteTextFile(plan.pak.staged_path, "pak-new");
  WriteTextFile(plan.catalog.staged_path, "catalog-new");
  WriteTextFile(plan.manifest->staged_path, "manifest-new");
  WriteTextFile(plan.report->staged_path, "report-new");

  auto intent = ArtifactPublicationIntent {
    .create_parent_directories = true,
    .publish_pak = true,
    .publish_catalog = true,
    .publish_manifest = true,
    .publish_report = true,
  };
  auto fs = PublishingFileSystem(plan.manifest->final_path);

  const auto result = PublishArtifacts(plan, intent, fs);

  EXPECT_FALSE(result.success);
  EXPECT_EQ(result.error_code, "paktool.publish.rename_failed");
  EXPECT_EQ(ReadTextFile(plan.pak.final_path), "pak-old");
  EXPECT_EQ(ReadTextFile(plan.catalog.final_path), "catalog-old");
  EXPECT_EQ(ReadTextFile(plan.manifest->final_path), "manifest-old");
  EXPECT_FALSE(std::filesystem::exists(plan.report->final_path));
  EXPECT_FALSE(std::filesystem::exists(plan.pak.staged_path));
  EXPECT_FALSE(std::filesystem::exists(plan.catalog.staged_path));
  EXPECT_FALSE(std::filesystem::exists(plan.manifest->staged_path));
  EXPECT_FALSE(std::filesystem::exists(plan.report->staged_path));
  EXPECT_FALSE(std::filesystem::exists(plan.pak.backup_path));
  EXPECT_FALSE(std::filesystem::exists(plan.catalog.backup_path));
  EXPECT_FALSE(std::filesystem::exists(plan.manifest->backup_path));
}

} // namespace
