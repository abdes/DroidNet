//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <ios>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Config/PathFinder.h>
#include <Oxygen/Config/PathFinderConfig.h>
#include <Oxygen/Core/Scripting/ScriptExecutable.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Data/ScriptAsset.h>
#include <Oxygen/Engine/Scripting/ScriptBytecodeBlob.h>
#include <Oxygen/Engine/Scripting/ScriptSourceBlob.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Scene/Scripting/ScriptingComponent.h>
#include <Oxygen/Scripting/Analysis/ScriptDiagnostic.h>
#include <Oxygen/Scripting/Execution/CompiledScriptExecutable.h>
#include <Oxygen/Scripting/Module/ScriptSourceReload.h>
#include <Oxygen/Scripting/test/Fakes/FakeScriptCompilationService.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::scripting::test {

namespace {
  using data::pak::scripting::ScriptAssetDesc;
  using data::pak::scripting::ScriptAssetFlags;

  constexpr auto kSceneCapacity = 8U;
  constexpr std::string_view kScriptFile = "spin.lua";

  constexpr std::string_view kNewSource = R"lua(
local script = oxygen.script {
  params = {
    speed = oxygen.param.float { default = 2.0 },
    extra = oxygen.param.float { default = 0.5 },
  },
}
return script
)lua";

  auto MakeScript() -> std::shared_ptr<const data::ScriptAsset>
  {
    auto desc = ScriptAssetDesc {};
    desc.flags = ScriptAssetFlags::kAllowExternalSource;
    return std::make_shared<data::ScriptAsset>(
      data::AssetKey::FromVirtualPath("/test/spin.oscript"), desc,
      data::ScriptParameters { { "speed", 1.0F } },
      std::string { kScriptFile });
  }

  auto MakeExecutable() -> std::shared_ptr<const ScriptExecutable>
  {
    return std::make_shared<const CompiledScriptExecutable>(
      std::make_shared<const ScriptBytecodeBlob>(ScriptBytecodeBlob::FromOwned(
        { 1 }, data::pak::scripting::ScriptLanguage::kLuau,
        data::pak::scripting::ScriptCompression::kNone, 1,
        ScriptBlobOrigin::kEmbeddedResource,
        ScriptBlobCanonicalName { "original" })));
  }

  class ScriptSourceReloadTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
      root_ = std::filesystem::temp_directory_path()
        / "oxygen_script_source_reload_test";
      std::error_code error;
      std::filesystem::remove_all(root_, error);
      std::filesystem::create_directories(root_);

      auto node = scene_->CreateNode("Spinner");
      ASSERT_TRUE(node.AttachScripting());
      auto scripting = node.GetScripting();
      ASSERT_TRUE(scripting.AddSlot(MakeScript()));
      ASSERT_TRUE(
        scripting.MarkSlotReady(scripting.Slots().front(), original_));
      node_ = std::move(node);
    }

    void TearDown() override
    {
      std::error_code error;
      std::filesystem::remove_all(root_, error);
    }

    [[nodiscard]] auto MakeReload() -> ScriptSourceReload
    {
      return { PathFinder(PathFinderConfig::Create()
                            .WithWorkspaceRoot(root_)
                            .AddScriptSourceRoot(root_)
                            .BuildShared(),
                 root_),
        observer_ptr<IScriptCompilationService> { &compiler_ },
        [this](const ScriptDiagnostic& diagnostic) -> void {
          diagnostics_.push_back(diagnostic);
        } };
    }

    [[nodiscard]] auto WriteSource(const std::string_view text) const
      -> std::filesystem::path
    {
      const auto path = root_ / kScriptFile;
      auto file = std::ofstream(path, std::ios::binary);
      file << text;
      return path;
    }

    [[nodiscard]] auto Node() -> scene::SceneNode& { return node_; }

    [[nodiscard]] auto Slot() -> const scene::ScriptingComponent::Slot&
    {
      return node_.GetScripting().Slots().front();
    }

    [[nodiscard]] auto Scene() const -> const std::shared_ptr<scene::Scene>&
    {
      return scene_;
    }
    [[nodiscard]] auto Original() const
      -> const std::shared_ptr<const ScriptExecutable>&
    {
      return original_;
    }
    [[nodiscard]] auto Diagnostics() const
      -> const std::vector<ScriptDiagnostic>&
    {
      return diagnostics_;
    }

  private:
    std::filesystem::path root_;
    std::shared_ptr<scene::Scene> scene_
      = std::make_shared<scene::Scene>("reload", kSceneCapacity);
    scene::SceneNode node_;
    std::shared_ptr<const ScriptExecutable> original_ = MakeExecutable();
    FakeScriptCompilationService compiler_;
    std::vector<ScriptDiagnostic> diagnostics_;
  };

} // namespace

