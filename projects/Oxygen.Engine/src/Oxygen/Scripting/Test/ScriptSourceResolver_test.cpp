//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Config/PathFinder.h>
#include <Oxygen/Config/PathFinderConfig.h>
#include <Oxygen/Data/ScriptAsset.h>
#include <Oxygen/Data/ScriptResource.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/Graphics/Common/BackendModule.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Loader/GraphicsBackendLoader.h>
#include <Oxygen/Platform/Platform.h>
#include <Oxygen/Scripting/Module/ScriptingModule.h>
#include <Oxygen/Scripting/Resolver/ScriptSourceResolver.h>

namespace oxygen::scripting::test {

namespace {

  struct ScriptAssetResourceIndices {
    uint32_t bytecode_index { data::pak::kNoResourceIndex };
    uint32_t source_index { data::pak::kNoResourceIndex };
  };

  auto MakeScriptAsset(std::string_view external_source,
    const ScriptAssetResourceIndices indices,
    const data::pak::ScriptAssetFlags flags)
    -> std::shared_ptr<data::ScriptAsset>
  {
    data::pak::ScriptAssetDesc desc {};
    desc.header.asset_type = static_cast<uint8_t>(data::AssetType::kScript);
    desc.bytecode_resource_index = indices.bytecode_index;
    desc.source_resource_index = indices.source_index;
    desc.flags = flags;
    if (!external_source.empty()) {
      auto path_span = std::span(desc.external_source_path);
      auto writable = path_span.first(path_span.size() - 1);
      const auto src_view = external_source | std::views::take(writable.size());
      const auto out_it = std::ranges::copy(src_view, writable.begin()).out;
      *out_it = '\0';
    }
    return std::make_shared<data::ScriptAsset>(
      data::AssetKey { 1 }, desc, std::vector<data::pak::ScriptParamRecord> {});
  }

  auto MakeScriptResource(const std::vector<uint8_t>& bytes,
    const data::pak::ScriptEncoding encoding, const uint64_t hash = 0)
    -> std::shared_ptr<const data::ScriptResource>
  {
    data::pak::ScriptResourceDesc desc {};
    desc.size_bytes = static_cast<uint32_t>(bytes.size());
    desc.language = data::pak::ScriptLanguage::kLuau;
    desc.encoding = encoding;
    desc.compression = data::pak::ScriptCompression::kNone;
    desc.content_hash = hash;
    return std::make_shared<const data::ScriptResource>(desc, bytes);
  }

} // namespace

class ScriptSourceResolverTest : public ::testing::Test {
protected:
  void SetUp() override
  {
    const auto timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::system_clock::now().time_since_epoch())
                             .count();
    temp_root_ = std::filesystem::temp_directory_path()
      / std::string("oxygen_scripting_resolver_test_")
          .append(std::to_string(timestamp));
    std::filesystem::create_directories(temp_root_ / "scripts");

    auto config = PathFinderConfig::Create()
                    .WithWorkspaceRoot(temp_root_)
                    .AddScriptSourceRoot("scripts")
                    .BuildShared();
    auto path_finder = PathFinder(config, temp_root_);
    resolver_ = std::make_unique<ScriptSourceResolver>(std::move(path_finder));
  }

  void TearDown() override
  {
    std::error_code ec;
    std::filesystem::remove_all(temp_root_, ec);
  }

  [[nodiscard]] auto TempRoot() const noexcept -> const std::filesystem::path&
  {
    return temp_root_;
  }

  [[nodiscard]] auto Resolver() const noexcept -> ScriptSourceResolver&
  {
    return *resolver_;
  }

  static auto WriteFile(
    const std::filesystem::path& path, std::string_view contents) -> void
  {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::out | std::ios::binary);
    out << contents;
  }

private:
  std::filesystem::path temp_root_;
  std::unique_ptr<ScriptSourceResolver> resolver_;
};

