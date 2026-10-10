//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/fbx/FbxAdapter.cpp,
//   Import/Internal/gltf/GltfAdapter.cpp,
//   Import/Internal/Pipelines/TexturePipeline.cpp

#include <algorithm>
#include <filesystem>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/AdapterTypes.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/TexturePipeline.h>
#include <Oxygen/Cooker/Import/Internal/fbx/FbxAdapter.h>
#include <Oxygen/Cooker/Import/Internal/gltf/GltfAdapter.h>
#include <Oxygen/Cooker/Test/Support/TestPaths.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::import::test {
namespace {
  class TextureSink final : public adapters::TextureWorkItemSink {
  public:
    TextureSink() = default;
    ~TextureSink() override = default;
    OXYGEN_MAKE_NON_COPYABLE(TextureSink)
    OXYGEN_MAKE_NON_MOVABLE(TextureSink)

    auto Consume(TexturePipeline::WorkItem item) -> bool override
    {
      items.push_back(std::move(item));
      return true;
    }

    std::vector<TexturePipeline::WorkItem> items;
  };

  NOLINT_TEST(
    ModelTextureSourceTest, GltfSharesTextureUsesAndDefersExternalReads)
  {
    constexpr auto document = std::string_view { R"({
      "asset":{"version":"2.0"},
      "images":[{"uri":"missing%20file.png"}],"textures":[{"source":0}],
      "materials":[
        {"pbrMetallicRoughness":{"baseColorTexture":{"index":0}},"normalTexture":{"index":0}},
        {"pbrMetallicRoughness":{"baseColorTexture":{"index":0}}}
      ]
    })" };
    auto input = adapters::AdapterInput {};
    input.source_id_prefix = "logical-model";
    input.request.source_path = "authoring/model.gltf";
    auto& tuning = input.request.options.texture_tuning;
    tuning.enabled = true;
    tuning.max_mip_levels = 3;
    tuning.color_output_format = Format::kBC7UNormSRGB;
    tuning.data_output_format = Format::kBC7UNorm;
    auto adapter = adapters::GltfAdapter {};
    ASSERT_TRUE(adapter
        .Parse(
          std::as_bytes(std::span(document.data(), document.size())), input)
        .success);
    const auto prepared = adapter.PrepareTextures(input);
    ASSERT_TRUE(prepared.success);
    EXPECT_TRUE(prepared.diagnostics.empty());
    ASSERT_EQ(prepared.sources.size(), 2U);
    for (const auto& source : prepared.sources) {
      EXPECT_EQ(source.source_path,
        std::filesystem::path("authoring/missing file.png"));
      EXPECT_FALSE(source.embedded);
      EXPECT_EQ(source.external_texture_id, source.source_id);
      EXPECT_EQ(source.desc.max_mip_levels, 3U);
      EXPECT_EQ(source.desc.output_format,
        source.source_id.ends_with("::base_color") ? Format::kBC7UNormSRGB
                                                   : Format::kBC7UNorm);
    }
    auto diagnostics = std::vector<ImportDiagnostic> {};
    const auto files
      = adapter.CollectExternalTextureSources(input, diagnostics);
    EXPECT_TRUE(diagnostics.empty());
    EXPECT_EQ(files.size(), prepared.sources.size());
    auto sink = TextureSink {};
    const auto streamed
      = adapter.BuildWorkItems(adapters::TextureWorkTag {}, sink, input);
    ASSERT_TRUE(streamed.success);
    EXPECT_TRUE(streamed.diagnostics.empty());
    ASSERT_EQ(sink.items.size(), prepared.sources.size());
    for (const auto& source : prepared.sources) {
      const auto item = std::ranges::find(
        sink.items, source.source_id, &TexturePipeline::WorkItem::source_id);
      ASSERT_NE(item, sink.items.end());
      EXPECT_EQ(item->source_path, source.source_path);
      EXPECT_EQ(item->desc.output_format, source.desc.output_format);
      EXPECT_EQ(item->desc.max_mip_levels, source.desc.max_mip_levels);
    }
  }

  NOLINT_TEST(
    ModelTextureSourceTest, GltfPreparationDoesNotDecodeEmbeddedImages)
  {
    constexpr auto document = std::string_view { R"({
      "asset":{"version":"2.0"},
      "images":[{"uri":"data:image/png;base64,"}],"textures":[{"source":0}],
      "materials":[{"normalTexture":{"index":0}}]
    })" };
    auto input = adapters::AdapterInput {};
    input.source_id_prefix = "embedded.gltf";
    input.request.source_path = "embedded.gltf";
    auto adapter = adapters::GltfAdapter {};
    ASSERT_TRUE(adapter
        .Parse(
          std::as_bytes(std::span(document.data(), document.size())), input)
        .success);
    const auto prepared = adapter.PrepareTextures(input);
    ASSERT_TRUE(prepared.success);
    EXPECT_TRUE(prepared.diagnostics.empty());
    ASSERT_EQ(prepared.sources.size(), 1U);
    EXPECT_TRUE(prepared.sources.front().embedded);
    EXPECT_TRUE(prepared.sources.front().source_path.empty());
    auto diagnostics = std::vector<ImportDiagnostic> {};
    EXPECT_TRUE(
      adapter.CollectExternalTextureSources(input, diagnostics).empty());
    EXPECT_TRUE(diagnostics.empty());
  }

  NOLINT_TEST(ModelTextureSourceTest, FbxPreparationMatchesTextureProduction)
  {
    const auto path
      = oxygen::cooker::test::ModelPath("static_textured_triangle.fbx");
    ASSERT_TRUE(std::filesystem::exists(path));
    const auto source_id = path.string();
    auto input = adapters::AdapterInput {};
    input.source_id_prefix = source_id;
    input.request.source_path = path;
    auto adapter = adapters::FbxAdapter {};
    ASSERT_TRUE(adapter.Parse(path, input).success);
    const auto prepared = adapter.PrepareTextures(input);
    ASSERT_TRUE(prepared.success);
    ASSERT_FALSE(prepared.sources.empty());
    auto sink = TextureSink {};
    const auto streamed
      = adapter.BuildWorkItems(adapters::TextureWorkTag {}, sink, input);
    ASSERT_TRUE(streamed.success);
    ASSERT_EQ(sink.items.size(), prepared.sources.size());
    for (const auto& source : prepared.sources) {
      const auto item = std::ranges::find(
        sink.items, source.source_id, &TexturePipeline::WorkItem::source_id);
      ASSERT_NE(item, sink.items.end());
      EXPECT_EQ(item->texture_id, source.source_id);
      EXPECT_EQ(item->source_key, source.source_key);
      EXPECT_EQ(item->desc.output_format, source.desc.output_format);
      EXPECT_EQ(item->failure_policy, source.failure_policy);
    }
  }
} // namespace
} // namespace oxygen::content::import::test
