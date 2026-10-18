//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "ScriptingModule_test_fixture.h"

#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/Scripting/ScriptExecutable.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/ScriptAsset.h>
#include <Oxygen/Engine/Scripting/ScriptSourceBlob.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/test/Utils/TestEventLoop.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scripting/Compilers/LuauScriptCompiler.h>
#include <Oxygen/Scripting/Execution/CompiledScriptExecutable.h>
#include <Oxygen/Scripting/Module/ScriptingModule.h>

namespace oxygen::scripting::test {

inline auto ToBytes(const std::string_view text) -> std::vector<uint8_t>
{
  std::vector<uint8_t> bytes;
  bytes.reserve(text.size());
  for (const auto ch : text) {
    bytes.push_back(static_cast<uint8_t>(ch));
  }
  return bytes;
}

//! A script asset with the given parameter declarations.
inline auto MakeScriptAsset(const data::ScriptParameters& declarations = {})
  -> std::shared_ptr<const data::ScriptAsset>
{
  return std::make_shared<data::ScriptAsset>(
    data::AssetKey {}, data::pak::scripting::ScriptAssetDesc {}, declarations);
}

//! A scene whose node "script-node" has one slot running `executable`.
inline auto MakeSceneWithExecutableSlot(const char* scene_name,
  std::shared_ptr<const ScriptExecutable> executable,
  const size_t capacity = ScriptingModuleTest::kDefaultSceneCapacity)
  -> std::shared_ptr<scene::Scene>
{
  auto scene = std::make_shared<scene::Scene>(scene_name, capacity);
  auto node = scene->CreateNode("script-node");
  if (!node.IsValid()) {
    return {};
  }
  if (!node.AttachScripting()) {
    return {};
  }
  auto scripting = node.GetScripting();
  if (!scripting.AddSlot(MakeScriptAsset())) {
    return {};
  }
  const auto slots = scripting.Slots();
  if (slots.empty()) {
    return {};
  }
  if (!scripting.MarkSlotReady(slots.front(), std::move(executable))) {
    return {};
  }

  return scene;
}

inline auto CompileSlotBytecode(const std::string_view source,
  const uint64_t content_hash) -> std::shared_ptr<const ScriptBytecodeBlob>
{
  LuauScriptCompiler compiler;
  auto compile_result
    = compiler.Compile(ScriptSourceBlob::FromOwned(ToBytes(source),
                         data::pak::scripting::ScriptLanguage::kLuau,
                         data::pak::scripting::ScriptCompression::kNone,
                         content_hash, ScriptBlobOrigin::kEmbeddedResource,
                         ScriptBlobCanonicalName { "scene_slot_local_state" }),
      core::meta::scripting::ScriptCompileMode::kDebug);
  if (!compile_result.success || !compile_result.HasBytecode()) {
    return {};
  }
  if (content_hash == 0) {
    // Preserve the compiler's unstamped output for runtime reload coverage.
    return compile_result.bytecode;
  }
  const auto bytes = compile_result.bytecode->BytesView();
  return std::make_shared<const ScriptBytecodeBlob>(
    ScriptBytecodeBlob::FromOwned(
      std::vector<uint8_t>(bytes.begin(), bytes.end()),
      compile_result.bytecode->Language(),
      compile_result.bytecode->Compression(), content_hash,
      ScriptBlobOrigin::kEmbeddedResource,
      ScriptBlobCanonicalName { "scene_slot_local_state" }));
}

inline auto CompileSlotExecutable(const std::string_view source,
  const uint64_t content_hash) -> std::shared_ptr<const ScriptExecutable>
{
  auto bytecode = CompileSlotBytecode(source, content_hash);
  if (!bytecode) {
    return {};
  }
  return std::make_shared<const CompiledScriptExecutable>(std::move(bytecode));
}

//! Runs one frame's Gameplay and Scene Mutation phases.
inline auto RunSlotHooks(ScriptingModule& module, engine::FrameContext& frame)
  -> void
{
  co::testing::TestEventLoop loop;
  co::Run(loop, module.OnGameplay(observer_ptr { &frame }));
  co::Run(loop, module.OnSceneMutation(observer_ptr { &frame }));
}

} // namespace oxygen::scripting::test