NOLINT_TEST_F(
  ScriptSourceResolverTest, ResolvePrefersEmbeddedBytecodeOverSourceAndExternal)
{
  const auto asset = MakeScriptAsset("external.luau",
    ScriptAssetResourceIndices {
      .bytecode_index = 1,
      .source_index = 2,
    },
    data::pak::ScriptAssetFlags::kAllowExternalSource);

  auto bytecode = MakeScriptResource(
    std::vector<uint8_t> { 'b', 'c' }, data::pak::ScriptEncoding::kBytecode);
  auto source = MakeScriptResource(
    std::vector<uint8_t> { 's', 'r', 'c' }, data::pak::ScriptEncoding::kSource);

  const auto result = Resolver().Resolve({
    .asset = *asset,
    .load_script_resource =
      [bytecode, source](const uint32_t index) {
        if (index == 1) {
          return bytecode;
        }
        if (index == 2) {
          return source;
        }
        return std::shared_ptr<const data::ScriptResource> {};
      },
    .map_resource_origin = {},
  });

  ASSERT_TRUE(result.ok);
  ASSERT_TRUE(result.blob.has_value());
  ASSERT_TRUE(std::holds_alternative<ScriptBytecodeBlob>(*result.blob));
  EXPECT_EQ(std::get<ScriptBytecodeBlob>(*result.blob).Size(), 2);
}

NOLINT_TEST_F(
  ScriptSourceResolverTest, ResolveFallsBackToExternalWhenNoEmbedded)
{
  const auto asset
    = MakeScriptAsset("runtime/fallback", ScriptAssetResourceIndices {},
      data::pak::ScriptAssetFlags::kAllowExternalSource);

  const auto script_path = TempRoot() / "scripts/runtime/fallback.luau";
  WriteFile(script_path, "fallback content");

  const auto result = Resolver().Resolve({
    .asset = *asset,
    .load_script_resource =
      [](const uint32_t) {
        return std::shared_ptr<const data::ScriptResource> {};
      },
    .map_resource_origin = {},
  });

  ASSERT_TRUE(result.ok);
  ASSERT_TRUE(result.blob.has_value());
  ASSERT_TRUE(std::holds_alternative<ScriptSourceBlob>(*result.blob));
  EXPECT_EQ(std::get<ScriptSourceBlob>(*result.blob).Size(), 16);
}

NOLINT_TEST_F(
  ScriptSourceResolverTest, ExternalSourceHashChangesWhenFileChanges)
{
  const auto asset
    = MakeScriptAsset("runtime/hash_test", ScriptAssetResourceIndices {},
      data::pak::ScriptAssetFlags::kAllowExternalSource);

  const auto script_path = TempRoot() / "scripts/runtime/hash_test.luau";

  WriteFile(script_path, "v1");
  const auto first = Resolver().Resolve({
    .asset = *asset,
    .load_script_resource =
      [](const uint32_t) {
        return std::shared_ptr<const data::ScriptResource> {};
      },
    .map_resource_origin = {},
  });
  ASSERT_TRUE(first.ok);
  ASSERT_TRUE(first.blob.has_value());
  const auto first_hash = std::get<ScriptSourceBlob>(*first.blob).ContentHash();

  // Low-resolution timers might need a small delay or forced timestamp update
  std::this_thread::sleep_for(std::chrono::milliseconds(10)); // NOLINT
  WriteFile(script_path, "version 2 is much longer");
  auto now = std::filesystem::last_write_time(script_path);
  std::filesystem::last_write_time(script_path, now + std::chrono::seconds(1));

  const auto second = Resolver().Resolve({
    .asset = *asset,
    .load_script_resource =
      [](const uint32_t) {
        return std::shared_ptr<const data::ScriptResource> {};
      },
    .map_resource_origin = {},
  });
  ASSERT_TRUE(second.ok);
  ASSERT_TRUE(second.blob.has_value());
  ASSERT_TRUE(std::holds_alternative<ScriptSourceBlob>(*second.blob));
  const auto second_hash
    = std::get<ScriptSourceBlob>(*second.blob).ContentHash();

  EXPECT_NE(first_hash, second_hash);
}

