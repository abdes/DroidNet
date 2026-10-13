//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <functional>
#include <ios>
#include <iterator>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ScriptingModule_test_fixture.h"
#include <gtest/gtest.h>
#include <lua.h>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/test/Utils/TestEventLoop.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scripting/Analysis/DiagnosticCatalog.h>
#include <Oxygen/Scripting/Analysis/ScriptAnalyzer.h>
#include <Oxygen/Scripting/Analysis/ScriptDiagnostic.h>
#include <Oxygen/Scripting/Bindings/Contracts/IScriptBindingPack.h>
#include <Oxygen/Scripting/Bindings/LuauDefinitions.h>
#include <Oxygen/Scripting/Module/ScriptingModule.h>

namespace oxygen::scripting::test {

namespace {

  //! Registry tables that hold engine state, not script-visible types.
  constexpr auto kInternalRegistryNames = std::to_array<std::string_view>({
    "oxygen.error_frames",
    "oxygen.events.runtime",
    "oxygen.script.tables",
    "oxygen.scripting.runtime_context",
  });

  //! What the default packs describe, flattened to dotted paths.
  struct DescribedApi {
    std::set<std::string, std::less<>> functions;
    std::set<std::string, std::less<>> fields;
    std::set<std::string, std::less<>> namespaces;
    std::set<std::string, std::less<>> untyped;
    //! Metatable name -> described method and metamethod names.
    std::map<std::string, std::set<std::string, std::less<>>, std::less<>>
      methods;
  };

  auto Join(const std::string_view path, const std::string_view name)
    -> std::string
  {
    return path.empty() ? std::string(name)
                        : std::string(path) + "." + std::string(name);
  }

  auto Describe() -> DescribedApi
  {
    auto api = DescribedApi {};
    for (const auto& pack : bindings::CreateDefaultBindingPacks()) {
      for (const auto& registration : pack->Namespaces()) {
        if (registration.describe == nullptr) {
          api.untyped.insert(registration.name);
          continue;
        }
        for (const auto& description : registration.describe()) {
          auto path = std::string(description.path);
          // Every prefix of a path is a namespace table.
          for (auto dot = path.find('.'); dot != std::string::npos;
            dot = path.find('.', dot + 1)) {
            api.namespaces.insert(path.substr(0, dot));
          }
          if (!path.empty()) {
            api.namespaces.insert(path);
          }
          for (const auto& function : description.functions) {
            api.functions.insert(Join(path, function.name));
          }
          for (const auto& field : description.fields) {
            api.fields.insert(Join(path, field.name));
          }
          for (const auto& type : description.classes) {
            if (type.metatable.empty()) {
              continue;
            }
            auto& names = api.methods[std::string(type.metatable)];
            for (const auto& method : type.methods) {
              names.insert(method.name);
            }
            for (const auto& method : type.metamethods) {
              names.insert(method.name);
            }
          }
        }
      }
    }
    return api;
  }

  //! Walks the live `oxygen` table and the registry when it is registered,
  //! after every default pack, and records each mismatch with the
  //! descriptions.
  class CoverageProbe final : public bindings::contracts::IScriptBindingPack {
  public:
    CoverageProbe(DescribedApi api, std::vector<std::string>& problems)
      : api_(std::move(api))
      , problems_(&problems)
    {
    }

    [[nodiscard]] auto Name() const noexcept -> std::string_view override
    {
      return "test.coverage-probe";
    }

    [[nodiscard]] auto Register(
      const bindings::contracts::ScriptBindingPackContext& context) const
      -> bool override
    {
      auto* state = context.lua_state;
      lua_getref(state, context.runtime_env_ref);
      lua_getfield(state, -1, "oxygen");
      auto seen = std::set<std::string, std::less<>> {};
      WalkTable(state, lua_gettop(state), "", seen);
      lua_pop(state, 2);
      for (const auto& path : api_.functions) {
        if (!seen.contains(path)) {
          problems_->push_back("described but not registered: oxygen." + path);
        }
      }
      CheckMetatables(state);
      return true;
    }

  private:
    [[nodiscard]] auto IsUntyped(const std::string_view path) const -> bool
    {
      const auto top = path.substr(0, path.find('.'));
      return api_.untyped.contains(top);
    }

