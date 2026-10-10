//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Tools/PakTool/ScriptSealing.cpp

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <ios>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <Oxygen/Base/Filesystem.h>
#include <Oxygen/Content/LooseCookedIndex.h>
#include <Oxygen/Cooker/Import/Internal/LooseCookedWriter.h>
#include <Oxygen/Cooker/Loose/LooseCookedLayout.h>
#include <Oxygen/Cooker/Pak/PakBuildRequest.h>
#include <Oxygen/Cooker/Test/Support/TempDir.h>
#include <Oxygen/Cooker/Tools/PakTool/ScriptSealing.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/CookedSource.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::content::import::LooseCookedLayout;
using oxygen::content::import::LooseCookedWriter;
using oxygen::content::pak::BuildMode;
using oxygen::content::pak::PakBuildRequest;
using oxygen::content::pak::tool::CleanupStagedLooseRoots;
using oxygen::content::pak::tool::SealLooseCookedSourcesForPakBuild;
using oxygen::data::AssetKey;
using oxygen::data::AssetType;
using oxygen::data::CookedSource;
using oxygen::data::CookedSourceKind;
using oxygen::data::kNoResourceReference;
using oxygen::data::SourceKey;
using oxygen::data::pak::scripting::ScriptAssetDesc;
using oxygen::data::pak::scripting::ScriptAssetFlags;
using oxygen::data::pak::scripting::ScriptEncoding;
using oxygen::data::pak::scripting::ScriptResourceDesc;

constexpr auto kSourceKey = "01234567-89ab-7def-8123-456789abcdef";

class PakToolScriptSealingTest : public testing::TestWithParam<bool> {
protected:
  void SetUp() override
  {
    root_ = temp_.Path();
    if (GetParam()) {
      for (auto index = 0U; index < 5U; ++index) {
        root_ /= "long-authoring-and-sealing-directory-component";
      }
    }
    std::filesystem::create_directories(oxygen::base::ToNativePath(root_));
  }

  void TearDown() override
  {
    // The long-path variant needs native-path removal; ScopedTempDir would
    // not delete it.
    std::error_code ec {};
    std::filesystem::remove_all(oxygen::base::ToNativePath(temp_.Path()), ec);
  }

  [[nodiscard]] auto Root() const -> const std::filesystem::path&
  {
    return root_;
  }

  [[nodiscard]] auto TestRoot() const -> const std::filesystem::path&
  {
    return temp_.Path();
  }