//! A changed source recompiles into its slots with its new declarations.
NOLINT_TEST_F(ScriptSourceReloadTest, ChangedSourceReplacesExecutable)
{
  auto reload = MakeReload();
  const auto path = WriteSource(kNewSource);

  EXPECT_EQ(reload.OnSourceChanged(Scene(), path), 1U);

  EXPECT_TRUE(Diagnostics().empty());
  EXPECT_NE(Slot().Executable(), Original());
  EXPECT_EQ(
    Slot().State(), scene::ScriptingComponent::Slot::CompileState::kReady);
  ASSERT_NE(Slot().Asset(), nullptr);
  EXPECT_TRUE(Slot().Asset()->HasParameter("extra"));
}

//! A source with errors reports them and the slot keeps its executable.
NOLINT_TEST_F(ScriptSourceReloadTest, BrokenSourceKeepsLastGoodExecutable)
{
  auto reload = MakeReload();
  const auto path = WriteSource("local script = oxygen.script {\n");

  EXPECT_EQ(reload.OnSourceChanged(Scene(), path), 0U);

  EXPECT_FALSE(Diagnostics().empty());
  EXPECT_EQ(Slot().Executable(), Original());
  ASSERT_NE(Slot().Asset(), nullptr);
  EXPECT_FALSE(Slot().Asset()->HasParameter("extra"));
}

//! A file no script uses changes nothing; ReloadAll recompiles every script.
NOLINT_TEST_F(ScriptSourceReloadTest, OnlyScriptsUsingTheFileReload)
{
  auto reload = MakeReload();
  (void)WriteSource(kNewSource);

  EXPECT_EQ(reload.OnSourceChanged(Scene(), "elsewhere/other.lua"), 0U);
  EXPECT_EQ(Slot().Executable(), Original());

  EXPECT_EQ(reload.ReloadAll(Scene()), 1U);
  EXPECT_NE(Slot().Executable(), Original());
}

//! An override the new declarations no longer accept is removed with a
//! diagnostic; one they still accept is kept.
NOLINT_TEST_F(ScriptSourceReloadTest, OverridesAreCheckedAgainstNewDeclarations)
{
  auto scripting = Node().GetScripting();
  ASSERT_TRUE(scripting.SetParameter(
    scripting.Slots().front(), "speed", data::ScriptParam { 3.0F }));
  auto reload = MakeReload();
  const auto path = WriteSource(R"lua(
local script = oxygen.script {
  params = {
    speed = oxygen.param.string { default = "fast" },
  },
}
return script
)lua");

  EXPECT_EQ(reload.OnSourceChanged(Scene(), path), 1U);

  ASSERT_EQ(Diagnostics().size(), 1U);
  EXPECT_EQ(Diagnostics().front().code, DiagnosticCode::kOverrideRejected);
  EXPECT_FALSE(Slot().HasOverrideParameter("speed"));
  EXPECT_EQ(
    std::get<std::string>(scripting.GetParameter(Slot(), "speed")), "fast");
}

//! An override that still matches survives the reload.
NOLINT_TEST_F(ScriptSourceReloadTest, MatchingOverrideIsKept)
{
  auto scripting = Node().GetScripting();
  ASSERT_TRUE(scripting.SetParameter(
    scripting.Slots().front(), "speed", data::ScriptParam { 3.0F }));
  auto reload = MakeReload();

  EXPECT_EQ(reload.OnSourceChanged(Scene(), WriteSource(kNewSource)), 1U);

  EXPECT_TRUE(Diagnostics().empty());
  EXPECT_EQ(std::get<float>(scripting.GetParameter(Slot(), "speed")), 3.0F);
}

} // namespace oxygen::scripting::test
