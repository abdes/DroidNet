//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/ModelTextureSource.h,
//   Import/Internal/fbx/FbxAdapter.cpp,
//   Import/Internal/gltf/GltfAdapter.cpp,
//   Import/Internal/Pipelines/TexturePipeline.cpp

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Base/Filesystem.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/AdapterTypes.h>
#include <Oxygen/Cooker/Import/Internal/ModelTextureSource.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/TexturePipeline.h>
#include <Oxygen/Cooker/Import/Internal/fbx/FbxAdapter.h>
#include <Oxygen/Cooker/Import/Internal/gltf/GltfAdapter.h>
#include <Oxygen/Cooker/Import/TextureImportTypes.h>
#include <Oxygen/Cooker/test/Support/Diagnostics.h>
#include <Oxygen/Cooker/test/Support/FileIo.h>
#include <Oxygen/Cooker/test/Support/TempDir.h>
#include <Oxygen/Cooker/test/Support/TestPaths.h>
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

  NOLINT_TEST(ModelTextureSourceTest, GltfTexturesAreKeyedByImageAndUsage)
  {
    // Two textures named "diffuse" with different images stay two textures;
    // two textures of one image with one usage are one.
    constexpr auto document = std::string_view { R"({
      "asset":{"version":"2.0"},
      "images":[{"uri":"a/diffuse.png"},{"uri":"b/diffuse.png"}],
      "textures":[
        {"name":"diffuse","source":0},
        {"name":"diffuse","source":1},
        {"name":"other","source":0}
      ],
      "materials":[
        {"pbrMetallicRoughness":{"baseColorTexture":{"index":0}}},
        {"pbrMetallicRoughness":{"baseColorTexture":{"index":1}}},
        {"pbrMetallicRoughness":{"baseColorTexture":{"index":2}}}
      ]
    })" };
    auto input = adapters::AdapterInput {};
    input.source_id_prefix = "model";
    input.request.source_path = "authoring/model.gltf";
    auto adapter = adapters::GltfAdapter {};
    ASSERT_TRUE(adapter
        .Parse(
          std::as_bytes(std::span(document.data(), document.size())), input)
        .success);
    const auto prepared = adapter.PrepareTextures(input);
    ASSERT_TRUE(prepared.success);
    ASSERT_EQ(prepared.sources.size(), 2U);
    EXPECT_NE(
      prepared.sources.at(0).source_path, prepared.sources.at(1).source_path);
  }

  //! Every texture a material binds is produced, the KHR_materials extension
  //! slots included; one bound but not produced would render the error
  //! texture.
  NOLINT_TEST(ModelTextureSourceTest, GltfProducesEveryTextureAMaterialBinds)
  {
    constexpr auto document = std::string_view { R"({
      "asset":{"version":"2.0"},
      "extensionsUsed":["KHR_materials_specular","KHR_materials_sheen",
        "KHR_materials_clearcoat","KHR_materials_transmission",
        "KHR_materials_volume"],
      "images":[{"uri":"a.png"}],"textures":[{"source":0}],
      "materials":[{
        "pbrMetallicRoughness":{"baseColorTexture":{"index":0},
          "metallicRoughnessTexture":{"index":0}},
        "normalTexture":{"index":0},"occlusionTexture":{"index":0},
        "emissiveTexture":{"index":0},
        "extensions":{
          "KHR_materials_specular":{"specularTexture":{"index":0}},
          "KHR_materials_sheen":{"sheenColorTexture":{"index":0}},
          "KHR_materials_clearcoat":{"clearcoatTexture":{"index":0},
            "clearcoatNormalTexture":{"index":0}},
          "KHR_materials_transmission":{"transmissionTexture":{"index":0}},
          "KHR_materials_volume":{"thicknessTexture":{"index":0}}
        }
      }]
    })" };
    auto input = adapters::AdapterInput {};
    input.source_id_prefix = "model";
    input.request.source_path = "authoring/model.gltf";
    auto adapter = adapters::GltfAdapter {};
    ASSERT_TRUE(adapter
        .Parse(
          std::as_bytes(std::span(document.data(), document.size())), input)
        .success);

    const auto prepared = adapter.PrepareTextures(input);

    ASSERT_TRUE(prepared.success);
    auto usages = std::vector<std::string> {};
    std::ranges::transform(prepared.sources, std::back_inserter(usages),
      [](const adapters::ModelTextureSource& source) -> std::string {
        return source.usage;
      });
    EXPECT_THAT(usages,
      ::testing::UnorderedElementsAre("base_color", "metallic_roughness",
        "normal", "occlusion", "emissive", "specular", "sheen_color",
        "clearcoat", "clearcoat_normal", "transmission", "thickness"));
  }

  //! A texture reads its EXT_texture_webp image, then its KHR_texture_basisu
  //! image, before its core image, which is only their fallback.
  NOLINT_TEST(ModelTextureSourceTest, GltfTexturesReadTheirExtensionImage)
  {
    constexpr auto document = std::string_view { R"({
      "asset":{"version":"2.0"},
      "extensionsUsed":["EXT_texture_webp","KHR_texture_basisu"],
      "images":[{"uri":"fallback.png"},{"uri":"color.webp"},{"uri":"normal.ktx2"}],
      "textures":[
        {"source":0,"extensions":{"EXT_texture_webp":{"source":1}}},
        {"extensions":{"KHR_texture_basisu":{"source":2}}},
        {"source":0,"extensions":{
          "KHR_texture_basisu":{"source":2},"EXT_texture_webp":{"source":1}}}
      ],
      "materials":[
        {"pbrMetallicRoughness":{"baseColorTexture":{"index":0}},
         "normalTexture":{"index":1},"emissiveTexture":{"index":2}}
      ]
    })" };
    auto input = adapters::AdapterInput {};
    input.source_id_prefix = "model";
    input.request.source_path = "authoring/model.gltf";
    auto adapter = adapters::GltfAdapter {};
    ASSERT_TRUE(adapter
        .Parse(
          std::as_bytes(std::span(document.data(), document.size())), input)
        .success);

    const auto prepared = adapter.PrepareTextures(input);

    ASSERT_TRUE(prepared.success);
    const auto path_of = [&](const std::string_view usage) -> std::string {
      const auto source = std::ranges::find_if(prepared.sources,
        [usage](const adapters::ModelTextureSource& item) -> bool {
          return item.source_id.ends_with(usage);
        });
      return source != prepared.sources.end()
        ? source->source_path.generic_string()
        : std::string {};
    };
    EXPECT_EQ(path_of("::base_color"), "authoring/color.webp");
    EXPECT_EQ(path_of("::normal"), "authoring/normal.ktx2");
    EXPECT_EQ(path_of("::emissive"), "authoring/color.webp");
  }

  //! A texture is named once per image, from the image file's stem when the
  //! texture and image have no name, then takes its usage: two images with
  //! one stem probe `_1`, and one image used twice keeps one name.
  NOLINT_TEST(ModelTextureSourceTest, GltfTexturesAreNamedPerImageWithUsage)
  {
    constexpr auto document = std::string_view { R"({
      "asset":{"version":"2.0"},
      "images":[
        {"uri":"a/diffuse.png"},
        {"uri":"b/diffuse.png"},
        {"uri":"c/Paint.png"},
        {"uri":"data:image/png;base64,AAAA"}
      ],
      "textures":[{"source":0},{"source":1},{"source":2},{"source":3}],
      "materials":[
        {"pbrMetallicRoughness":{"baseColorTexture":{"index":0}}},
        {"pbrMetallicRoughness":{"baseColorTexture":{"index":1}}},
        {"pbrMetallicRoughness":{"baseColorTexture":{"index":2}},
         "normalTexture":{"index":2},
         "emissiveTexture":{"index":3}}
      ]
    })" };
    auto input = adapters::AdapterInput {};
    input.source_id_prefix = "model";
    input.request.source_path = "authoring/model.gltf";
    auto adapter = adapters::GltfAdapter {};
    ASSERT_TRUE(adapter
        .Parse(
          std::as_bytes(std::span(document.data(), document.size())), input)
        .success);
    const auto prepared = adapter.PrepareTextures(input);
    ASSERT_TRUE(prepared.success);
    auto names = std::vector<std::string> {};
    std::ranges::transform(prepared.sources, std::back_inserter(names),
      &adapters::ModelTextureSource::name);
    EXPECT_THAT(names,
      ::testing::ElementsAre("diffuse.base_color", "diffuse_1.base_color",
        "Paint.base_color", "Paint.normal", "texture_3.emissive"));
  }

  NOLINT_TEST(ModelTextureSourceTest, GltfTexturesAreProducedInSourceKeyOrder)
  {
    constexpr auto document = std::string_view { R"({
      "asset":{"version":"2.0"},
      "images":[{"uri":"c.png"},{"uri":"a.png"},{"uri":"b.png"}],
      "textures":[{"source":2},{"source":0},{"source":1}],
      "materials":[
        {"normalTexture":{"index":0},
         "pbrMetallicRoughness":{"baseColorTexture":{"index":1}}},
        {"emissiveTexture":{"index":2},"occlusionTexture":{"index":0}}
      ]
    })" };
    auto input = adapters::AdapterInput {};
    input.source_id_prefix = "model";
    input.request.source_path = "authoring/model.gltf";
    auto adapter = adapters::GltfAdapter {};
    ASSERT_TRUE(adapter
        .Parse(
          std::as_bytes(std::span(document.data(), document.size())), input)
        .success);
    const auto prepared = adapter.PrepareTextures(input);
    ASSERT_TRUE(prepared.success);
    ASSERT_EQ(prepared.sources.size(), 4U);
    EXPECT_TRUE(std::ranges::is_sorted(
      prepared.sources, {}, &adapters::ModelTextureSource::source_id));

    auto sink = TextureSink {};
    ASSERT_TRUE(
      adapter.BuildWorkItems(adapters::TextureWorkTag {}, sink, input).success);
    ASSERT_EQ(sink.items.size(), prepared.sources.size());
    for (size_t i = 0; i < sink.items.size(); ++i) {
      EXPECT_EQ(sink.items.at(i).source_id, prepared.sources.at(i).source_id);
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

  //! Prepares a model of two materials, each with an embedded base color
  //! image on its own texture node and video, as Maya writes them; the first
  //! names `static_textured_checker.png`, the second `second_file`.
  [[nodiscard]] auto PrepareFbxWithTwoEmbeddedTextures(
    const std::string_view second_file) -> adapters::ModelTexturePreparation
  {
    constexpr auto kContent = std::string_view {
      "        Content: , "
      "\"iVBORw0KGgoAAAANSUhEUgAAAAgAAAAECAYAAACzzX7wAAAAG0lEQV"
      "R4nGP4oCHyX2SLzX9cNAM+SRDNQLEJACwzQuGP5hyxAAAAAElFTkSuQmCC\"\n"
    };
    const auto dir = oxygen::cooker::test::ScopedTempDir {};
    auto text = oxygen::cooker::test::ReadText(
      oxygen::cooker::test::ModelPath("static_textured_triangle.fbx"));
    const auto insert_after
      = [&text](const std::string_view after, const std::string& insert) {
          const auto at = text.find(after);
          if (at == std::string::npos) {
            ADD_FAILURE() << "fixture changed: " << after;
            return;
          }
          text.insert(at + after.size(), insert);
        };
    const auto file = std::string(second_file);
    insert_after("        Type: \"Clip\"\n", std::string(kContent));
    insert_after("        Cropping: 0,0,0,0\n    }\n",
      "    Material: 1006, \"Material::Second\", \"\" {\n"
      "        Version: 102\n"
      "        ShadingModel: \"phong\"\n"
      "    }\n"
      "    Video: 1008, \"Video::Second\", \"Clip\" {\n"
      "        Type: \"Clip\"\n"
      "        Filename: \""
        + file + "\"\n        RelativeFilename: \"" + file + "\"\n"
        + std::string(kContent)
        + "    }\n"
          "    Texture: 1007, \"Texture::Second\", \"TextureVideoClip\" {\n"
          "        Type: \"TextureVideoClip\"\n"
          "        Version: 202\n"
          "        TextureName: \"Texture::Second\"\n"
          "        Media: \"Video::Second\"\n"
          "        FileName: \""
        + file + "\"\n        RelativeFilename: \"" + file
        + "\"\n"
          "    }\n");
    insert_after("    C: \"OO\",1004,1005\n",
      "    C: \"OO\",1008,1007\n"
      "    C: \"OP\",1007,1006,\"DiffuseColor\"\n"
      "    C: \"OO\",1006,1002\n");
    const auto path = dir.Path() / "embedded.fbx";
    oxygen::cooker::test::WriteText(path, text);
    const auto source_id = path.string();
    auto input = adapters::AdapterInput {};
    input.source_id_prefix = source_id;
    input.request.source_path = path;
    auto adapter = adapters::FbxAdapter {};
    if (!adapter.Parse(path, input).success) {
      ADD_FAILURE() << "fixture failed to parse";
      return {};
    }
    return adapter.PrepareTextures(input);
  }

  //! An embedded image is one texture however many texture nodes and videos
  //! name it, as Maya writes one of each per material; different embedded
  //! files stay different textures.
  NOLINT_TEST(ModelTextureSourceTest, FbxEmbeddedTexturesAreOnePerFile)
  {
    const auto shared
      = PrepareFbxWithTwoEmbeddedTextures("static_textured_checker.png");
    const auto distinct = PrepareFbxWithTwoEmbeddedTextures("other.png");

    ASSERT_TRUE(shared.success)
      << oxygen::cooker::test::DiagnosticSummary(shared.diagnostics);
    ASSERT_TRUE(distinct.success)
      << oxygen::cooker::test::DiagnosticSummary(distinct.diagnostics);
    ASSERT_EQ(shared.sources.size(), 1U);
    EXPECT_TRUE(shared.sources.front().embedded);
    EXPECT_EQ(shared.sources.front().usage, "base_color");
    ASSERT_EQ(distinct.sources.size(), 2U);
    EXPECT_TRUE(std::ranges::all_of(
      distinct.sources, [](const adapters::ModelTextureSource& source) -> bool {
        return source.embedded;
      }));
  }

  //! Whether a prepared model flips the green channel of its normal map.
  [[nodiscard]] auto NormalMapFlip(
    const adapters::ModelTexturePreparation& prepared) -> std::optional<bool>
  {
    const auto normal
      = std::ranges::find(prepared.sources, TextureIntent::kNormalTS,
        [](const adapters::ModelTextureSource& source) -> TextureIntent {
          return source.desc.intent;
        });
    if (normal == prepared.sources.end()) {
      return std::nullopt;
    }
    return normal->desc.flip_normal_green;
  }

  //! glTF declares its normal maps green-up; the engine's are green-down
  //! (`space::texture`), so the cook flips them, and `flip_normal_green`
  //! reverses that for a map authored the other way. Color maps never flip.
  NOLINT_TEST(ModelTextureSourceTest, GltfNormalMapsCookGreenDown)
  {
    constexpr auto document = std::string_view { R"({
      "asset":{"version":"2.0"},
      "images":[{"uri":"color.png"},{"uri":"normal.png"}],
      "textures":[{"source":0},{"source":1}],
      "materials":[{"pbrMetallicRoughness":{"baseColorTexture":{"index":0}},
        "normalTexture":{"index":1}}]
    })" };
    for (const bool authored_green_down : { false, true }) {
      auto input = adapters::AdapterInput {};
      input.source_id_prefix = "model";
      input.request.source_path = "authoring/model.gltf";
      input.request.options.texture_tuning.flip_normal_green
        = authored_green_down;
      auto adapter = adapters::GltfAdapter {};
      ASSERT_TRUE(adapter
          .Parse(
            std::as_bytes(std::span(document.data(), document.size())), input)
          .success);

      const auto prepared = adapter.PrepareTextures(input);

      ASSERT_TRUE(prepared.success);
      EXPECT_EQ(NormalMapFlip(prepared), !authored_green_down);
      EXPECT_TRUE(std::ranges::none_of(prepared.sources,
        [](const adapters::ModelTextureSource& source) -> bool {
          return source.desc.intent != TextureIntent::kNormalTS
            && source.desc.flip_normal_green;
        }));
    }
  }

  //! Prepares the textures of the normal-mapped FBX fixture, authored by
  //! `application` (none: the fixture records no application).
  [[nodiscard]] auto PrepareFbxAuthoredBy(const std::string_view application)
    -> adapters::ModelTexturePreparation
  {
    const auto dir = oxygen::cooker::test::ScopedTempDir {};
    const auto fixture
      = oxygen::cooker::test::ModelPath("static_normal_triangle.fbx");
    auto text = oxygen::cooker::test::ReadText(fixture);
    if (!application.empty()) {
      constexpr auto kCreator
        = std::string_view { "    Creator: \"Oxygen.Engine Test Fixture\"\n" };
      const auto at = text.find(kCreator);
      if (at == std::string::npos) {
        ADD_FAILURE() << "fixture header changed";
        return {};
      }
      text.insert(at + kCreator.size(),
        "    SceneInfo: \"SceneInfo::GlobalInfo\", \"UserData\" {\n"
        "        Properties70: {\n"
        "            P: \"Original|ApplicationName\", \"KString\", \"\", \"\", "
        "\""
          + std::string(application)
          + "\"\n"
            "        }\n"
            "    }\n");
    }
    const auto path = dir.Path() / fixture.filename();
    oxygen::cooker::test::WriteText(path, text);
    const auto map
      = oxygen::cooker::test::ModelPath("static_tangent_normal.png");
    std::filesystem::copy_file(map, dir.Path() / map.filename());
    const auto source_id = path.string();
    auto input = adapters::AdapterInput {};
    input.source_id_prefix = source_id;
    input.request.source_path = path;
    auto adapter = adapters::FbxAdapter {};
    if (!adapter.Parse(path, input).success) {
      ADD_FAILURE() << "fixture failed to parse";
      return {};
    }
    return adapter.PrepareTextures(input);
  }

  //! FBX records no normal map convention; its authoring application decides
  //! it: 3ds Max writes green-down maps, Maya green-up ones.
  NOLINT_TEST(
    ModelTextureSourceTest, FbxNormalMapsFollowTheAuthoringApplication)
  {
    const auto max = PrepareFbxAuthoredBy("3ds Max");
    const auto maya = PrepareFbxAuthoredBy("Maya");

    ASSERT_TRUE(max.success && maya.success);
    EXPECT_EQ(NormalMapFlip(max), false);
    EXPECT_EQ(NormalMapFlip(maya), true);
    EXPECT_FALSE(oxygen::cooker::test::HasDiagnosticCode(
      max.diagnostics, "fbx.normal_map_convention_assumed"));
    EXPECT_FALSE(oxygen::cooker::test::HasDiagnosticCode(
      maya.diagnostics, "fbx.normal_map_convention_assumed"));
  }

  //! An FBX from an application Oxygen does not know has its normal maps taken
  //! green-up, as most applications write them, and the import says so.
  NOLINT_TEST(
    ModelTextureSourceTest, FbxUnknownApplicationNormalMapsAreReported)
  {
    const auto prepared = PrepareFbxAuthoredBy({});

    ASSERT_TRUE(prepared.success);
    EXPECT_EQ(NormalMapFlip(prepared), true);
    EXPECT_TRUE(oxygen::cooker::test::HasDiagnosticCode(
      prepared.diagnostics, "fbx.normal_map_convention_assumed"))
      << oxygen::cooker::test::DiagnosticSummary(prepared.diagnostics);
  }

  //! A missing texture is looked for beside the model, in its textures folders,
  //! then the same in the model folder's parent.
  NOLINT_TEST(TextureFallbackPathsTest, SearchesModelThenParentFolders)
  {
    const auto root = std::filesystem::absolute("pack");
    const auto paths = adapters::TextureFallbackPaths(
      root / "Models" / "model.fbx", "C:/artist/temp/wall.png");

    auto expected = std::vector<std::filesystem::path> {
      root / "Models" / "wall.png",
      root / "Models" / "textures" / "wall.png",
      root / "Models" / "Textures" / "wall.png",
      root / "wall.png",
      root / "textures" / "wall.png",
      root / "Textures" / "wall.png",
    };
    // A case-insensitive file system names each folder spelling once.
    const auto distinct = [](std::vector<std::filesystem::path> list)
      -> std::vector<std::filesystem::path> {
      auto keys = std::vector<std::string> {};
      std::erase_if(list, [&](const std::filesystem::path& path) -> bool {
        auto key = base::PathIdentityKey(path);
        if (std::ranges::find(keys, key) != keys.end()) {
          return true;
        }
        keys.push_back(std::move(key));
        return false;
      });
      return list;
    };
    expected = distinct(expected);
    ASSERT_EQ(paths.size(), expected.size());
    for (size_t index = 0; index < paths.size(); ++index) {
      EXPECT_EQ(base::PathIdentityKey(paths.at(index)),
        base::PathIdentityKey(expected.at(index)));
    }
  }

  //! The authored path itself and a path that names no file are not listed.
  NOLINT_TEST(TextureFallbackPathsTest, SkipsAuthoredPathAndFolders)
  {
    const auto model = std::filesystem::absolute("pack") / "model.gltf";
    const auto authored = model.parent_path() / "wall.png";
    const auto paths = adapters::TextureFallbackPaths(model, authored);
    EXPECT_TRUE(std::ranges::none_of(
      paths, [&](const std::filesystem::path& path) -> bool {
        return base::PathIdentityKey(path) == base::PathIdentityKey(authored);
      }));
    EXPECT_TRUE(adapters::TextureFallbackPaths(
      model, model.parent_path() / "textures" / "")
        .empty());
  }
} // namespace
} // namespace oxygen::content::import::test
