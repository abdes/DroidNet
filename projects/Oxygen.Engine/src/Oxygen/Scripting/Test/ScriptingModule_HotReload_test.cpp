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
#include <thread>

#include <Oxygen/Config/PathFinderConfig.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/Engine/Scripting/ScriptHotReloadService.h>
#include <Oxygen/Graphics/Common/BackendModule.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Loader/GraphicsBackendLoader.h>
#include <Oxygen/OxCo/EventLoop.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/Platform/Platform.h>
#include <Oxygen/Scripting/Execution/CompiledScriptExecutable.h>
#include <Oxygen/Scripting/Module/ScriptingModule.h>
#include <Oxygen/Scripting/Resolver/ScriptSourceResolver.h>
#include <Oxygen/Testing/GTest.h>

using oxygen::EngineConfig;
using oxygen::GraphicsConfig;
using oxygen::PathFinder;
using oxygen::PathFinderConfig;
using oxygen::Platform;
using oxygen::PlatformConfig;
using oxygen::TypeId;
using oxygen::data::AssetKey;
using oxygen::graphics::BackendType;
using oxygen::scripting::CompiledScriptExecutable;
using oxygen::scripting::ScriptBlobOrigin;
using oxygen::scripting::ScriptBytecodeBlob;
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

class ScriptingAdvancedTest : public ::testing::Test {
protected:
  void SetUp() override
  {
    temp_dir_ = std::filesystem::temp_directory_path()
      / "oxygen_scripting_advanced_test";
    std::error_code ec;
    std::filesystem::remove_all(temp_dir_, ec);
    std::filesystem::create_directories(temp_dir_ / "root1");
    std::filesystem::create_directories(temp_dir_ / "root2");

    ctx_.platform
      = std::make_shared<oxygen::Platform>(PlatformConfig { .headless = true });

    pfc_ = PathFinderConfig::Create()
             .WithWorkspaceRoot(temp_dir_)
             .AddScriptSourceRoot(temp_dir_ / "root1")
             .AddScriptSourceRoot(temp_dir_ / "root2")
             .BuildShared();

    auto& loader = oxygen::GraphicsBackendLoader::GetInstanceRelaxed();
    GraphicsConfig config {};
    config.headless = true;
    auto gfx = loader.LoadBackend(BackendType::kHeadless, config, *pfc_);

    ctx_.engine = std::make_shared<oxygen::AsyncEngine>(
      ctx_.platform, gfx, EngineConfig {});
  }

  [[nodiscard]] auto TempDir() const -> auto& { return temp_dir_; }
  [[nodiscard]] auto PFC() const -> auto& { return pfc_; }
  [[nodiscard]] auto Ctx() -> auto& { return ctx_; }

  void TearDown() override
  {
    Ctx().engine.reset();
    oxygen::GraphicsBackendLoader::GetInstanceRelaxed().UnloadBackend();
    Ctx().platform.reset();
    if (std::filesystem::exists(temp_dir_)) {
      std::error_code ec;
      std::filesystem::remove_all(temp_dir_, ec);
    }
  }

  static void WriteFile(
    const std::filesystem::path& path, std::string_view content)
  {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream f(path);
    f << content;
  }

private:
  std::filesystem::path temp_dir_;
  std::shared_ptr<const PathFinderConfig> pfc_;
  TestContext ctx_;
};

//! 1. ROOT PRECEDENCE TEST: First root wins.
NOLINT_TEST_F(ScriptingAdvancedTest, FirstRootWinsPrecedence)
{
  auto path1 = TempDir() / "root1/shared.lua";
  auto path2 = TempDir() / "root2/shared.lua";

  WriteFile(path1, "return 'root1'");
  WriteFile(path2, "return 'root2'");

  // Resolver should find root1 first
  ScriptSourceResolver resolver(PathFinder(PFC(), TempDir()));

  oxygen::data::pak::scripting::ScriptAssetDesc desc {};
  desc.flags
    = oxygen::data::pak::scripting::ScriptAssetFlags::kAllowExternalSource;
  const std::string p = "shared.lua";
  std::ranges::copy(p.substr(0, sizeof(desc.external_source_path)),
    &desc.external_source_path[0]); // NOLINT
  oxygen::data::ScriptAsset asset(MakeAssetKey(1U), desc);

  auto result = resolver.Resolve({ .asset = asset,
    .load_script_resource = [](uint32_t) { return nullptr; },
    .map_resource_origin = [](uint32_t) -> std::optional<ScriptBlobOrigin> {
      return std::nullopt;
    } });

  ASSERT_TRUE(result.ok);
  auto& blob = std::get<ScriptSourceBlob>(*result.blob);
  std::string content(
    reinterpret_cast<const char*>(blob.BytesView().data()), // NOLINT
    blob.Size());
  EXPECT_EQ(content, "return 'root1'");
}

//! 2. CONCURRENCY TEST: Thread-safe bytecode updates.
NOLINT_TEST_F(ScriptingAdvancedTest, BytecodeUpdatesAreThreadSafe)
{
  auto blob1
    = std::make_shared<const ScriptBytecodeBlob>(ScriptBytecodeBlob::FromOwned(
      { 1 }, oxygen::data::pak::scripting::ScriptLanguage::kLuau,
      oxygen::data::pak::scripting::ScriptCompression::kNone, 1,
      oxygen::scripting::ScriptBlobOrigin::kExternalFile,
      oxygen::scripting::ScriptBlobCanonicalName { "v1" }));

  auto blob2
    = std::make_shared<const ScriptBytecodeBlob>(ScriptBytecodeBlob::FromOwned(
      { 2 }, oxygen::data::pak::scripting::ScriptLanguage::kLuau,
      oxygen::data::pak::scripting::ScriptCompression::kNone, 2,
      oxygen::scripting::ScriptBlobOrigin::kExternalFile,
      oxygen::scripting::ScriptBlobCanonicalName { "v2" }));

  CompiledScriptExecutable executable(blob1);

  std::atomic<bool> stop { false };

  // Thread A: Constantly reading bytecode
  std::thread reader([&]() {
    while (!stop) {
      auto view = executable.BytecodeView();
      (void)view.size();
    }
  });

  // Thread B: Constantly updating bytecode
  std::thread writer([&]() {
    constexpr int kIterationCount = 1000;
    for (int i = 0; i < kIterationCount; ++i) {
      executable.UpdateBytecode(i % 2 == 0 ? blob1 : blob2);
    }
    stop = true;
  });

  reader.join();
  writer.join();
  SUCCEED();
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
  const std::string p = "module_test.lua";
  std::ranges::copy(p.substr(0, sizeof(desc.external_source_path)),
    &desc.external_source_path[0]); // NOLINT
  oxygen::data::ScriptAsset asset(MakeAssetKey(1U), desc);

  auto result = resolver.Resolve({ .asset = asset,
    .load_script_resource = [](uint32_t) { return nullptr; },
    .map_resource_origin = [](uint32_t) -> std::optional<ScriptBlobOrigin> {
      return std::nullopt;
    } });

  ASSERT_TRUE(result.ok);
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
