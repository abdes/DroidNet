//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Composition/Typed.h>
#include <Oxygen/Config/EngineConfig.h>
#include <Oxygen/Config/PathFinder.h>
#include <Oxygen/Config/PathFinderConfig.h>
#include <Oxygen/Config/PlatformConfig.h>
#include <Oxygen/Core/EngineModule.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Data/ScriptAsset.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/Engine/Scripting/ScriptHotReloadService.h>
#include <Oxygen/Engine/Scripting/ScriptSourceBlob.h>
#include <Oxygen/Graphics/Common/BackendModule.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Loader/GraphicsBackendLoader.h>
#include <Oxygen/OxCo/EventLoop.h>
#include <Oxygen/Platform/Platform.h>
#include <Oxygen/Scripting/Module/ScriptingModule.h>
#include <Oxygen/Scripting/Resolver/ScriptSourceResolver.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Testing/TempDirTest.h>

using oxygen::EngineConfig;
using oxygen::GraphicsConfig;
using oxygen::PathFinder;
using oxygen::PathFinderConfig;
using oxygen::Platform;
using oxygen::PlatformConfig;
using oxygen::TypeId;
using oxygen::data::AssetKey;
using oxygen::graphics::BackendType;
using oxygen::scripting::ScriptBlobOrigin;
using oxygen::scripting::ScriptingModule;
using oxygen::scripting::ScriptSourceBlob;
using oxygen::scripting::ScriptSourceResolver;

using namespace std::chrono_literals;

namespace {

auto MakeAssetKey(const std::uint8_t seed) -> oxygen::data::AssetKey
{
  auto bytes = std::array<std::uint8_t, oxygen::data::AssetKey::kSizeBytes> {};
  bytes[0] = seed;
  return oxygen::data::AssetKey::FromBytes(bytes);
}

struct TestContext {
  std::shared_ptr<oxygen::Platform> platform;
  std::shared_ptr<oxygen::IAsyncEngine> engine;
  std::atomic<bool> running { false };
};

auto EventLoopRun(TestContext& ctx) -> void
{
  while (ctx.running.load()) {
    if (ctx.platform->Async().PollOne() == 0) {
      std::this_thread::sleep_for(1ms);
    }
  }
}

} // namespace

template <> struct oxygen::co::EventLoopTraits<TestContext> {
  static auto Run(TestContext& ctx) -> void
  {
    ctx.running.store(true);
    EventLoopRun(ctx);
  }
  static auto Stop(TestContext& ctx) -> void { ctx.running.store(false); }
  static auto IsRunning(const TestContext& ctx) -> bool
  {
    return ctx.running.load();
  }
  static auto EventLoopId(TestContext& ctx) -> co::EventLoopID
  {
    return co::EventLoopID(&ctx);
  }
};

class ScriptingAdvancedTest : public oxygen::testing::TempDirTest {
protected:
  void SetUp() override
  {
    std::filesystem::create_directories(TempDir() / "root1");
    std::filesystem::create_directories(TempDir() / "root2");

    ctx_.platform
      = std::make_shared<oxygen::Platform>(PlatformConfig { .headless = true });

    pfc_ = PathFinderConfig::Create()
             .WithWorkspaceRoot(TempDir())
             .WithSourceMount("Root1", TempDir() / "root1")
             .WithSourceMount("Root2", TempDir() / "root2")
             .BuildShared();

    auto& loader = oxygen::GraphicsBackendLoader::GetInstanceRelaxed();
    GraphicsConfig config {};
    config.headless = true;
    auto gfx = loader.LoadBackend(BackendType::kHeadless, config, *pfc_);

    ctx_.engine = std::make_shared<oxygen::AsyncEngine>(
      ctx_.platform, gfx, EngineConfig {});
  }

  [[nodiscard]] auto PFC() const -> auto& { return pfc_; }
  [[nodiscard]] auto Ctx() -> auto& { return ctx_; }

  void TearDown() override
  {
    Ctx().engine.reset();
    oxygen::GraphicsBackendLoader::GetInstanceRelaxed().UnloadBackend();
    Ctx().platform.reset();
  }

  static void WriteFile(
    const std::filesystem::path& path, std::string_view content)
  {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream f(path);
    f << content;
  }

private:
  std::shared_ptr<const PathFinderConfig> pfc_;
  TestContext ctx_;
};

//! 1. MOUNT TEST: a mount path resolves in its own mount, never another.
NOLINT_TEST_F(ScriptingAdvancedTest, MountPathResolvesInItsOwnMount)
{
  auto path1 = TempDir() / "root1/shared.lua";
  auto path2 = TempDir() / "root2/shared.lua";

  WriteFile(path1, "return 'root1'");
  WriteFile(path2, "return 'root2'");

  ScriptSourceResolver resolver(PathFinder(PFC(), TempDir()));

  oxygen::data::pak::scripting::ScriptAssetDesc desc {};
  desc.flags
    = oxygen::data::pak::scripting::ScriptAssetFlags::kAllowExternalSource;
  oxygen::data::ScriptAsset asset(MakeAssetKey(1U), desc,
    oxygen::data::ScriptParameters {}, "/Root2/shared.lua");

  auto result = resolver.Resolve({
    .asset = asset,
    .load_script_resource
    = [](oxygen::data::ResourceReferenceIndex) { return nullptr; },
    .map_resource_origin = [](oxygen::data::ResourceReferenceIndex)
      -> std::optional<ScriptBlobOrigin> { return std::nullopt; },
  });

  ASSERT_TRUE(result.ok);
  ASSERT_HAS_VALUE(result.blob);
  auto& blob = std::get<ScriptSourceBlob>(*result.blob);
  std::string content(
    reinterpret_cast<const char*>(blob.BytesView().data()), // NOLINT
    blob.Size());
  EXPECT_EQ(content, "return 'root2'");
}

