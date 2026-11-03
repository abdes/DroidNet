//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Tools/PakTool/ScriptSealing.cpp

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "RequestSnapshot.h"

#include <Oxygen/Base/Filesystem.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Content/LooseCookedIndex.h>
#include <Oxygen/Cooker/Cooked/CookerStorage.h>
#include <Oxygen/Cooker/Import/Internal/LooseCookedWriter.h>
#include <Oxygen/Cooker/Pak/PakBuildRequest.h>
#include <Oxygen/Cooker/Tools/PakTool/ScriptSealing.h>
#include <Oxygen/Cooker/Tools/PakTool/StagedSourceStorage.h>
#include <Oxygen/Cooker/test/Support/CookedFiles.h>
#include <Oxygen/Cooker/test/Support/FileIo.h>
#include <Oxygen/Cooker/testing/MemoryFileSystem.h>
#include <Oxygen/Cooker/testing/ScriptBuilder.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/CookedSource.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/Serio/ByteReader.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Testing/ScopedTempDir.h>

namespace {

using oxygen::content::import::LooseCookedWriter;
using oxygen::content::pak::BuildMode;
using oxygen::content::pak::PakBuildRequest;
using oxygen::content::pak::tool::CleanupStagedFiles;
using oxygen::content::pak::tool::SealLooseCookedSourcesForPakBuild;
using oxygen::content::pak::tool::SourceMountFolders;
using oxygen::content::pak::tool::StagedSourceStorage;
using oxygen::cooker::testing::ScriptBuilder;
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
constexpr auto kNotMountPath
  = std::string_view { "paktool.script_seal.external_source_not_mount_path" };

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
    auto out
      = std::ofstream(oxygen::base::ToNativePath(path), std::ios::binary);
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

    auto bytes = std::array<char, sizeof(ScriptAssetDesc)> {};
    in.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!in.good() && !in.eof()) {
      throw std::runtime_error(
        "cannot read script descriptor " + path.string());
    }
    const auto descriptor = oxygen::serio::CopyAt<ScriptAssetDesc>(
      std::as_bytes(std::span { bytes }));
    if (!descriptor) {
      throw std::runtime_error("short script descriptor " + path.string());
    }
    return *descriptor;
  }

  //! Seals a loose-cooked source whose script descriptor stores `stored` as
  //! its external source path, with `content_root` as the `Content` source
  //! mount, and expects sealing to fail with `code`.
  auto ExpectSealingRejectsStoredPath(const std::string& stored,
    const std::string_view cooked_name,
    const std::filesystem::path& content_root,
    const std::string_view code) const -> void
  {
    const auto cooked = Root() / cooked_name;
    auto script = ScriptBuilder { "rooted" };
    script.Flags(ScriptAssetFlags::kAllowExternalSource).ExternalSource(stored);
    LooseCookedWriter writer(
      cooked, oxygen::content::cooked::RealCookerStorage());
    const auto virtual_path = std::string("/.cooked/rooted.oscript");
    writer.WriteAssetDescriptor(AssetKey::FromVirtualPath(virtual_path),
      AssetType::kScript, virtual_path, "rooted.oscript", script.Bytes(),
      script.References(),
      oxygen::content::import::LooseCookedProducer {
        .source = "/Content/rooted.lua",
        .completes = {},
      });
    static_cast<void>(writer.Finish());
    PakBuildRequest request {};
    request.mode = BuildMode::kFull;
    request.sources = {
      CookedSource {
        .kind = CookedSourceKind::kLooseCooked,
        .path = cooked,
      },
    };
    auto staged
      = StagedSourceStorage(oxygen::content::cooked::RealCookerStorage());
    const auto sealed
      = SealLooseCookedSourcesForPakBuild(request, Root() / "staging",
        SourceMountFolders { { "Content", content_root } }, staged);
    ASSERT_FALSE(sealed.has_value()) << stored;
    EXPECT_EQ(sealed.error().error_code, code) << stored;
    EXPECT_EQ(sealed.error().external_source_path, stored);
  }

