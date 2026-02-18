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
#include <unordered_map>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Config/PathFinder.h>
#include <Oxygen/Config/PathFinderConfig.h>
#include <Oxygen/Data/ScriptAsset.h>
#include <Oxygen/Data/ScriptResource.h>
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
      data::AssetKey {}, desc, std::vector<data::pak::ScriptParamRecord> {});
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
                    .WithScriptsRootPath("scripts")
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
  const auto asset = MakeScriptAsset("game/external_script.luau",
    ScriptAssetResourceIndices { .bytecode_index = 1, .source_index = 2 },
    data::pak::ScriptAssetFlags::kAllowExternalSource);

  const auto bytecode
    = MakeScriptResource(std::vector<uint8_t> { 'B', 'C', '0', '1' },
      data::pak::ScriptEncoding::kBytecode);
  const auto source = MakeScriptResource(
    std::vector<uint8_t> { 'p', 'r', 'i', 'n', 't', '(', ')', '\n' },
    data::pak::ScriptEncoding::kSource);

  std::unordered_map<uint32_t, std::shared_ptr<const data::ScriptResource>>
    resources { { 1, bytecode }, { 2, source } };
  const auto result = Resolver().Resolve({
    .asset = *asset,
    .load_script_resource =
      [&resources](const uint32_t index) {
        if (const auto it = resources.find(index); it != resources.end()) {
          return it->second;
        }
        return std::shared_ptr<const data::ScriptResource> {};
      },
    .map_resource_origin = {},
  });

  ASSERT_TRUE(result.ok);
  EXPECT_TRUE(result.blob.IsBytecode());
  EXPECT_TRUE(std::equal(result.blob.bytes.begin(), result.blob.bytes.end(),
    bytecode->GetData().begin(), bytecode->GetData().end()));
  EXPECT_EQ(result.blob.origin, ScriptSourceBlob::Origin::kEmbeddedResource);
}

NOLINT_TEST_F(
  ScriptSourceResolverTest, ResolveFallsBackToExternalWhenNoEmbedded)
{
  const auto asset
    = MakeScriptAsset("runtime/hook", ScriptAssetResourceIndices {},
      data::pak::ScriptAssetFlags::kAllowExternalSource);

  const auto script_path = TempRoot() / "scripts/runtime/hook.luau";
  constexpr auto kScript = "x = 2\nx = x + 3\n";
  WriteFile(script_path, kScript);

  const auto result = Resolver().Resolve({
    .asset = *asset,
    .load_script_resource =
      [](const uint32_t) {
        return std::shared_ptr<const data::ScriptResource> {};
      },
    .map_resource_origin = {},
  });

  ASSERT_TRUE(result.ok);
  EXPECT_TRUE(result.blob.IsSource());
  EXPECT_EQ(result.blob.origin, ScriptSourceBlob::Origin::kExternalFile);
  EXPECT_EQ(std::string(result.blob.bytes.begin(), result.blob.bytes.end()),
    std::string(kScript));
}

NOLINT_TEST_F(ScriptSourceResolverTest, ResolveRejectsAbsoluteExternalPath)
{
  const auto absolute = std::filesystem::absolute("runtime/abspath.luau");
  const auto asset
    = MakeScriptAsset(absolute.generic_string(), ScriptAssetResourceIndices {},
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
    = [](const uint32_t index) -> std::optional<ScriptSourceBlob::Origin> {
      if (index == 3) {
        return ScriptSourceBlob::Origin::kLooseCookedResource;
      }
      return std::nullopt;
    },
  });

  ASSERT_TRUE(result.ok);
  EXPECT_EQ(result.blob.origin, ScriptSourceBlob::Origin::kLooseCookedResource);
}

NOLINT_TEST_F(
  ScriptSourceResolverTest, ScriptingModuleExecutesResolvedSourceBlob)
{
  const auto asset
    = MakeScriptAsset("runtime/module_test", ScriptAssetResourceIndices {},
      data::pak::ScriptAssetFlags::kAllowExternalSource);

  const auto script_path = TempRoot() / "scripts/runtime/module_test.luau";
  constexpr auto kScript = "counter = 1\ncounter = counter + 1\n";
  WriteFile(script_path, kScript);

  const auto resolved = Resolver().Resolve({
    .asset = *asset,
    .load_script_resource =
      [](const uint32_t) {
        return std::shared_ptr<const data::ScriptResource> {};
      },
    .map_resource_origin = {},
  });
  ASSERT_TRUE(resolved.ok);
  ASSERT_TRUE(resolved.blob.IsSource());

  ScriptingModule module { engine::ModulePriority { 100U } }; // NOLINT
  ASSERT_TRUE(module.OnAttached(observer_ptr<AsyncEngine> {}));
  const auto execute_result = module.ExecuteScript(resolved.blob);
  EXPECT_TRUE(execute_result.ok);
  EXPECT_EQ(execute_result.stage, "ok");
}

} // namespace oxygen::scripting::test
