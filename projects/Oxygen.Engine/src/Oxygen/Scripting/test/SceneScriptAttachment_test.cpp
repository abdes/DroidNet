//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Content/test/Fakes/StubAssetLoader.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/Asset.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Data/ScriptAsset.h>
#include <Oxygen/Data/test/Fixtures/SceneAssetBuilder.h>
#include <Oxygen/Engine/Scripting/ScriptBytecodeBlob.h>
#include <Oxygen/Engine/Scripting/ScriptSourceBlob.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Scene/Scripting/ScriptingComponent.h>
#include <Oxygen/Scripting/Analysis/ScriptDiagnostic.h>
#include <Oxygen/Scripting/Attachment/SceneScriptAttachment.h>
#include <Oxygen/Scripting/IScriptSourceResolver.h>
#include <Oxygen/Scripting/test/Fakes/FakeScriptCompilationService.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::scripting::test {

namespace {
  using data::AssetKey;
  using data::ScriptAssetParam;
  using data::ScriptNodeParam;
  using data::pak::scripting::ScriptLanguage;
  using data::pak::scripting::ScriptParamRecord;
  using data::pak::scripting::ScriptSlotRecord;
  using CompileState = scene::ScriptingComponent::Slot::CompileState;
  using State = SceneScriptSlotOutcome::State;

  constexpr auto kSceneCapacity = 16U;
  constexpr int32_t kExecutionOrder = 4;

  auto Key(const std::string_view path) -> AssetKey
  {
    return AssetKey::FromVirtualPath(path);
  }

  auto Param(const std::string_view key, const data::ScriptParam& value)
    -> ScriptParamRecord
  {
    return data::MakeScriptParamRecord(key, value)
      .value_or(ScriptParamRecord {});
  }

  //! A script asset declaring `speed` (float), `helper` (a script asset) and
  //! `target` (a node).
  auto MakeScript(const AssetKey& key) -> std::shared_ptr<data::ScriptAsset>
  {
    const auto declarations = std::vector {
      Param("speed", 1.0F),
      Param("helper",
        ScriptAssetParam { .key = {}, .asset_type = data::AssetType::kScript }),
      Param("target", ScriptNodeParam {}),
    };
    return std::make_shared<data::ScriptAsset>(
      key, data::pak::scripting::ScriptAssetDesc {}, declarations);
  }

  //! Serves the script assets and hydrated slots a test registers.
  class TestLoader final : public content::testing::StubAssetLoader {
  public:
    auto AddScript(const AssetKey& key) -> void
    {
      scripts_.insert_or_assign(key, MakeScript(key));
    }

    auto SetSlots(const uint32_t node,
      std::vector<content::IAssetLoader::HydratedScriptSlot> slots) -> void
    {
      slots_.insert_or_assign(node, std::move(slots));
    }

    auto GetScriptAsset(const AssetKey& key, const data::Asset& /*context*/)
      -> std::shared_ptr<data::ScriptAsset> override
    {
      const auto it = scripts_.find(key);
      return it != scripts_.end() ? it->second : nullptr;
    }

    [[nodiscard]] auto GetHydratedScriptSlots(
      const data::SceneAsset& /*scene_asset*/,
      const data::pak::scripting::ScriptingComponentRecord& component) const
      -> std::vector<HydratedScriptSlot> override
    {
      const auto it = slots_.find(component.node_index);
      return it != slots_.end() ? it->second
                                : std::vector<HydratedScriptSlot> {};
    }

  private:
    std::unordered_map<AssetKey, std::shared_ptr<data::ScriptAsset>> scripts_;
    std::unordered_map<uint32_t, std::vector<HydratedScriptSlot>> slots_;
  };

  //! Resolves registered scripts to bytecode or to Luau source.
  class TestResolver final : public IScriptSourceResolver {
  public:
    auto UseBytecode(const AssetKey& key) -> void { bytecode_.push_back(key); }
    auto UseSource(const AssetKey& key) -> void { source_.push_back(key); }

