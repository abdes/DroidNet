//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "ScriptingModule_test_fixture.h"

namespace oxygen::scripting::test {

class ContentBindingsTest : public ScriptingModuleTest { };

NOLINT_TEST_F(
  ContentBindingsTest, ExecuteScriptContentBindingsExposeV1AssetsModuleSurface)
{
  auto module = MakeModule();
  ASSERT_TRUE(module.OnAttached(observer_ptr<IAsyncEngine> {}));

  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
local assets = oxygen.assets
if type(assets) ~= "table" then error("missing oxygen.assets") end

local expected = {
  "available", "enabled",
  "has_texture", "get_texture", "has_buffer", "get_buffer",
  "has_material", "get_material", "has_geometry", "get_geometry", "has_script", "get_script",
  "has_input_action", "get_input_action", "has_input_mapping_context", "get_input_mapping_context",
  "load_texture_async", "load_buffer_async", "load_material_async", "load_geometry_async",
  "load_script_async", "load_input_action_async", "load_input_mapping_context_async",
  "release_resource", "release_asset", "trim_cache",
  "mint_synthetic_texture_key", "mint_synthetic_buffer_key",
  "add_pak_file", "add_loose_cooked_root", "clear_mounts",
  "create_procedural_geometry", "create_default_material", "create_debug_material"
}
for i = 1, #expected do
  local name = expected[i]
  if type(assets[name]) ~= "function" then
    error("missing assets function: " .. name)
  end
end
)lua" },
    .chunk_name = ScriptChunkName { "content_bindings_surface" },
  });

  EXPECT_TRUE(result.ok) << result.message;
}

NOLINT_TEST_F(ContentBindingsTest,
  ExecuteScriptContentBindingsNoLoaderDeterministicDefaults)
{
  auto module = MakeModule();
  ASSERT_TRUE(module.OnAttached(observer_ptr<IAsyncEngine> {}));

  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
local assets = oxygen.assets
if assets.available() ~= false then error("available should be false") end
if assets.enabled() ~= false then error("enabled should be false") end

if assets.has_texture(1) ~= false then error("has_texture should be false") end
if assets.get_texture(1) ~= nil then error("get_texture should be nil") end
if assets.has_buffer(1) ~= false then error("has_buffer should be false") end
if assets.get_buffer(1) ~= nil then error("get_buffer should be nil") end

local guid = "01234567-89ab-cdef-0123-456789abcdef"
if assets.has_material(guid) ~= false then error("has_material should be false") end
if assets.get_material(guid) ~= nil then error("get_material should be nil") end
if assets.release_asset(guid) ~= false then error("release_asset should be false") end

if assets.release_resource(42) ~= false then error("release_resource should be false") end
if assets.trim_cache() ~= false then error("trim_cache should be false") end
if assets.mint_synthetic_texture_key() ~= 0 then error("mint_synthetic_texture_key should be 0") end
if assets.mint_synthetic_buffer_key() ~= 0 then error("mint_synthetic_buffer_key should be 0") end

local cb_called = false
local ok = assets.load_texture_async(1, function(v)
  cb_called = true
end)
if ok ~= false then error("load_texture_async should return false without loader") end
if cb_called ~= false then error("callback should not be called without loader") end
)lua" },
    .chunk_name = ScriptChunkName { "content_bindings_no_loader_defaults" },
  });

  EXPECT_TRUE(result.ok) << result.message;
}

NOLINT_TEST_F(ContentBindingsTest,
  ExecuteScriptContentBindingsProceduralCreationAndUserdataSurfaceWorks)
{
  auto module = MakeModule();
  ASSERT_TRUE(module.OnAttached(observer_ptr<IAsyncEngine> {}));

  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
local assets = oxygen.assets
local geom = assets.create_procedural_geometry("sphere", { latitude_segments = 8, longitude_segments = 12 })
if geom == nil then error("expected procedural geometry userdata") end
if geom:is_valid() ~= true then error("geometry is_valid failed") end
if type(geom:guid()) ~= "string" then error("geometry guid type") end
if geom:type_name() ~= "GeometryAsset" then error("geometry type_name") end
if type(geom:to_string()) ~= "string" then error("geometry to_string type") end

local mat = assets.create_default_material()
if mat == nil then error("expected default material userdata") end
if mat:is_valid() ~= true then error("material is_valid failed") end
if type(mat:guid()) ~= "string" then error("material guid type") end
if mat:type_name() ~= "MaterialAsset" then error("material type_name") end
)lua" },
    .chunk_name = ScriptChunkName { "content_bindings_procedural_userdata" },
  });

  EXPECT_TRUE(result.ok) << result.message;
}

NOLINT_TEST_F(
  ContentBindingsTest, ExecuteScriptContentBindingsRejectLegacyAndBadGuidShape)
{
  auto module = MakeModule();
  ASSERT_TRUE(module.OnAttached(observer_ptr<IAsyncEngine> {}));

  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
local assets = oxygen.assets
if oxygen.content ~= nil then
  error("legacy oxygen.content namespace must not exist")
end

local ok_bad_guid, _ = pcall(function()
  assets.has_material("invalid-guid")
end)
if ok_bad_guid then
  error("invalid guid should raise Lua error")
end

local ok_legacy, msg_legacy = pcall(function()
  return assets.find
end)
if ok_legacy and msg_legacy ~= nil then
  error("legacy assets.find must not exist")
end
)lua" },
    .chunk_name = ScriptChunkName { "content_bindings_legacy_bad_guid" },
  });

  EXPECT_TRUE(result.ok) << result.message;
}

} // namespace oxygen::scripting::test