NOLINT_TEST_F(ScriptSourceResolverTest, ResolveRejectsAbsoluteExternalPath)
{
  const auto asset
    = MakeScriptAsset("C:/absolute/path.luau", ScriptAssetResourceIndices {},
      data::pak::ScriptAssetFlags::kAllowExternalSource);

  const auto result = Resolver().Resolve({
    .asset = *asset,
    .load_script_resource =
      [](const uint32_t) {
        return std::shared_ptr<const data::ScriptResource> {};
      },
    .map_resource_origin = {},
  });

  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.error_message, "external source path must be relative");
}

NOLINT_TEST_F(ScriptSourceResolverTest, ResolveRejectsParentTraversalPath)
{
  const auto asset
    = MakeScriptAsset("../escape.luau", ScriptAssetResourceIndices {},
      data::pak::ScriptAssetFlags::kAllowExternalSource);

  const auto result = Resolver().Resolve({
    .asset = *asset,
    .load_script_resource =
      [](const uint32_t) {
        return std::shared_ptr<const data::ScriptResource> {};
      },
    .map_resource_origin = {},
  });

  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.error_message,
    "external source path must not contain parent traversal");
}

NOLINT_TEST_F(ScriptSourceResolverTest, ResolveUsesMappedLooseCookedOrigin)
{
  const auto asset = MakeScriptAsset({},
    ScriptAssetResourceIndices {
      .bytecode_index = 3,
      .source_index = data::pak::kNoResourceIndex,
    },
    data::pak::ScriptAssetFlags::kNone);

  auto source = MakeScriptResource(std::vector<uint8_t> { 'l', 'o', 'a', 'd' },
    data::pak::ScriptEncoding::kBytecode);
  const auto result = Resolver().Resolve({
    .asset = *asset,
    .load_script_resource =
      [source](const uint32_t index) {
        if (index == 3) {
          return source;
        }
        return std::shared_ptr<const data::ScriptResource> {};
      },
    .map_resource_origin
    = [](const uint32_t index) -> std::optional<ScriptBlobOrigin> {
      if (index == 3) {
        return ScriptBlobOrigin::kLooseCookedResource;
      }
      return std::nullopt;
    },
  });

  ASSERT_TRUE(result.ok);
  ASSERT_TRUE(result.blob.has_value());
  ASSERT_TRUE(std::holds_alternative<ScriptBytecodeBlob>(*result.blob));
  const auto& bytecode_blob = std::get<ScriptBytecodeBlob>(*result.blob);
  EXPECT_EQ(bytecode_blob.GetOrigin(), ScriptBlobOrigin::kLooseCookedResource);
}

NOLINT_TEST_F(
  ScriptSourceResolverTest, ScriptingModuleExecutesResolvedSourceBlob)
{
  const auto asset
    = MakeScriptAsset("runtime/module_test", ScriptAssetResourceIndices {},
      data::pak::ScriptAssetFlags::kAllowExternalSource);

  const auto script_path = TempRoot() / "scripts/runtime/module_test.luau";
  constexpr auto kScript = "return 42";
  WriteFile(script_path, kScript);

  const auto resolve_result = Resolver().Resolve({
    .asset = *asset,
    .load_script_resource =
      [](const uint32_t) {
        return std::shared_ptr<const data::ScriptResource> {};
      },
    .map_resource_origin = {},
  });

  ASSERT_TRUE(resolve_result.ok);
  const auto& blob = std::get<ScriptSourceBlob>(*resolve_result.blob);

  auto platform
    = std::make_shared<Platform>(PlatformConfig { .headless = true });
  const auto pfc
    = PathFinderConfig::Create().WithWorkspaceRoot(TempRoot()).BuildShared();
  auto& loader = GraphicsBackendLoader::GetInstanceRelaxed();
  auto gfx = loader.LoadBackend(graphics::BackendType::kHeadless,
    GraphicsConfig { .headless = true }, *pfc);

  auto engine = std::make_shared<AsyncEngine>(platform, gfx, EngineConfig {});

  ScriptingModule module(engine::kScriptingModulePriority);
  ASSERT_TRUE(module.OnAttached(observer_ptr { engine.get() }));

  const auto execute_result = module.ExecuteScript(blob);
  EXPECT_TRUE(execute_result.ok) << execute_result.message;

  loader.UnloadBackend();
}

} // namespace oxygen::scripting::test
