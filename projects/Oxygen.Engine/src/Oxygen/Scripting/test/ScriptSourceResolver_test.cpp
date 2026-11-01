//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Config/EngineConfig.h>
#include <Oxygen/Config/PathFinder.h>
#include <Oxygen/Config/PathFinderConfig.h>
#include <Oxygen/Config/PlatformConfig.h>
#include <Oxygen/Core/EngineModule.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Data/ScriptAsset.h>
#include <Oxygen/Data/ScriptResource.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/Engine/Scripting/ScriptBytecodeBlob.h>
#include <Oxygen/Engine/Scripting/ScriptSourceBlob.h>
#include <Oxygen/Graphics/Common/BackendModule.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Loader/GraphicsBackendLoader.h>
#include <Oxygen/Platform/Platform.h>
#include <Oxygen/Scripting/Module/ScriptingModule.h>
#include <Oxygen/Scripting/Resolver/ScriptSourceResolver.h>
#include <Oxygen/Serio/ByteBuffer.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Testing/TempDirTest.h>

namespace oxygen::scripting::test {

namespace {

  constexpr auto kNotMountPath
    = std::string_view { "external source path must be a mount path, "
                         "/<Mount>/<path in mount>, that stays in its mount" };

  auto MakeAssetKey(const std::uint8_t seed) -> data::AssetKey
  {
    auto bytes = std::array<std::uint8_t, data::AssetKey::kSizeBytes> {};
    bytes[0] = seed;
    return data::AssetKey::FromBytes(bytes);
  }

  struct ScriptAssetResourceIndices {
    data::ResourceReferenceIndex bytecode_index { data::kNoResourceReference };
    data::ResourceReferenceIndex source_index { data::kNoResourceReference };
  };

  auto MakeScriptAsset(std::string_view external_source,
    const ScriptAssetResourceIndices indices,
    const data::pak::scripting::ScriptAssetFlags flags)
    -> std::shared_ptr<data::ScriptAsset>
  {
    data::pak::scripting::ScriptAssetDesc desc {};
    desc.header.asset_type = static_cast<uint8_t>(data::AssetType::kScript);
    desc.bytecode_resource_index = indices.bytecode_index;
    desc.source_resource_index = indices.source_index;
    desc.flags = flags;
    return std::make_shared<data::ScriptAsset>(MakeAssetKey(1U), desc,
      data::ScriptParameters {}, std::string { external_source });
  }

  auto MakeScriptResource(const std::vector<uint8_t>& bytes,
    const data::pak::scripting::ScriptEncoding encoding,
    const uint64_t hash = 0) -> std::shared_ptr<const data::ScriptResource>
  {
    data::pak::scripting::ScriptResourceDesc desc {};
    desc.size_bytes = static_cast<uint32_t>(bytes.size());
    desc.language = data::pak::scripting::ScriptLanguage::kLuau;
    desc.encoding = encoding;
    desc.compression = data::pak::scripting::ScriptCompression::kNone;
    desc.content_hash = hash;
    auto buffer = serio::ByteBuffer::CopyOf(std::as_bytes(std::span { bytes }));
    if (!buffer) {
      throw std::bad_alloc();
    }
    return std::make_shared<const data::ScriptResource>(
      desc, std::move(buffer).value());
  }

} // namespace

class ScriptSourceResolverTest : public oxygen::testing::TempDirTest {
protected:
  void SetUp() override
  {
    std::filesystem::create_directories(TempDir() / "scripts");

    auto config = PathFinderConfig::Create()
                    .WithWorkspaceRoot(TempDir())
                    .WithSourceMount("Content", "scripts")
                    .BuildShared();
    auto path_finder = PathFinder(config, TempDir());
    resolver_ = std::make_unique<ScriptSourceResolver>(std::move(path_finder));
  }

  [[nodiscard]] auto Resolver() const noexcept -> ScriptSourceResolver&
  {
    return *resolver_;
  }