    auto WalkTable(lua_State* state, const int table, const std::string& path,
      std::set<std::string, std::less<>>& seen) const -> void
    {
      lua_pushnil(state);
      while (lua_next(state, table) != 0) {
        if (lua_type(state, -2) != LUA_TSTRING) {
          lua_pop(state, 1);
          continue;
        }
        const auto key = std::string(lua_tostring(state, -2));
        const auto member = Join(path, key);
        if (key.starts_with("__") || IsUntyped(member)) {
          lua_pop(state, 1);
          continue;
        }
        if (lua_isfunction(state, -1)) {
          seen.insert(member);
          if (!api_.functions.contains(member)) {
            problems_->push_back(
              std::string("undescribed function: oxygen.").append(member));
          }
        } else if (lua_istable(state, -1) && api_.namespaces.contains(member)) {
          WalkTable(state, lua_gettop(state), member, seen);
        } else if (!api_.fields.contains(member)) {
          problems_->push_back(
            std::string("undescribed member: oxygen.").append(member));
        }
        lua_pop(state, 1);
      }
    }

    //! Method names a metatable resolves: its own fields, or those of its
    //! `__index` table when that is another table.
    static auto LiveMethods(lua_State* state, const int metatable)
      -> std::set<std::string, std::less<>>
    {
      auto names = std::set<std::string, std::less<>> {};
      const auto collect = [state, &names](const int table) -> void {
        lua_pushnil(state);
        while (lua_next(state, table) != 0) {
          if (lua_type(state, -2) == LUA_TSTRING && lua_isfunction(state, -1)) {
            names.insert(lua_tostring(state, -2));
          }
          lua_pop(state, 1);
        }
      };
      collect(metatable);
      lua_getfield(state, metatable, "__index");
      if (lua_istable(state, -1) && lua_rawequal(state, -1, metatable) == 0) {
        collect(lua_gettop(state));
      }
      lua_pop(state, 1);
      return names;
    }

    auto CheckMetatables(lua_State* state) const -> void
    {
      lua_pushnil(state);
      while (lua_next(state, LUA_REGISTRYINDEX) != 0) {
        if (lua_type(state, -2) != LUA_TSTRING || !lua_istable(state, -1)) {
          lua_pop(state, 1);
          continue;
        }
        const auto name = std::string(lua_tostring(state, -2));
        const bool internal = std::ranges::contains(
          kInternalRegistryNames, std::string_view(name));
        if (!name.starts_with("oxygen.") || internal
          || name.starts_with("oxygen.physics")) {
          lua_pop(state, 1);
          continue;
        }
        const auto described = api_.methods.find(name);
        if (described == api_.methods.end()) {
          problems_->push_back("undescribed userdata type: " + name);
          lua_pop(state, 1);
          continue;
        }
        const auto live = LiveMethods(state, lua_gettop(state));
        for (const auto& method : live) {
          const bool infrastructure = method == "__index"
            || method == "__newindex" || method == "__tostring"
            || method == "__eq";
          if (!infrastructure && !described->second.contains(method)) {
            problems_->push_back(std::string("undescribed method: ")
                .append(name)
                .append(" ")
                .append(method));
          }
        }
        for (const auto& method : described->second) {
          if (!live.contains(method)) {
            problems_->push_back(std::string("described but not registered: ")
                .append(name)
                .append(" ")
                .append(method));
          }
        }
        lua_pop(state, 1);
      }
    }

    DescribedApi api_;
    std::vector<std::string>* problems_;
  };

  auto Definitions() -> const std::string&
  {
    static const auto text = bindings::GenerateLuauDefinitions(
      bindings::CreateDefaultBindingPacks());
    return text;
  }

  auto CheckTypes(const std::string_view source_name,
    const std::string_view source) -> ScriptAnalysis
  {
    return ScriptAnalyzer {}.Analyze({
      .source_name = source_name,
      .source = std::as_bytes(std::span(source.data(), source.size())),
      .level = AnalysisLevel::kTypes,
      .type_definitions = Definitions(),
    });
  }

  //! Type errors (S05xx) and errors of any family, rendered for a failure
  //! message.
  auto TypeProblems(const ScriptAnalysis& analysis) -> std::vector<std::string>
  {
    auto problems = std::vector<std::string> {};
    for (const auto& diagnostic : analysis.diagnostics) {
      if (diagnostic.severity == DiagnosticSeverity::kError
        || FamilyOf(diagnostic.code) == DiagnosticFamily::kTypes) {
        problems.push_back(FormatSummary(diagnostic));
      }
    }
    return problems;
  }