  static auto WriteTextFile(
    const std::filesystem::path& path, const std::string_view content) -> void
  {
    std::filesystem::create_directories(
      oxygen::base::ToNativePath(path.parent_path()));
    auto out = std::ofstream(
      oxygen::base::ToNativePath(path), std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(out.is_open()) << path.string();
    out << content;
  }

  static auto ReadScriptAssetDescriptor(const std::filesystem::path& path)
    -> ScriptAssetDesc
  {
    auto in = std::ifstream(oxygen::base::ToNativePath(path), std::ios::binary);
    if (!in.is_open()) {
      throw std::runtime_error("cannot open " + path.string());
    }

    auto descriptor = ScriptAssetDesc {};
    in.read(reinterpret_cast<char*>(&descriptor), sizeof(descriptor));
    if (!in.good() && !in.eof()) {
      throw std::runtime_error(
        "cannot read script descriptor " + path.string());
    }
    return descriptor;
  }

  //! Seals a loose-cooked source whose script descriptor stores `stored` as
  //! its external source path and expects sealing to fail.
  auto ExpectSealingRejectsStoredPath(const std::string& stored,
    const std::string_view cooked_name,
    const std::filesystem::path& content_root) const -> void
  {
    const auto cooked = Root() / cooked_name;
    ScriptAssetDesc descriptor {};
    descriptor.header.asset_type = static_cast<uint8_t>(AssetType::kScript);
    descriptor.header.version
      = oxygen::data::pak::scripting::kScriptAssetVersion;
    descriptor.flags = ScriptAssetFlags::kAllowExternalSource;
    ASSERT_LT(stored.size(), sizeof(descriptor.external_source_path));
    std::memcpy(descriptor.external_source_path, stored.data(), stored.size());
    LooseCookedWriter writer(cooked);
    const auto layout = LooseCookedLayout {};
    const auto virtual_path = layout.ScriptVirtualPath("rooted");
    writer.WriteAssetDescriptor(AssetKey::FromVirtualPath(virtual_path),
      AssetType::kScript, virtual_path,
      layout.ScriptDescriptorRelPath("rooted"),
      std::as_bytes(std::span { &descriptor, 1 }), {});
    static_cast<void>(writer.Finish());
    PakBuildRequest request {};
    request.mode = BuildMode::kFull;
    request.sources = { CookedSource {
      .kind = CookedSourceKind::kLooseCooked,
      .path = cooked,
    } };
    const auto sealed = SealLooseCookedSourcesForPakBuild(
      request, Root() / "staging", std::span { &content_root, 1 });
    EXPECT_FALSE(sealed.has_value()) << stored;
    if (sealed.has_value()) {
      CleanupStagedLooseRoots(sealed->staged_loose_roots);
    }
  }

private:
  oxygen::cooker::test::ScopedTempDir temp_;
  std::filesystem::path root_;
};

NOLINT_TEST_P(PakToolScriptSealingTest,
  ExternalScriptAssetIsSealedIntoEmbeddedSourceInStagedRoot)
{
  const auto content_root = Root() / "Examples" / "Content";
  const auto cooked_root = Root() / "derived" / "unrelated" / "main";
  const auto external_script_path
    = content_root / "scenes" / "proc-cubes" / "proc_cubes.lua";
  WriteTextFile(external_script_path, "return { update = function() end }\n");

  const auto layout = LooseCookedLayout {};
  const auto script_name = std::string("proc_cubes");
  const auto script_virtual_path = layout.ScriptVirtualPath(script_name);
  const auto script_relpath = layout.ScriptDescriptorRelPath(script_name);
  const auto script_key = AssetKey::FromVirtualPath(script_virtual_path);

  auto descriptor = ScriptAssetDesc {};
  descriptor.header.asset_type = static_cast<uint8_t>(AssetType::kScript);
  descriptor.header.version = oxygen::data::pak::scripting::kScriptAssetVersion;
  descriptor.flags = ScriptAssetFlags::kAllowExternalSource;
  const auto stored_external_path
    = std::string("scenes/proc-cubes/proc_cubes.lua");
  std::memcpy(descriptor.external_source_path, stored_external_path.data(),
    stored_external_path.size());

  auto writer = LooseCookedWriter(cooked_root);
  const auto source_key = SourceKey::FromString(kSourceKey);
  ASSERT_TRUE(source_key.has_value());
  writer.SetSourceKey(source_key.value());
  writer.SetContentVersion(7);
  writer.WriteAssetDescriptor(script_key, AssetType::kScript,
    script_virtual_path, script_relpath,
    std::as_bytes(std::span { &descriptor, 1 }), {});
  static_cast<void>(writer.Finish());

  auto request = PakBuildRequest {};
  request.mode = BuildMode::kFull;
  request.sources = {
    CookedSource {
      .kind = CookedSourceKind::kLooseCooked,
      .path = cooked_root,
    },
  };

  const auto sealed = SealLooseCookedSourcesForPakBuild(
    request, Root() / "staging", std::span { &content_root, 1 });
  ASSERT_TRUE(sealed.has_value())
    << sealed.error().error_code << ": " << sealed.error().error_message;
  ASSERT_EQ(sealed->sealed_script_assets, 1U);
  ASSERT_EQ(sealed->staged_loose_roots.size(), 1U);
  ASSERT_EQ(sealed->build_request.sources.size(), 1U);

  const auto& staged_root = sealed->build_request.sources.at(0).path;
  const auto staged_parent = staged_root.parent_path();
  EXPECT_NE(staged_root, cooked_root);
  EXPECT_TRUE(std::filesystem::exists(
    oxygen::base::ToNativePath(staged_root / "container.index.bin")));

  const auto staged_descriptor = ReadScriptAssetDescriptor(
    staged_root / std::filesystem::path(script_relpath));
  EXPECT_NE(staged_descriptor.source_resource_index, kNoResourceReference);
  EXPECT_EQ(static_cast<uint32_t>(staged_descriptor.flags), 0U);
  EXPECT_EQ(staged_descriptor.external_source_path[0], '\0');

  const auto scripts_table_path
    = staged_root / std::filesystem::path(layout.ScriptsTableRelPath());
  const auto scripts_data_path
    = staged_root / std::filesystem::path(layout.ScriptsDataRelPath());
  EXPECT_TRUE(
    std::filesystem::exists(oxygen::base::ToNativePath(scripts_table_path)));
  EXPECT_TRUE(
    std::filesystem::exists(oxygen::base::ToNativePath(scripts_data_path)));

  {
    auto table_in = std::ifstream(
      oxygen::base::ToNativePath(scripts_table_path), std::ios::binary);
    ASSERT_TRUE(table_in.is_open());
    table_in.seekg(0, std::ios::end);
    const auto table_size = static_cast<size_t>(table_in.tellg());
    table_in.seekg(0, std::ios::beg);
    ASSERT_EQ(table_size, sizeof(ScriptResourceDesc) * 2U);
    auto table_entries = std::vector<ScriptResourceDesc>(2U);
    table_in.read(reinterpret_cast<char*>(table_entries.data()),
      static_cast<std::streamsize>(table_size));

    const auto index
      = oxygen::content::lc::LooseCookedIndex::LoadFromRoot(staged_root);
    const auto references = index.FindAssetReferences(script_key);
    ASSERT_TRUE(references.has_value());
    const auto resolved
      = references->ResolveResource(staged_descriptor.source_resource_index,
        oxygen::data::ResourceKind::kScript);
    ASSERT_TRUE(resolved.has_value());
    ASSERT_TRUE(resolved->has_value());
    const auto source_index = (**resolved).get();
    ASSERT_EQ(source_index, 1U);
    EXPECT_EQ(table_entries.at(source_index).encoding, ScriptEncoding::kSource);
    EXPECT_GT(table_entries.at(source_index).size_bytes, 0U);
  }

  {
    const auto staged_index
      = oxygen::content::lc::LooseCookedIndex::LoadFromRoot(staged_root);
    const auto descriptor_size = staged_index.FindDescriptorSize(script_key);
    ASSERT_TRUE(descriptor_size.has_value());
    EXPECT_EQ(descriptor_size.value_or(0U), sizeof(ScriptAssetDesc));
    const auto file_relpath = staged_index.FindFileRelPath(
      oxygen::data::loose_cooked::FileKind::kScriptsTable);
    ASSERT_TRUE(file_relpath.has_value());
    EXPECT_EQ(file_relpath.value_or(""), layout.ScriptsTableRelPath());
  }

  CleanupStagedLooseRoots(sealed->staged_loose_roots);
  EXPECT_FALSE(
    std::filesystem::exists(oxygen::base::ToNativePath(staged_root)));
  EXPECT_FALSE(
    std::filesystem::exists(oxygen::base::ToNativePath(staged_parent)));
}

NOLINT_TEST_P(PakToolScriptSealingTest, RejectsRootRelativeScriptPath)
{
  const auto content_root = Root() / "Content";
  const auto outside = TestRoot() / "outside.lua";
  WriteTextFile(outside, "return 1");
  WriteTextFile(content_root / "inside.lua", "return 2");

  ExpectSealingRejectsStoredPath(
    std::string("\\") + outside.relative_path().generic_string(),
    "rooted-script-root-relative", content_root);
}

NOLINT_TEST_P(PakToolScriptSealingTest, RejectsDriveRelativeScriptPath)
{
#if defined(_WIN32)
  const auto content_root = Root() / "Content";
  WriteTextFile(content_root / "inside.lua", "return 2");

  ExpectSealingRejectsStoredPath(
    content_root.root_name().generic_string() + "inside.lua",
    "rooted-script-drive-relative", content_root);
#else
  // root_name() is empty on POSIX, so the stored path would be a plain
  // relative path rather than a drive-relative one.
  GTEST_SKIP() << "Drive-relative paths exist only on Windows";
#endif
}

INSTANTIATE_TEST_SUITE_P(PathLengths, PakToolScriptSealingTest, testing::Bool(),
  [](const testing::TestParamInfo<bool>& info) {
    return info.param ? "LongPaths" : "ShortPaths";
  });

} // namespace