  static auto WriteFile(
    const std::filesystem::path& path, std::string_view contents) -> void
  {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << contents;
  }

private:
  std::unique_ptr<ScriptSourceResolver> resolver_;
};

NOLINT_TEST_F(
  ScriptSourceResolverTest, ResolvePrefersEmbeddedBytecodeOverSourceAndExternal)
{
  const auto asset = MakeScriptAsset("/Content/external.luau",
    ScriptAssetResourceIndices {
      .bytecode_index = data::ResourceReferenceIndex { 1U },
      .source_index = data::ResourceReferenceIndex { 2U },
    },
    data::pak::scripting::ScriptAssetFlags::kAllowExternalSource);

  auto bytecode = MakeScriptResource(std::vector<uint8_t> { 'b', 'c' },
    data::pak::scripting::ScriptEncoding::kBytecode);
  auto source = MakeScriptResource(std::vector<uint8_t> { 's', 'r', 'c' },
    data::pak::scripting::ScriptEncoding::kSource);

  const auto result = Resolver().Resolve({
    .asset = *asset,
    .load_script_resource =
      [bytecode, source](const data::ResourceReferenceIndex index) {
        if (index.get() == 1) {
          return bytecode;
        }
        if (index.get() == 2) {
          return source;
        }
        return std::shared_ptr<const data::ScriptResource> {};
      },
    .map_resource_origin = {},
  });

  ASSERT_TRUE(result.ok);
  ASSERT_HAS_VALUE(result.blob);
  ASSERT_TRUE(std::holds_alternative<ScriptBytecodeBlob>(*result.blob));
  EXPECT_EQ(std::get<ScriptBytecodeBlob>(*result.blob).Size(), 2);
}

NOLINT_TEST_F(
  ScriptSourceResolverTest, ResolveFallsBackToExternalWhenNoEmbedded)
{
  const auto asset = MakeScriptAsset("/Content/runtime/fallback",
    ScriptAssetResourceIndices {},
    data::pak::scripting::ScriptAssetFlags::kAllowExternalSource);

  const auto script_path = TempDir() / "scripts/runtime/fallback.luau";
  WriteFile(script_path, "fallback content");

  const auto result = Resolver().Resolve({
    .asset = *asset,
    .load_script_resource =
      [](const data::ResourceReferenceIndex) {
        return std::shared_ptr<const data::ScriptResource> {};
      },
    .map_resource_origin = {},
  });

  ASSERT_TRUE(result.ok);
  ASSERT_HAS_VALUE(result.blob);
  ASSERT_TRUE(std::holds_alternative<ScriptSourceBlob>(*result.blob));
  EXPECT_EQ(std::get<ScriptSourceBlob>(*result.blob).Size(), 16);
}

//! An external script resolves while an editor holds it open for writing:
//! the resolver shares writes, so a hot reload never fails on sharing.
NOLINT_TEST_F(
  ScriptSourceResolverTest, ExternalSourceResolvesWhileAnEditorHoldsIt)
{
  const auto asset
    = MakeScriptAsset("/Content/runtime/edited", ScriptAssetResourceIndices {},
      data::pak::scripting::ScriptAssetFlags::kAllowExternalSource);
  const auto script_path = TempDir() / "scripts/runtime/edited.luau";
  WriteFile(script_path, "edited content");
  // An editor's handle: open for writing, sharing reads and writes.
  constexpr auto mode
    = static_cast<std::ios::openmode>(static_cast<unsigned>(std::ios::binary)
      | static_cast<unsigned>(std::ios::app));
  auto editor = std::ofstream(script_path, mode);
  ASSERT_TRUE(editor.is_open());

  const auto result = Resolver().Resolve({
    .asset = *asset,
    .load_script_resource =
      [](const data::ResourceReferenceIndex) {
        return std::shared_ptr<const data::ScriptResource> {};
      },
    .map_resource_origin = {},
  });

  ASSERT_TRUE(result.ok) << result.error_message;
  ASSERT_HAS_VALUE(result.blob);
  ASSERT_TRUE(std::holds_alternative<ScriptSourceBlob>(*result.blob));
  EXPECT_EQ(std::get<ScriptSourceBlob>(*result.blob).Size(), 14);
}

NOLINT_TEST_F(
  ScriptSourceResolverTest, ExternalSourceHashChangesWhenFileChanges)
{
  const auto asset = MakeScriptAsset("/Content/runtime/hash_test",
    ScriptAssetResourceIndices {},
    data::pak::scripting::ScriptAssetFlags::kAllowExternalSource);

  const auto script_path = TempDir() / "scripts/runtime/hash_test.luau";

  WriteFile(script_path, "v1");
  const auto first = Resolver().Resolve({
    .asset = *asset,
    .load_script_resource =
      [](const data::ResourceReferenceIndex) {
        return std::shared_ptr<const data::ScriptResource> {};
      },
    .map_resource_origin = {},
  });
  ASSERT_TRUE(first.ok);
  ASSERT_HAS_VALUE(first.blob);
  const auto first_hash = std::get<ScriptSourceBlob>(*first.blob).ContentHash();

  // Low-resolution timers might need a small delay or forced timestamp update
  std::this_thread::sleep_for(std::chrono::milliseconds(10)); // NOLINT
  WriteFile(script_path, "version 2 is much longer");
  auto now = std::filesystem::last_write_time(script_path);
  std::filesystem::last_write_time(script_path, now + std::chrono::seconds(1));

  const auto second = Resolver().Resolve({
    .asset = *asset,
    .load_script_resource =
      [](const data::ResourceReferenceIndex) {
        return std::shared_ptr<const data::ScriptResource> {};
      },
    .map_resource_origin = {},
  });
  ASSERT_TRUE(second.ok);
  ASSERT_HAS_VALUE(second.blob);
  ASSERT_TRUE(std::holds_alternative<ScriptSourceBlob>(*second.blob));
  const auto second_hash
    = std::get<ScriptSourceBlob>(*second.blob).ContentHash();

  EXPECT_NE(first_hash, second_hash);
}

NOLINT_TEST_F(ScriptSourceResolverTest, ResolveRejectsPathThatIsNotMountPath)
{
  const auto asset
    = MakeScriptAsset("C:/absolute/path.luau", ScriptAssetResourceIndices {},
      data::pak::scripting::ScriptAssetFlags::kAllowExternalSource);

  const auto result = Resolver().Resolve({
    .asset = *asset,
    .load_script_resource =
      [](const data::ResourceReferenceIndex) {
        return std::shared_ptr<const data::ScriptResource> {};
      },
    .map_resource_origin = {},
  });

  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.error_message, kNotMountPath);