  auto ReadText(const std::filesystem::path& path) -> std::string
  {
    auto stream = std::ifstream(path, std::ios::binary);
    return { std::istreambuf_iterator<char>(stream),
      std::istreambuf_iterator<char>() };
  }

  auto RunSceneMutationPhase(
    ScriptingModule& module, observer_ptr<engine::FrameContext> context) -> void
  {
    co::testing::TestEventLoop loop;
    co::Run(loop, module.OnSceneMutation(context));
  }

  //! Uses each typed pack the way scripts do; it both runs and type-checks.
  constexpr std::string_view kApiTour = R"lua(
local function check(condition: boolean, what: string)
  if not condition then
    error("api tour: " .. what)
  end
end

function on_scene_mutation()
  -- math and conventions
  local up = oxygen.conventions.world.up
  local q = oxygen.math.quat_from_euler_xyz(0, oxygen.math.deg_to_rad(90), 0)
  local turned: vector = q * up
  check(oxygen.math.near_equal(turned, oxygen.math.rotate_vec3(q, up)), "rotate")
  local m = oxygen.math.mat4_trs(vector.create(1, 2, 3), q, vector.one)
  check(oxygen.math.is_finite(oxygen.math.mat4_transform_point(m, vector.zero)), "mat4")
  check(oxygen.math.vec4(1, 2, 3, 4).w == 4, "vec4")

  -- time, ids and events
  local dt = oxygen.time.delta_seconds()
  check(dt >= 0, "time")
  local id = oxygen.uuid.new()
  check(oxygen.uuid.is_valid(oxygen.uuid.to_string(id)), "uuid")
  local h = oxygen.hash.combine64(oxygen.hash.hash64("a"), "b")
  check(tostring(h) ~= "", "hash")
  local connection = oxygen.events.on("tour.ping", function(_payload) end)
  check(connection:connected(), "connection")
  connection:disconnect()
  check(oxygen.events.listener_count("tour.ping") == 0, "listeners")
  oxygen.log.info("api tour", dt)

  -- scene graph, components and queries
  local root = oxygen.scene.create_node("TourRoot")
  check(root ~= nil, "create root")
  if root == nil then
    return
  end
  local child = oxygen.scene.create_node("TourChild", root)
  check(child ~= nil and child.parent == root, "create child")
  root:set_local_position(vector.create(0, 1, 0))
  root.local_scale = vector.one
  check(root:translate(vector.create(1, 0, 0), true), "translate")
  check(root:attach_perspective_camera({ fov_y = 1.0, near_plane = 0.1 }), "camera")
  local perspective = root:camera_get_perspective()
  check(perspective ~= nil and perspective.aspect_mode == "auto", "perspective")
  check(root:camera_set_exposure({ iso = 200 }), "exposure")
  check(oxygen.scene.set_active_camera(root), "active camera")
  if child ~= nil then
    check(child:attach_point_light({ range = 5, color_rgb = vector.one }), "light")
    check(child:light_type() == "point", "light type")
    check(child:renderable_set_geometry("cube"), "geometry")
    check(child:renderable_get_effective_lod_count() >= 1, "lods")
  end
  local found = oxygen.scene.find_one("TourRoot/TourChild")
  check(found ~= nil, "find_one")
  local query = oxygen.scene.query("**")
  check(query ~= nil and query:scope(root):count() >= 1, "query")