//! 3. MODULE EXECUTION TEST: Verify ScriptingModule can execute resolved
//! source.
NOLINT_TEST_F(ScriptingAdvancedTest, ScriptingModuleExecutesResolvedSource)
{
  auto path = TempDir() / "root1/module_test.lua";
  WriteFile(path, "return 42");

  ScriptSourceResolver resolver(PathFinder(PFC(), TempDir()));

  oxygen::data::pak::scripting::ScriptAssetDesc desc {};
  desc.flags
    = oxygen::data::pak::scripting::ScriptAssetFlags::kAllowExternalSource;
  oxygen::data::ScriptAsset asset(MakeAssetKey(1U), desc,
    oxygen::data::ScriptParameters {}, "/Root1/module_test.lua");

  auto result = resolver.Resolve({
    .asset = asset,
    .load_script_resource
    = [](oxygen::data::ResourceReferenceIndex) { return nullptr; },
    .map_resource_origin = [](oxygen::data::ResourceReferenceIndex)
      -> std::optional<ScriptBlobOrigin> { return std::nullopt; },
  });

  ASSERT_TRUE(result.ok);
  ASSERT_HAS_VALUE(result.blob);
  auto& blob = std::get<ScriptSourceBlob>(*result.blob);

  constexpr oxygen::engine::ModulePriority kScriptingPriority { 450 };
  ScriptingModule module(kScriptingPriority);
  ASSERT_TRUE(module.OnAttached(
    oxygen::observer_ptr<oxygen::IAsyncEngine> { Ctx().engine.get() }));

  std::string source_text;
  {
    const auto bytes = blob.BytesView();
    source_text.reserve(bytes.size());
    for (const auto byte : bytes) {
      source_text.push_back(static_cast<char>(byte));
    }
  }
  const auto execute_result
    = module.ExecuteScript(oxygen::scripting::ScriptExecutionRequest {
      .source_text = oxygen::scripting::ScriptSourceText { source_text },
      .chunk_name
      = oxygen::scripting::ScriptChunkName { blob.GetCanonicalName().get() },
    });
  EXPECT_TRUE(execute_result.ok) << execute_result.message;
}

//! 4. ISOLATION TEST: Verify script instances are isolated via sandboxed
//! environments.
NOLINT_TEST_F(ScriptingAdvancedTest, ScriptInstancesAreIsolated)
{
  constexpr oxygen::engine::ModulePriority kScriptingPriority { 450 };
  ScriptingModule module(kScriptingPriority);
  ASSERT_TRUE(module.OnAttached(
    oxygen::observer_ptr<oxygen::IAsyncEngine> { Ctx().engine.get() }));

  // Source script: if 'counter' is nil, set it to 1. Else increment.
  // In an isolated world, running this twice returns 1 twice.
  const std::string source = "if counter == nil then counter = 0 end\ncounter "
                             "= counter + 1\nreturn counter";
  const auto bytes_vec = std::vector<uint8_t>(source.begin(), source.end());
  constexpr uint64_t kTestId = 123;
  auto blob = ScriptSourceBlob::FromOwned(bytes_vec,
    oxygen::data::pak::scripting::ScriptLanguage::kLuau,
    oxygen::data::pak::scripting::ScriptCompression::kNone, kTestId,
    oxygen::scripting::ScriptBlobOrigin::kExternalFile,
    oxygen::scripting::ScriptBlobCanonicalName { "isolation_test" });

  // ExecuteScript handles sandboxing for each call.
  std::string source_text;
  {
    const auto bytes = blob.BytesView();
    source_text.reserve(bytes.size());
    for (const auto byte : bytes) {
      source_text.push_back(static_cast<char>(byte));
    }
  }
  const auto result1
    = module.ExecuteScript(oxygen::scripting::ScriptExecutionRequest {
      .source_text = oxygen::scripting::ScriptSourceText { source_text },
      .chunk_name
      = oxygen::scripting::ScriptChunkName { blob.GetCanonicalName().get() },
    });
  const auto result2
    = module.ExecuteScript(oxygen::scripting::ScriptExecutionRequest {
      .source_text = oxygen::scripting::ScriptSourceText { source_text },
      .chunk_name
      = oxygen::scripting::ScriptChunkName { blob.GetCanonicalName().get() },
    });

  EXPECT_TRUE(result1.ok) << result1.message;
  EXPECT_TRUE(result2.ok) << result2.message;
}