private:
  oxygen::testing::ScopedTempDir temp_;
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

  const auto script_virtual_path = std::string("/.cooked/proc_cubes.oscript");
  const auto script_relpath = std::string("proc_cubes.oscript");
  const auto script_key = AssetKey::FromVirtualPath(script_virtual_path);

  const auto stored_external_path
    = std::string("/Content/scenes/proc-cubes/proc_cubes.lua");
  auto script = ScriptBuilder { "proc_cubes" };
  script.Flags(ScriptAssetFlags::kAllowExternalSource)
    .ExternalSource(stored_external_path);

  auto writer = LooseCookedWriter(
    cooked_root, oxygen::content::cooked::RealCookerStorage());
  const auto source_key = SourceKey::FromString(kSourceKey);
  ASSERT_HAS_VALUE(source_key);
  writer.SetSourceKey(source_key.value());
  writer.SetContentVersion(7);
  writer.WriteAssetDescriptor(script_key, AssetType::kScript,
    script_virtual_path, script_relpath, script.Bytes(), script.References(),
    oxygen::content::import::LooseCookedProducer {
      .source = "/Content/script.lua",
      .completes = {},
    });
  static_cast<void>(writer.Finish());
  // Sealing rewrites the descriptor in place, under the content name the
  // original index lists.
  const auto script_content_relpath
    = oxygen::cooker::test::CookedFile(cooked_root, script_relpath)
        .lexically_relative(cooked_root);

  auto request = PakBuildRequest {};
  request.mode = BuildMode::kFull;
  request.sources = {
    CookedSource {
      .kind = CookedSourceKind::kLooseCooked,
      .path = cooked_root,
    },
  };

  auto& storage = oxygen::content::cooked::RealCookerStorage();
  auto staged = StagedSourceStorage(storage);
  const auto sealed
    = SealLooseCookedSourcesForPakBuild(request, Root() / "staging",
      SourceMountFolders { { "Content", content_root } }, staged);
  ASSERT_HAS_VALUE(sealed) << sealed.error().error_code << ": "
                           << sealed.error().error_message;
  ASSERT_EQ(sealed->sealed_script_assets, 1U);
  ASSERT_EQ(sealed->staged_loose_roots.size(), 1U);
  ASSERT_EQ(sealed->build_request.sources.size(), 1U);

  const auto& staged_root = sealed->build_request.sources.at(0).path;
  const auto staged_parent = staged_root.parent_path();
  EXPECT_NE(staged_root, cooked_root);
  EXPECT_TRUE(std::filesystem::exists(
    oxygen::base::ToNativePath(staged_root / "container.index.bin")));

  const auto staged_descriptor
    = ReadScriptAssetDescriptor(staged_root / script_content_relpath);
  EXPECT_NE(staged_descriptor.source_resource_index, kNoResourceReference);
  EXPECT_EQ(static_cast<uint32_t>(staged_descriptor.flags), 0U);
  EXPECT_EQ(staged_descriptor.external_source_path_offset, 0U);
  // The sealed descriptor's string table no longer holds the external path.
  const auto staged_bytes
    = oxygen::cooker::test::ReadBytes(staged_root / script_content_relpath);
  const auto path_bytes = std::as_bytes(std::span(stored_external_path));
  EXPECT_TRUE(std::ranges::search(staged_bytes, path_bytes).empty());

  const auto scripts_table_path = staged_root / "Resources" / "scripts.table";
  const auto scripts_data_path = staged_root / "Resources" / "scripts.data";
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
    auto table_bytes = std::vector<char>(table_size);
    table_in.read(table_bytes.data(), static_cast<std::streamsize>(table_size));
    auto table_entries = std::vector<ScriptResourceDesc> {};
    for (size_t row = 0; row < 2U; ++row) {
      const auto entry = oxygen::serio::CopyAt<ScriptResourceDesc>(
        std::as_bytes(std::span { table_bytes }),
        oxygen::serio::ByteOffset { row * sizeof(ScriptResourceDesc) });
      ASSERT_HAS_VALUE(entry);
      table_entries.push_back(*entry);
    }

    const auto index
      = oxygen::content::lc::LooseCookedIndex::LoadFromRoot(staged_root);
    const auto references = index.FindAssetReferences(script_key);
    ASSERT_HAS_VALUE(references);
    const auto resolved
      = references->ResolveResource(staged_descriptor.source_resource_index,
        oxygen::data::ResourceKind::kScript);
    ASSERT_HAS_VALUE(resolved);
    ASSERT_HAS_VALUE(*resolved);
    const auto source_index = (**resolved).get();
    ASSERT_EQ(source_index, 1U);
    EXPECT_EQ(table_entries.at(source_index).encoding, ScriptEncoding::kSource);
    EXPECT_GT(table_entries.at(source_index).size_bytes, 0U);
  }

  {
    const auto staged_index
      = oxygen::content::lc::LooseCookedIndex::LoadFromRoot(staged_root);
    const auto descriptor_size = staged_index.FindDescriptorSize(script_key);
    ASSERT_HAS_VALUE(descriptor_size);
    // The descriptor and its string table, which holds the empty string and
    // the script's name, and no longer the external source path.
    EXPECT_EQ(descriptor_size.value_or(0U),
      sizeof(ScriptAssetDesc) + 1U + script.Name().size() + 1U);
    const auto file_relpath = staged_index.FindFileRelPath(
      oxygen::data::loose_cooked::FileKind::kScriptsTable);
    ASSERT_HAS_VALUE(file_relpath);
    EXPECT_EQ(file_relpath.value_or(""), "Resources/scripts.table");
  }

  CleanupStagedFiles(storage, sealed->staged_files);
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

  ExpectSealingRejectsStoredPath("\\" + outside.filename().generic_string(),
    "rooted-script-root-relative", content_root, kNotMountPath);
}