  -- environment
  local environment = oxygen.scene.ensure_environment()
  check(environment ~= nil, "environment")
  if environment ~= nil then
    local fog = environment:ensure_fog()
    check(fog ~= nil and fog:set_max_opacity(0.5), "fog")
    local sky = environment:ensure_sky_sphere()
    check(sky ~= nil and sky:set({ source = "solid_color", intensity = 2 }), "sky")
    local post = environment:ensure_post_process()
    if post ~= nil then
      local ok, reason = post:set({ tone_mapper = "aces_fitted" })
      check(ok, reason or "post process")
    end
    check(#environment:systems() == 3, "systems")
  end

  -- input and assets need no loaded content
  check(oxygen.input.edges.triggered == "triggered", "edges")
  check(not oxygen.assets.has_material("00000000-0000-0000-0000-000000000000"), "assets")
  tour_done = true
end
)lua";

} // namespace

class BindingDefinitionsTest : public ScriptingModuleTest { };

//! Every function, member and userdata method reachable from `oxygen` is
//! described, and every description matches a registration, so a raw
//! registration cannot slip back in.
NOLINT_TEST_F(BindingDefinitionsTest, DescriptionsCoverTheLiveApi)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));
  auto problems = std::vector<std::string> {};

  ASSERT_TRUE(module.RegisterBindingPack(
    std::make_shared<CoverageProbe>(Describe(), problems)));

  EXPECT_TRUE(problems.empty()) << [&problems] {
    auto text = std::string {};
    for (const auto& problem : problems) {
      text += problem + "\n";
    }
    return text;
  }();
}

//! The generated definitions load into Luau without errors.
NOLINT_TEST_F(BindingDefinitionsTest, GeneratedDefinitionsAreValidLuau)
{
  const auto analysis = CheckTypes("valid.lua", "return nil\n");

  EXPECT_TRUE(analysis.diagnostics.empty())
    << FormatSummary(analysis.diagnostics.front());
  EXPECT_NE(Definitions().find("declare oxygen:"), std::string::npos);
  EXPECT_NE(Definitions().find("physics: any"), std::string::npos);
}

//! A misspelled `oxygen.scene` member is a type error.
NOLINT_TEST_F(BindingDefinitionsTest, MisspelledSceneMemberIsFlagged)
{
  const auto analysis = CheckTypes(
    "typo.lua", "local node = oxygen.scene.fnd_one(\"Player\")\nreturn node\n");

  ASSERT_EQ(analysis.diagnostics.size(), 1U);
  EXPECT_EQ(analysis.diagnostics.front().code, DiagnosticCode::kUnknownMember);
}

//! The maintained example scripts check without type errors.
NOLINT_TEST_F(BindingDefinitionsTest, ExampleScriptsTypeCheck)
{
  const auto root = std::filesystem::path(OXYGEN_EXAMPLE_SCENES_DIR);
  ASSERT_TRUE(std::filesystem::is_directory(root)) << root.string();
  auto checked = 0;
  for (const auto& entry :
    std::filesystem::recursive_directory_iterator(root)) {
    if (entry.path().extension() != ".lua") {
      continue;
    }
    const auto name = entry.path().lexically_relative(root).generic_string();
    const auto analysis = CheckTypes(name, ReadText(entry.path()));
    EXPECT_TRUE(TypeProblems(analysis).empty()) << [&analysis] {
      auto text = std::string {};
      for (const auto& problem : TypeProblems(analysis)) {
        text += problem + "\n";
      }
      return text;
    }();
    ++checked;
  }
  EXPECT_GT(checked, 0);
}

//! A tour of every typed pack runs in the engine and type-checks, so a
//! signature that disagrees with its implementation fails one of the two.
NOLINT_TEST_F(BindingDefinitionsTest, ApiTourRunsAndTypeChecks)
{
  const auto analysis = CheckTypes("api_tour.lua", kApiTour);
  EXPECT_TRUE(TypeProblems(analysis).empty()) << [&analysis] {
    auto text = std::string {};
    for (const auto& problem : TypeProblems(analysis)) {
      text += problem + "\n";
    }
    return text;
  }();

  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));
  const auto loaded = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { kApiTour },
    .chunk_name = ScriptChunkName { "api_tour" },
  });
  ASSERT_TRUE(loaded.ok) << loaded.message;
  auto scene
    = std::make_shared<scene::Scene>("api_tour", kDefaultSceneCapacity);
  engine::FrameContext context;
  context.SetFrameSequenceNumber(frame::SequenceNumber { 1 }, Tag::Get());
  context.SetScene(observer_ptr<scene::Scene> { scene.get() });
  RunSceneMutationPhase(module, observer_ptr { &context });

  EXPECT_FALSE(context.HasErrors());
  const auto done = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { "assert(tour_done == true)" },
    .chunk_name = ScriptChunkName { "api_tour_done" },
  });
  EXPECT_TRUE(done.ok) << done.message;
}

} // namespace oxygen::scripting::test
