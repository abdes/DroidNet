//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Cooker/Import/Internal/AdapterTypes.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/MaterialPipeline.h>
#include <Oxygen/Cooker/Import/Internal/fbx/FbxAdapter.h>
#include <Oxygen/Cooker/Import/Internal/gltf/GltfAdapter.h>
#include <Oxygen/Cooker/Import/Naming.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::import::test {
namespace {
  class CancelNamingStrategy final : public NamingStrategy {
  public:
    explicit CancelNamingStrategy(std::stop_source& cancellation)
      : cancellation_(cancellation)
    {
    }
    ~CancelNamingStrategy() override = default;
    OXYGEN_MAKE_NON_COPYABLE(CancelNamingStrategy)
    OXYGEN_MAKE_NON_MOVABLE(CancelNamingStrategy)

    auto Rename(std::string_view, const NamingContext&) const
      -> std::optional<std::string> override
    {
      cancellation_.request_stop();
      return std::nullopt;
    }

  private:
    std::stop_source& cancellation_;
  };

  class MaterialSink final : public adapters::MaterialWorkItemSink {
  public:
    MaterialSink() = default;
    ~MaterialSink() override = default;
    OXYGEN_MAKE_NON_COPYABLE(MaterialSink)
    OXYGEN_MAKE_NON_MOVABLE(MaterialSink)

    auto Consume(MaterialPipeline::WorkItem item) -> bool override
    {
      items.push_back(std::move(item));
      return true;
    }

    std::vector<MaterialPipeline::WorkItem> items;
  };

  template <typename Adapter>
  auto CheckPreparedMaterials(const std::string_view filename) -> void
  {
    const auto path
      = std::filesystem::path(__FILE__).parent_path() / "Models" / filename;
    const auto source_id = path.string();
    auto prepared_naming = NamingService({
      .strategy = std::make_shared<NoOpNamingStrategy>(),
      .enable_namespacing = true,
      .enforce_uniqueness = true,
    });
    auto input = adapters::AdapterInput {};
    input.source_id_prefix = source_id;
    input.request.source_path = path;
    input.naming_service = observer_ptr { &prepared_naming };
    auto adapter = Adapter {};
    ASSERT_TRUE(adapter.Parse(path, input).success);
    const auto prepared = adapter.PrepareMaterials(input);
    ASSERT_TRUE(prepared.success);
    ASSERT_FALSE(prepared.sources.empty());

    // Analysis and import each own a naming session; neither assigns names
    // twice.
    auto import_naming = NamingService({
      .strategy = std::make_shared<NoOpNamingStrategy>(),
      .enable_namespacing = true,
      .enforce_uniqueness = true,
    });
    input.naming_service = observer_ptr { &import_naming };
    auto sink = MaterialSink {};
    const auto streamed
      = adapter.BuildWorkItems(adapters::MaterialWorkTag {}, sink, input);
    ASSERT_TRUE(streamed.success);
    ASSERT_EQ(sink.items.size(), prepared.sources.size());
    for (size_t i = 0; i < prepared.sources.size(); ++i) {
      const auto& source = prepared.sources.at(i);
      const auto& item = sink.items.at(i);
      EXPECT_EQ(source.source_id, item.source_id);
      EXPECT_EQ(source.source_key, item.source_key);
      EXPECT_EQ(source.material.name, item.material.name);
      EXPECT_EQ(source.material.storage_name, item.material.storage_name);
      EXPECT_EQ(source.material.domain, item.material.domain);
      EXPECT_EQ(source.material.alpha_mode, item.material.alpha_mode);
      EXPECT_EQ(
        source.material.inputs.metalness, item.material.inputs.metalness);
      EXPECT_EQ(
        source.material.inputs.roughness, item.material.inputs.roughness);
      for (const auto& slot : MaterialSource::TextureSlots()) {
        const auto& expected = source.material.textures.*slot.binding;
        const auto& actual = item.material.textures.*slot.binding;
        EXPECT_EQ(expected.assigned, actual.assigned);
        EXPECT_EQ(expected.source_id, actual.source_id);
      }
    }
    EXPECT_EQ(prepared_naming.GetNameCount(ImportNameKind::kMaterial),
      import_naming.GetNameCount(ImportNameKind::kMaterial));
  }

  NOLINT_TEST(ModelMaterialSourceTest, GltfPreparationMatchesProductionRecipes)
  {
    CheckPreparedMaterials<adapters::GltfAdapter>(
      "static_scalar_triangle.gltf");
  }

  NOLINT_TEST(ModelMaterialSourceTest, FbxPreparationMatchesProductionRecipes)
  {
    CheckPreparedMaterials<adapters::FbxAdapter>("static_scalar_triangle.fbx");
  }

  NOLINT_TEST(
    ModelMaterialSourceTest, GltfCancellationStopsFurtherNameAllocation)
  {
    constexpr auto document = std::string_view { R"({
      "asset":{"version":"2.0"},
      "materials":[{"name":"First"},{"name":"Second"}]
    })" };
    auto cancellation = std::stop_source {};
    auto naming = NamingService({
      .strategy = std::make_shared<CancelNamingStrategy>(cancellation),
      .enable_namespacing = true,
      .enforce_uniqueness = true,
    });
    auto input = adapters::AdapterInput {};
    input.source_id_prefix = "cancellation.gltf";
    input.request.source_path = "cancellation.gltf";
    input.naming_service = observer_ptr { &naming };
    input.stop_token = cancellation.get_token();
    auto adapter = adapters::GltfAdapter {};
    ASSERT_TRUE(adapter
        .Parse(
          std::as_bytes(std::span(document.data(), document.size())), input)
        .success);
    const auto prepared = adapter.PrepareMaterials(input);
    EXPECT_FALSE(prepared.success);
    ASSERT_EQ(prepared.diagnostics.size(), 1U);
    EXPECT_EQ(prepared.diagnostics.front().code, "import.canceled");
    EXPECT_EQ(prepared.sources.size(), 1U);
    EXPECT_EQ(naming.GetNameCount(ImportNameKind::kMaterial), 1U);
  }
} // namespace
} // namespace oxygen::content::import::test