  const auto relative
    = MakeScriptAsset("runtime/fallback.luau", ScriptAssetResourceIndices {},
      data::pak::scripting::ScriptAssetFlags::kAllowExternalSource);
  WriteFile(TempDir() / "scripts/runtime/fallback.luau", "relative");
  const auto relative_result = Resolver().Resolve({
    .asset = *relative,
    .load_script_resource =
      [](const data::ResourceReferenceIndex) {
        return std::shared_ptr<const data::ScriptResource> {};
      },
    .map_resource_origin = {},
  });
  EXPECT_FALSE(relative_result.ok);
  EXPECT_EQ(relative_result.error_message, kNotMountPath);
}

//! A mount this run does not bind falls back silently to the embedded
//! bytecode, like a missing file, even when another mount holds the path.
NOLINT_TEST_F(ScriptSourceResolverTest, ResolveFallsThroughForUnboundMount)
{
  WriteFile(TempDir() / "scripts/runtime/unbound.luau", "unbound");
  const auto asset = MakeScriptAsset("/Other/runtime/unbound.luau",
    ScriptAssetResourceIndices {
      .bytecode_index = data::ResourceReferenceIndex { 1U },
    },
    data::pak::scripting::ScriptAssetFlags::kAllowExternalSource);
  auto bytecode = MakeScriptResource(std::vector<uint8_t> { 'b', 'c' },
    data::pak::scripting::ScriptEncoding::kBytecode);

  const auto result = Resolver().Resolve({
    .asset = *asset,
    .load_script_resource =
      [bytecode](const data::ResourceReferenceIndex index) {
        return index.get() == 1
          ? bytecode
          : std::shared_ptr<const data::ScriptResource> {};
      },
    .map_resource_origin = {},
  });

  ASSERT_TRUE(result.ok) << result.error_message;
  ASSERT_HAS_VALUE(result.blob);
  EXPECT_TRUE(std::holds_alternative<ScriptBytecodeBlob>(*result.blob));
}