NOLINT_TEST_P(PakToolScriptSealingTest, RejectsDriveRelativeScriptPath)
{
#ifdef _WIN32
  const auto content_root = Root() / "Content";
  WriteTextFile(content_root / "inside.lua", "return 2");

  ExpectSealingRejectsStoredPath(
    content_root.root_name().generic_string() + "inside.lua",
    "rooted-script-drive-relative", content_root, kNotMountPath);
#else
  // root_name() is empty on POSIX, so the stored path would be a plain
  // relative path rather than a drive-relative one.
  GTEST_SKIP() << "Drive-relative paths exist only on Windows";
#endif
}

//! A stored path that is not `/<Mount>/<path in mount>`, or that leaves its
//! mount, never seals.
NOLINT_TEST_P(PakToolScriptSealingTest, RejectsPathsThatAreNotMountPaths)
{
  const auto content_root = Root() / "Content";
  WriteTextFile(content_root / "inside.lua", "return 2");
  WriteTextFile(Root() / "outside.lua", "return 1");

  ExpectSealingRejectsStoredPath(
    "inside.lua", "relative-script", content_root, kNotMountPath);
  ExpectSealingRejectsStoredPath(
    "/Content/../outside.lua", "escaping-script", content_root, kNotMountPath);
  ExpectSealingRejectsStoredPath(
    "/Content/", "folder-script", content_root, kNotMountPath);
}

//! A script whose mount nothing binds names the mount, not a missing file.
NOLINT_TEST_P(PakToolScriptSealingTest, RejectsScriptInUnboundMount)
{
  const auto content_root = Root() / "Content";
  WriteTextFile(content_root / "inside.lua", "return 2");

  ExpectSealingRejectsStoredPath("/Other/inside.lua", "unbound-script",
    content_root, "paktool.script_seal.external_source_mount_unbound");
}

NOLINT_TEST_P(PakToolScriptSealingTest, RejectsMissingScriptInBoundMount)
{
  const auto content_root = Root() / "Content";
  std::filesystem::create_directories(oxygen::base::ToNativePath(content_root));

  ExpectSealingRejectsStoredPath("/Content/missing.lua", "missing-script",
    content_root, "paktool.script_seal.external_source_unresolvable");
}

INSTANTIATE_TEST_SUITE_P(PathLengths, PakToolScriptSealingTest, testing::Bool(),
  [](const testing::TestParamInfo<bool>& info) -> const char* {
    return info.param ? "LongPaths" : "ShortPaths";
  });