    [[nodiscard]] auto Resolve(const ResolveRequest& request) const
      -> ResolveResult override
    {
      const auto key = request.asset.get().GetAssetKey();
      if (std::ranges::contains(bytecode_, key)) {
        return {
          .ok = true,
          .blob = ScriptBytecodeBlob::FromOwned({ 1, 2, 3 },
            ScriptLanguage::kLuau, {}, 0, ScriptBlobOrigin::kEmbeddedResource,
            ScriptBlobCanonicalName { "bytecode" }),
          .error_message = {},
        };
      }
      if (std::ranges::contains(source_, key)) {
        const std::string text = "return 1";
        return {
          .ok = true,
          .blob = ScriptSourceBlob::FromOwned({ text.begin(), text.end() },
            ScriptLanguage::kLuau, {}, 0, ScriptBlobOrigin::kExternalFile,
            ScriptBlobCanonicalName { "source.luau" }),
          .error_message = {},
        };
      }
      return { .ok = false, .blob = {}, .error_message = "no source" };
    }

  private:
    std::vector<AssetKey> bytecode_;
    std::vector<AssetKey> source_;
  };

  class SceneScriptAttachmentTest : public ::testing::Test {
  protected:
    //! Builds a scene asset with three nodes and a scripting component on
    //! node 0, and creates the matching runtime nodes.
    auto MakeScene() -> std::shared_ptr<data::SceneAsset>
    {
      auto builder = data::testing::SceneAssetBuilder {};
      const auto root = builder.AddNode("Root");
      builder.AddNode("Target", root);
      builder.AddNode("Other", root);
      // The loader hydrates the slots a test sets; the cooked record only
      // has to be valid.
      builder.AddScripting(root,
        {
          ScriptSlotRecord {
            .script_asset_key = Key("/test/Cooked.oscript"),
          },
        });
      for (const auto* name : { "Root", "Target", "Other" }) {
        nodes_.push_back(scene_->CreateNode(name));
      }
      return builder.Build();
    }

    auto Attach(const data::SceneAsset& asset) -> SceneScriptAttachmentResult
    {
      const auto attachment = SceneScriptAttachment(
        loader_, resolver_, observer_ptr { &compiler_ });
      return attachment.Attach(asset, nodes_);
    }

    auto RootSlot() -> const scene::ScriptingComponent::Slot&
    {
      return nodes_.front().GetScripting().Slots().front();
    }

    [[nodiscard]] auto Nodes() -> std::vector<scene::SceneNode>&
    {
      return nodes_;
    }
    [[nodiscard]] auto Loader() -> TestLoader& { return loader_; }
    [[nodiscard]] auto Resolver() -> TestResolver& { return resolver_; }

  private:
    std::shared_ptr<scene::Scene> scene_
      = std::make_shared<scene::Scene>("attach", kSceneCapacity);
    std::vector<scene::SceneNode> nodes_;
    TestLoader loader_;
    TestResolver resolver_;
    FakeScriptCompilationService compiler_;
  };

  auto Slot(const AssetKey& script, std::vector<ScriptParamRecord> params = {})
    -> content::IAssetLoader::HydratedScriptSlot
  {
    return {
      .script_asset_key = script,
      .execution_order = kExecutionOrder,
      .params = std::move(params),
    };
  }

} // namespace

//! Embedded bytecode makes the slot ready at once, with its execution order.
NOLINT_TEST_F(SceneScriptAttachmentTest, BytecodeSlotIsReady)
{
  const auto script = Key("/test/Rotate.oscript");
  Loader().AddScript(script);
  Resolver().UseBytecode(script);
  Loader().SetSlots(0U, { Slot(script) });
  const auto asset = MakeScene();

  const auto result = Attach(*asset);

  ASSERT_EQ(result.slots.size(), 1U);
  EXPECT_EQ(result.slots.front().state, State::kReady);
  EXPECT_TRUE(result.slots.front().diagnostics.empty());
  EXPECT_EQ(RootSlot().State(), CompileState::kReady);
  EXPECT_EQ(RootSlot().ExecutionOrder(), kExecutionOrder);
}

//! Source goes through the compilation service, which marks the slot ready.
NOLINT_TEST_F(SceneScriptAttachmentTest, SourceSlotCompiles)
{
  const auto script = Key("/test/Source.oscript");
  Loader().AddScript(script);
  Resolver().UseSource(script);
  Loader().SetSlots(0U, { Slot(script) });
  const auto asset = MakeScene();

  const auto result = Attach(*asset);

  ASSERT_EQ(result.slots.size(), 1U);
  EXPECT_EQ(result.slots.front().state, State::kCompiling);
  EXPECT_EQ(RootSlot().State(), CompileState::kReady);
}