NOLINT_TEST_F(ScriptSourceResolverTest, ResolveRejectsParentTraversalPath)
{
  const auto asset
    = MakeScriptAsset("/Content/../escape.luau", ScriptAssetResourceIndices {},
      data::pak::scripting::ScriptAssetFlags::kAllowExternalSource);

  const auto result = Resolver().Resolve({
    .asset = *asset,
    .load_script_resource =
      [](const data::ResourceReferenceIndex) {
        return std::shared_ptr<const data::ScriptResource> {};
      },
    .map_resource_origin = {},
  });

  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.error_message, kNotMountPath);
}

NOLINT_TEST_F(ScriptSourceResolverTest, ResolveUsesMappedLooseCookedOrigin)
{
  const auto asset = MakeScriptAsset({},
    ScriptAssetResourceIndices {
      .bytecode_index = data::ResourceReferenceIndex { 3U },
      .source_index = data::kNoResourceReference,
    },
    data::pak::scripting::ScriptAssetFlags::kNone);

  auto source = MakeScriptResource(std::vector<uint8_t> { 'l', 'o', 'a', 'd' },
    data::pak::scripting::ScriptEncoding::kBytecode);
  const auto result = Resolver().Resolve({
    .asset = *asset,
    .load_script_resource =
      [source](const data::ResourceReferenceIndex index) {
        if (index.get() == 3) {
          return source;
        }
        return std::shared_ptr<const data::ScriptResource> {};
      },
    .map_resource_origin = [](const data::ResourceReferenceIndex index)
      -> std::optional<ScriptBlobOrigin> {
      if (index.get() == 3) {
        return ScriptBlobOrigin::kLooseCookedResource;
      }
      return std::nullopt;
    },
  });

  ASSERT_TRUE(result.ok);
  ASSERT_HAS_VALUE(result.blob);
  ASSERT_TRUE(std::holds_alternative<ScriptBytecodeBlob>(*result.blob));
  const auto& bytecode_blob = std::get<ScriptBytecodeBlob>(*result.blob);
  EXPECT_EQ(bytecode_blob.GetOrigin(), ScriptBlobOrigin::kLooseCookedResource);
}

NOLINT_TEST_F(
  ScriptSourceResolverTest, ScriptingModuleExecutesResolvedSourceBlob)
{
  const auto asset = MakeScriptAsset("/Content/runtime/module_test",
    ScriptAssetResourceIndices {},
    data::pak::scripting::ScriptAssetFlags::kAllowExternalSource);

  const auto script_path = TempDir() / "scripts/runtime/module_test.luau";
  constexpr auto kScript = "return 42";
  WriteFile(script_path, kScript);

  const auto resolve_result = Resolver().Resolve({
    .asset = *asset,
    .load_script_resource =
      [](const data::ResourceReferenceIndex) {
        return std::shared_ptr<const data::ScriptResource> {};
      },
    .map_resource_origin = {},
  });

  ASSERT_TRUE(resolve_result.ok);
  ASSERT_HAS_VALUE(resolve_result.blob);
  const auto& blob = std::get<ScriptSourceBlob>(*resolve_result.blob);

  auto platform
    = std::make_shared<Platform>(PlatformConfig { .headless = true });
  const auto pfc
    = PathFinderConfig::Create().WithWorkspaceRoot(TempDir()).BuildShared();
  auto& loader = GraphicsBackendLoader::GetInstanceRelaxed();
  auto gfx = loader.LoadBackend(graphics::BackendType::kHeadless,
    GraphicsConfig { .headless = true }, *pfc);

  std::shared_ptr<IAsyncEngine> engine
    = std::make_shared<AsyncEngine>(platform, gfx, EngineConfig {});

  ScriptingModule module(engine::kScriptingModulePriority);
  ASSERT_TRUE(module.OnAttached(observer_ptr<IAsyncEngine> { engine.get() }));

  std::string source_text;
  const auto bytes = blob.BytesView();
  source_text.reserve(bytes.size());
  for (const auto byte : bytes) {
    source_text.push_back(static_cast<char>(byte));
  }
  const auto execute_result
    = module.ExecuteScript(oxygen::scripting::ScriptExecutionRequest {
      .source_text = oxygen::scripting::ScriptSourceText { source_text },
      .chunk_name
      = oxygen::scripting::ScriptChunkName { blob.GetCanonicalName().get() },
    });
  EXPECT_TRUE(execute_result.ok) << execute_result.message;

  loader.UnloadBackend();
}

} // namespace oxygen::scripting::test