//! Sealing stages only what it changes: underneath, the staged root holds the
//! sealed descriptor, the script tables and the index; through the staged
//! storage, it shows every other file of the original root, which stays as it
//! was. Cleanup removes exactly what sealing staged.
NOLINT_TEST(
  PakToolScriptSealingOverlayTest, StagedRootHoldsOnlyWhatSealingChanged)
{
  using oxygen::content::cooked::PathKind;

  // Arrange
  auto memory = oxygen::cooker::testing::MemoryFileSystem {};
  // An absolute path, as sealing requires; nothing is written to disk.
  const auto base = std::filesystem::temp_directory_path() / "memory";
  const auto content = base / "Content";
  const auto cooked = base / "cooked";
  memory.AddFile(content / "spin.lua", "return 1");
  constexpr auto kHull = std::string_view { "hull" };
  memory.AddFile(cooked / "Ship/Hull.obuf", kHull);
  auto script = ScriptBuilder { "spin" };
  script.Flags(ScriptAssetFlags::kAllowExternalSource)
    .ExternalSource("/Content/spin.lua");
  {
    auto writer = LooseCookedWriter(cooked, memory);
    writer.WriteAssetDescriptor(
      AssetKey::FromVirtualPath("/.cooked/spin.oscript"), AssetType::kScript,
      "/.cooked/spin.oscript", "spin.oscript", script.Bytes(),
      script.References(),
      oxygen::content::import::LooseCookedProducer {
        .source = "/Content/spin.lua",
        .completes = {},
      });
    writer.RegisterExternalAuxiliaryFile("Ship/Hull.obuf", {},
      oxygen::content::import::LooseCookedProducer {
        .source = "/Content/Ship.glb",
        .completes = {},
      },
      oxygen::content::import::LooseCookedFileDigest {
        .size = kHull.size(),
        .sha256 = oxygen::base::ComputeSha256(std::as_bytes(std::span(kHull))),
      });
    static_cast<void>(writer.Finish());
  }
  // Sealing rewrites the descriptor in place, under the content name the
  // original index lists.
  const auto spin_relpath
    = oxygen::cooker::test::CookedFile(cooked, "spin.oscript", memory)
        .lexically_relative(cooked);
  auto request = PakBuildRequest {};
  request.mode = BuildMode::kFull;
  request.sources = {
    CookedSource { .kind = CookedSourceKind::kLooseCooked, .path = cooked },
  };
  auto staged = StagedSourceStorage(memory);

  // Act
  const auto sealed = SealLooseCookedSourcesForPakBuild(request,
    base / "staging", SourceMountFolders { { "Content", content } }, staged);

  // Assert
  ASSERT_HAS_VALUE(sealed) << sealed.error().error_code << ": "
                           << sealed.error().error_message;
  const auto& staged_root = sealed->build_request.sources.at(0).path;
  EXPECT_EQ(memory.KindOf(staged_root / spin_relpath), PathKind::kFile);
  EXPECT_EQ(memory.KindOf(staged_root / "Ship/Hull.obuf"), PathKind::kNone);
  EXPECT_EQ(staged.KindOf(staged_root / "Ship/Hull.obuf"), PathKind::kFile);
  const auto index_bytes = oxygen::content::cooked::ReadWholeFile(staged,
    staged_root / oxygen::data::loose_cooked::kIndexFileName,
    oxygen::content::cooked::kMaxMetadataBytes);
  ASSERT_HAS_VALUE(index_bytes);
  const auto index
    = oxygen::content::lc::LooseCookedIndex::LoadFromBytes(*index_bytes);
  const auto inventory = index.GetFileInventory();
  EXPECT_TRUE(std::ranges::any_of(inventory, [](const auto& file) -> bool {
    return file.relative_path == "Ship/Hull.obuf";
  }));

  // Cleanup removes the staged root and the sealed sources folder; the
  // staging parent, the pak's output folder, stays.
  CleanupStagedFiles(memory, sealed->staged_files);
  EXPECT_EQ(memory.KindOf(staged_root), PathKind::kNone);
  EXPECT_EQ(memory.KindOf(staged_root.parent_path()), PathKind::kNone);
  EXPECT_EQ(memory.KindOf(base / "staging"), PathKind::kFolder);
  EXPECT_EQ(memory.KindOf(cooked / "Ship/Hull.obuf"), PathKind::kFile);
}

} // namespace