//! A missing script asset leaves a failed slot; the scene still attaches.
NOLINT_TEST_F(SceneScriptAttachmentTest, MissingScriptFailsItsSlot)
{
  const auto missing = Key("/test/Missing.oscript");
  Loader().SetSlots(0U, { Slot(missing) });
  const auto asset = MakeScene();

  const auto result = Attach(*asset);

  ASSERT_EQ(result.slots.size(), 1U);
  const auto& outcome = result.slots.front();
  EXPECT_EQ(outcome.state, State::kFailed);
  ASSERT_EQ(outcome.diagnostics.size(), 1U);
  EXPECT_EQ(outcome.diagnostics.front().code, DiagnosticCode::kAttachFailed);
  EXPECT_EQ(RootSlot().State(), CompileState::kCompilationFailed);
}

//! Overrides that match a declaration apply; the others are rejected with a
//! diagnostic each and the declared default stays.
NOLINT_TEST_F(SceneScriptAttachmentTest, OverridesAreCheckedAgainstDeclarations)
{
  const auto script = Key("/test/Rotate.oscript");
  const auto helper = Key("/test/Helper.oscript");
  Loader().AddScript(script);
  Resolver().UseBytecode(script);
  constexpr float kSpeed = 2.5F;
  constexpr uint32_t kMissingNode = 9U;
  Loader().SetSlots(0U,
    {
      Slot(script,
        {
          Param("speed", kSpeed),
          Param("helper",
            ScriptAssetParam {
              .key = helper,
              .asset_type = data::AssetType::kScript,
            }),
          Param("unknown", 1.0F),
          Param("target", ScriptNodeParam { .node_index = kMissingNode }),
        }),
    });
  const auto asset = MakeScene();

  const auto result = Attach(*asset);

  ASSERT_EQ(result.slots.size(), 1U);
  const auto& outcome = result.slots.front();
  EXPECT_EQ(outcome.state, State::kReady);
  ASSERT_EQ(outcome.diagnostics.size(), 2U);
  for (const auto& diagnostic : outcome.diagnostics) {
    EXPECT_EQ(diagnostic.code, DiagnosticCode::kOverrideRejected);
  }
  auto scripting = Nodes().front().GetScripting();
  const auto& slot = RootSlot();
  EXPECT_EQ(std::get<float>(scripting.GetParameter(slot, "speed")), kSpeed);
  EXPECT_EQ(
    std::get<ScriptAssetParam>(scripting.GetParameter(slot, "helper")).key,
    helper);
  EXPECT_EQ(std::get<ScriptNodeParam>(scripting.GetParameter(slot, "target")),
    ScriptNodeParam {});
  EXPECT_FALSE(scripting.TryGetParameter(slot, "unknown").has_value());
}

//! A value of another type than the declaration is rejected.
NOLINT_TEST_F(SceneScriptAttachmentTest, OverrideOfAnotherTypeIsRejected)
{
  const auto script = Key("/test/Rotate.oscript");
  Loader().AddScript(script);
  Resolver().UseBytecode(script);
  Loader().SetSlots(0U, { Slot(script, { Param("speed", int32_t { 3 }) }) });
  const auto asset = MakeScene();

  const auto result = Attach(*asset);

  ASSERT_EQ(result.slots.front().diagnostics.size(), 1U);
  EXPECT_EQ(result.slots.front().diagnostics.front().code,
    DiagnosticCode::kOverrideRejected);
  EXPECT_EQ(std::get<float>(
              Nodes().front().GetScripting().GetParameter(RootSlot(), "speed")),
    1.0F);
}

//! A node parameter binds to the node created for its index.
NOLINT_TEST_F(SceneScriptAttachmentTest, NodeParameterBindsToItsNode)
{
  const auto script = Key("/test/Rotate.oscript");
  Loader().AddScript(script);
  Resolver().UseBytecode(script);
  Loader().SetSlots(
    0U, { Slot(script, { Param("target", ScriptNodeParam { 1U }) }) });
  const auto asset = MakeScene();

  const auto result = Attach(*asset);

  EXPECT_TRUE(result.slots.front().diagnostics.empty());
  EXPECT_EQ(RootSlot().TryGetBoundNode("target"), Nodes().at(1).GetHandle());
}

} // namespace oxygen::scripting::test
