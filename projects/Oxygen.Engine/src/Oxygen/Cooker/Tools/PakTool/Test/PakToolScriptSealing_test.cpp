//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <process.h>
#include <span>
#include <string_view>
#include <vector>

#include <Oxygen/Content/LooseCookedIndex.h>
#include <Oxygen/Cooker/Import/Internal/LooseCookedWriter.h>
#include <Oxygen/Cooker/Loose/LooseCookedLayout.h>
#include <Oxygen/Cooker/Tools/PakTool/ScriptSealing.h>
#include <Oxygen/Data/PakFormat_scripting.h>
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
using oxygen::data::SourceKey;
using oxygen::data::pak::core::kNoResourceIndex;
using oxygen::data::pak::scripting::ScriptAssetDesc;
using oxygen::data::pak::scripting::ScriptAssetFlags;
using oxygen::data::pak::scripting::ScriptEncoding;
using oxygen::data::pak::scripting::ScriptResourceDesc;

constexpr auto kSourceKey = "01234567-89ab-7def-8123-456789abcdef";

class PakToolScriptSealingTest : public testing::Test {
protected:
  void SetUp() override
  {
    static auto counter = std::atomic_uint64_t { 0 };
    const auto pid = static_cast<unsigned long long>(_getpid());
    const auto id = counter.fetch_add(1, std::memory_order_relaxed);
    root_ = std::filesystem::temp_directory_path() / "oxygen_paktool_seal"
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

  static auto ReadScriptAssetDescriptor(const std::filesystem::path& path)
    -> ScriptAssetDesc
  {
    auto in = std::ifstream(path, std::ios::binary);
    EXPECT_TRUE(in.is_open()) << path.string();

    auto descriptor = ScriptAssetDesc {};
    in.read(reinterpret_cast<char*>(&descriptor), sizeof(descriptor));
    EXPECT_TRUE(in.good() || in.eof()) << path.string();
    return descriptor;
  }

private:
  std::filesystem::path root_;
};

NOLINT_TEST_F(PakToolScriptSealingTest,
  ExternalScriptAssetIsSealedIntoEmbeddedSourceInStagedRoot)
{
  const auto content_root = Root() / "Examples" / "Content";
  const auto cooked_root = content_root / ".cooked";
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
    std::as_bytes(std::span { &descriptor, 1 }));
  static_cast<void>(writer.Finish());

  auto request = PakBuildRequest {};
  request.mode = BuildMode::kFull;
  request.sources = {
    CookedSource {
      .kind = CookedSourceKind::kLooseCooked,
      .path = cooked_root,
    },
  };

  const auto sealed
    = SealLooseCookedSourcesForPakBuild(request, Root() / "staging");
  ASSERT_TRUE(sealed.has_value())
    << sealed.error().error_code << ": " << sealed.error().error_message;
  ASSERT_EQ(sealed->sealed_script_assets, 1U);
  ASSERT_EQ(sealed->staged_loose_roots.size(), 1U);
  ASSERT_EQ(sealed->build_request.sources.size(), 1U);

  const auto& staged_root = sealed->build_request.sources[0].path;
  const auto staged_parent = staged_root.parent_path();
  EXPECT_NE(staged_root, cooked_root);
  EXPECT_TRUE(std::filesystem::exists(staged_root / "container.index.bin"));

  const auto staged_descriptor = ReadScriptAssetDescriptor(
    staged_root / std::filesystem::path(script_relpath));
  EXPECT_NE(staged_descriptor.source_resource_index, kNoResourceIndex);
  EXPECT_EQ(static_cast<uint32_t>(staged_descriptor.flags), 0U);
  EXPECT_EQ(staged_descriptor.external_source_path[0], '\0');

  const auto scripts_table_path
    = staged_root / std::filesystem::path(layout.ScriptsTableRelPath());
  const auto scripts_data_path
    = staged_root / std::filesystem::path(layout.ScriptsDataRelPath());
  EXPECT_TRUE(std::filesystem::exists(scripts_table_path));
  EXPECT_TRUE(std::filesystem::exists(scripts_data_path));

  {
    auto table_in = std::ifstream(scripts_table_path, std::ios::binary);
    ASSERT_TRUE(table_in.is_open());
    table_in.seekg(0, std::ios::end);
    const auto table_size = static_cast<size_t>(table_in.tellg());
    table_in.seekg(0, std::ios::beg);
    ASSERT_EQ(table_size, sizeof(ScriptResourceDesc) * 2U);
    auto table_entries = std::vector<ScriptResourceDesc>(2U);
    table_in.read(reinterpret_cast<char*>(table_entries.data()),
      static_cast<std::streamsize>(table_size));

    const auto source_index
      = static_cast<uint32_t>(staged_descriptor.source_resource_index);
    ASSERT_EQ(source_index, 1U);
    EXPECT_EQ(table_entries[source_index].encoding, ScriptEncoding::kSource);
    EXPECT_GT(table_entries[source_index].size_bytes, 0U);
  }

  {
    const auto staged_index
      = oxygen::content::lc::LooseCookedIndex::LoadFromRoot(staged_root);
    const auto descriptor_size = staged_index.FindDescriptorSize(script_key);
    ASSERT_TRUE(descriptor_size.has_value());
    EXPECT_EQ(*descriptor_size, sizeof(ScriptAssetDesc));
    const auto file_relpath = staged_index.FindFileRelPath(
      oxygen::data::loose_cooked::FileKind::kScriptsTable);
    ASSERT_TRUE(file_relpath.has_value());
    EXPECT_EQ(*file_relpath, layout.ScriptsTableRelPath());
  }

  CleanupStagedLooseRoots(sealed->staged_loose_roots);
  EXPECT_FALSE(std::filesystem::exists(staged_root));
  EXPECT_FALSE(std::filesystem::exists(staged_parent));
}

} // namespace
