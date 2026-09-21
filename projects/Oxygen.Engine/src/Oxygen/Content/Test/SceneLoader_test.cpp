//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

#include "Mocks/MockStream.h"

#include <Oxygen/Content/DescriptorDependencies.h>
#include <Oxygen/Content/Internal/DependencyCollector.h>
#include <Oxygen/Content/LoaderContext.h>
#include <Oxygen/Content/Loaders/SceneLoader.h>
#include <Oxygen/Content/SourceToken.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/PakFormatSerioWriters.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Reader.h>
#include <Oxygen/Serio/Writer.h>
#include <Oxygen/Testing/GTest.h>

using oxygen::content::loaders::LoadSceneAsset;

namespace {

auto MakeSceneWithFlagRecord(const uint32_t values, const uint32_t inherited,
  const uint8_t version = oxygen::data::pak::world::kSceneAssetVersion)
  -> std::vector<std::byte>
{
  namespace world = oxygen::data::pak::world;
  auto descriptor = world::SceneAssetDesc {};
  descriptor.header.asset_type
    = static_cast<uint8_t>(oxygen::data::AssetType::kScene);
  descriptor.header.version = version;
  descriptor.nodes.offset = sizeof(descriptor);
  descriptor.nodes.count = 1U;
  descriptor.nodes.entry_size = sizeof(world::NodeRecord);
  auto node = world::NodeRecord {};
  node.node_flags = values;
  node.inherited_flags = inherited;
  node.translation[0] = 3.5F;
  node.scale[2] = 2.0F;
  auto environment = world::SceneEnvironmentBlockHeader {};
  environment.byte_size = sizeof(environment);
  auto bytes = std::vector<std::byte>(
    sizeof(descriptor) + sizeof(node) + sizeof(environment));
  std::memcpy(bytes.data(), &descriptor, sizeof(descriptor));
  std::memcpy(bytes.data() + sizeof(descriptor), &node, sizeof(node));
  std::memcpy(bytes.data() + sizeof(descriptor) + sizeof(node), &environment,
    sizeof(environment));
  return bytes;
}

NOLINT_TEST(SceneFlagRecordTest, LoaderPreservesFlagModesAndFollowingTransform)
{
  namespace world = oxygen::data::pak::world;
  auto bytes = MakeSceneWithFlagRecord(world::kSceneNodeFlag_Visible,
    world::kSceneNodeFlag_CastsShadows | world::kSceneNodeFlag_ReceivesShadows);
  auto stream = oxygen::serio::MemoryStream(std::span<std::byte>(bytes));
  auto reader = oxygen::serio::Reader(stream);
  auto context = oxygen::content::LoaderContext {};
  context.desc_reader = &reader;
  context.parse_only = true;
  const auto scene = LoadSceneAsset(context);
  ASSERT_NE(scene, nullptr);
  const auto nodes = scene->GetNodes();
  ASSERT_EQ(nodes.size(), 1U);
  EXPECT_EQ(nodes.front().node_flags, world::kSceneNodeFlag_Visible);
  EXPECT_EQ(nodes.front().inherited_flags,
    world::kSceneNodeFlag_CastsShadows | world::kSceneNodeFlag_ReceivesShadows);
  EXPECT_FLOAT_EQ(nodes.front().translation[0], 3.5F);
  EXPECT_FLOAT_EQ(nodes.front().scale[2], 2.0F);
}

NOLINT_TEST(
  SceneFlagRecordTest, RejectsUnknownAndAmbiguousFlagsWithoutStringTable)
{
  namespace world = oxygen::data::pak::world;
  for (const auto [values, inherited] :
    std::array { std::pair { uint32_t { 1U << 31U }, uint32_t { 0 } },
      std::pair { uint32_t { 0 }, world::kSceneNodeFlag_Static },
      std::pair {
        world::kSceneNodeFlag_Visible, world::kSceneNodeFlag_Visible } }) {
    auto bytes = MakeSceneWithFlagRecord(values, inherited);
    EXPECT_THROW((oxygen::data::SceneAsset(oxygen::data::AssetKey {}, bytes)),
      std::runtime_error);
    auto stream = oxygen::serio::MemoryStream(std::span<std::byte>(bytes));
    auto reader = oxygen::serio::Reader(stream);
    auto context = oxygen::content::LoaderContext {};
    context.desc_reader = &reader;
    context.parse_only = true;
    EXPECT_THROW(LoadSceneAsset(context), std::runtime_error);
  }
}

NOLINT_TEST(SceneFlagRecordTest, RejectsInvalidNodeReferencesWithoutStringTable)
{
  namespace world = oxygen::data::pak::world;
  for (const auto [parent, name] :
    std::array { std::pair { 1U, 0U }, std::pair { 0U, 1U } }) {
    auto bytes = MakeSceneWithFlagRecord(0U, 0U);
    auto node = world::NodeRecord {};
    node.parent_index = parent;
    node.scene_name_offset = name;
    std::memcpy(
      bytes.data() + sizeof(world::SceneAssetDesc), &node, sizeof(node));
    EXPECT_THROW((oxygen::data::SceneAsset(oxygen::data::AssetKey {}, bytes)),
      std::runtime_error);
    auto stream = oxygen::serio::MemoryStream(std::span<std::byte>(bytes));
    auto reader = oxygen::serio::Reader(stream);
    auto context = oxygen::content::LoaderContext {};
    context.desc_reader = &reader;
    context.parse_only = true;
    EXPECT_THROW(LoadSceneAsset(context), std::runtime_error);
  }
}

NOLINT_TEST(SceneFlagRecordTest, RejectsRetiredSceneVersionFour)
{
  auto bytes = MakeSceneWithFlagRecord(0U, 0U, 4U);
  EXPECT_THROW((oxygen::data::SceneAsset(oxygen::data::AssetKey {}, bytes)),
    std::runtime_error);
  auto stream = oxygen::serio::MemoryStream(std::span<std::byte>(bytes));
  auto reader = oxygen::serio::Reader(stream);
  auto context = oxygen::content::LoaderContext {};
  context.desc_reader = &reader;
  context.parse_only = true;
  EXPECT_THROW(LoadSceneAsset(context), std::runtime_error);
}

auto MakeExposureScene() -> std::vector<std::byte>
{
  namespace world = oxygen::data::pak::world;
  auto descriptor = world::SceneAssetDesc {};
  descriptor.header.asset_type
    = static_cast<uint8_t>(oxygen::data::AssetType::kScene);
  descriptor.header.version = world::kSceneAssetVersion;
  auto post = world::PostProcessVolumeEnvironmentRecord {};
  post.curve_key_count = 2U;
  post.header.record_size
    = sizeof(post) + 2U * sizeof(world::ExposureCompensationKeyRecord);
  post.auto_exposure_black_influence = 0.35F;
  post.auto_exposure_transition_distance_ev = 2.25F;
  post.auto_exposure_metering_mask
    = oxygen::data::pak::core::ResourceIndexT { 7U };
  const auto environment = world::SceneEnvironmentBlockHeader {
    .byte_size
    = sizeof(world::SceneEnvironmentBlockHeader) + post.header.record_size,
    .systems_count = 1U,
  };
  oxygen::serio::MemoryStream stream;
  oxygen::serio::Writer writer(stream);
  const auto packed = writer.ScopedAlignment(1);
  if (!writer.Write(descriptor) || !writer.Write(environment)
    || !writer.Write(post)
    || !writer.Write(world::ExposureCompensationKeyRecord { -4.0F, 1.0F })
    || !writer.Write(world::ExposureCompensationKeyRecord { 12.0F, -0.5F })) {
    throw std::runtime_error("Could not write scene exposure test fixture");
  }
  const auto bytes = stream.Data();
  return { bytes.begin(), bytes.end() };
}

NOLINT_TEST(SceneExposureRecordTest, PreservesCurrentPrefixAndVariableCurve)
{
  const auto bytes = MakeExposureScene();
  const auto scene = oxygen::data::SceneAsset(oxygen::data::AssetKey {}, bytes);
  const auto post = scene.TryGetPostProcessVolumeEnvironment();
  ASSERT_TRUE(post.has_value());
  EXPECT_EQ(post->header.record_size, 160U);
  EXPECT_EQ(post->auto_exposure_metering_mask.get(), 7U);
  EXPECT_FLOAT_EQ(post->auto_exposure_black_influence, 0.35F);
  EXPECT_FLOAT_EQ(post->auto_exposure_transition_distance_ev, 2.25F);
  const auto keys = scene.GetPostProcessCompensationCurve();
  ASSERT_EQ(keys.size(), 2U);
  EXPECT_FLOAT_EQ(keys[0].metered_ev, -4.0F);
  EXPECT_FLOAT_EQ(keys[0].compensation_ev, 1.0F);
  EXPECT_FLOAT_EQ(keys[1].metered_ev, 12.0F);
  EXPECT_FLOAT_EQ(keys[1].compensation_ev, -0.5F);
}

NOLINT_TEST(SceneExposureRecordTest, RejectsObsoleteAndMalformedLayouts)
{
  namespace world = oxygen::data::pak::world;
  using Record = world::PostProcessVolumeEnvironmentRecord;
  constexpr auto kRecordStart = sizeof(world::SceneAssetDesc)
    + sizeof(world::SceneEnvironmentBlockHeader);
  // These are independent wire offsets: the retired prefix, unsupported
  // extension, mismatched/over-limit counts, and each reserved word.
  for (const auto [offset, value] :
    std::array { std::pair { 4U, 104U }, std::pair { 104U, 0U },
      std::pair { 104U, 2U }, std::pair { 132U, 1U }, std::pair { 132U, 65U },
      std::pair { 120U, 1U }, std::pair { 124U, 1U }, std::pair { 128U, 1U },
      std::pair { 136U, 1U }, std::pair { 140U, 1U } }) {
    SCOPED_TRACE(offset);
    auto bytes = MakeExposureScene();
    oxygen::serio::MemoryStream stream { std::span<std::byte>(bytes) };
    oxygen::serio::Writer writer(stream);
    const auto packed = writer.ScopedAlignment(1);
    ASSERT_TRUE(stream.Seek(kRecordStart + offset));
    ASSERT_TRUE(writer.Write(value));
    EXPECT_THROW((oxygen::data::SceneAsset(oxygen::data::AssetKey {}, bytes)),
      std::runtime_error);
  }
  auto bytes = MakeExposureScene();
  oxygen::serio::MemoryStream stream { std::span<std::byte>(bytes) };
  oxygen::serio::Writer writer(stream);
  const auto packed = writer.ScopedAlignment(1);
  ASSERT_TRUE(stream.Seek(kRecordStart + sizeof(Record)
    + sizeof(world::ExposureCompensationKeyRecord)));
  ASSERT_TRUE(writer.Write(-4.0F));
  EXPECT_THROW((oxygen::data::SceneAsset(oxygen::data::AssetKey {}, bytes)),
    std::runtime_error);
}

NOLINT_TEST(SceneExposureRecordTest, RejectsSceneVersionFive)
{
  auto bytes = MakeSceneWithFlagRecord(0U, 0U, 5U);
  EXPECT_THROW((oxygen::data::SceneAsset(oxygen::data::AssetKey {}, bytes)),
    std::runtime_error);
}

class SceneLoaderTest : public testing::Test {
protected:
  using MockStream = oxygen::content::testing::MockStream;
  using Reader = oxygen::serio::Reader<MockStream>;
  using Writer = oxygen::serio::Writer<MockStream>;

  SceneLoaderTest()
    : writer_(stream_)
    , reader_(stream_)
  {
  }

  auto MakeContextParseOnly() -> oxygen::content::LoaderContext
  {
    if (!stream_.Seek(0)) {
      throw std::runtime_error("Failed to seek stream");
    }
    return oxygen::content::LoaderContext {
      .current_asset_key = oxygen::data::AssetKey {},
      .desc_reader = &reader_,
      .work_offline = true,
      .parse_only = true,
    };
  }

  auto MakeContextDecode() -> std::pair<oxygen::content::LoaderContext,
    std::shared_ptr<oxygen::content::internal::DependencyCollector>>
  {
    if (!stream_.Seek(0)) {
      throw std::runtime_error("Failed to seek stream");
    }

    auto collector
      = std::make_shared<oxygen::content::internal::DependencyCollector>();

    return { oxygen::content::LoaderContext {
               .current_asset_key = oxygen::data::AssetKey {},
               .source_token = oxygen::content::internal::SourceToken(1U),
               .desc_reader = &reader_,
               .work_offline = true,
               .dependency_collector = collector,
               .source_pak = nullptr,
               .parse_only = false,
             },
      collector };
  }

  auto WriteEmptyEnvironmentBlock() -> void
  {
    using oxygen::data::pak::world::SceneEnvironmentBlockHeader;

    SceneEnvironmentBlockHeader header {};
    header.byte_size = sizeof(SceneEnvironmentBlockHeader);
    header.systems_count = 0;

    const auto header_write = writer_.WriteBlob(std::span<const std::byte>(
      reinterpret_cast<const std::byte*>(&header), sizeof(header)));
    ASSERT_TRUE(header_write) << header_write.error().message();
  }

  auto WriteMinimalSceneWithRenderable(
    const oxygen::data::AssetKey geometry_key,
    const oxygen::data::AssetKey material_key = {}) -> void
  {
    using oxygen::data::pak::world::NodeRecord;
    using oxygen::data::pak::world::RenderableRecord;
    using oxygen::data::pak::world::SceneAssetDesc;

    SceneAssetDesc desc {};
    desc.header.asset_type
      = static_cast<uint8_t>(oxygen::data::AssetType::kScene);
    desc.header.version = oxygen::data::pak::world::kSceneAssetVersion;

    // Layout:
    // [SceneAssetDesc][NodeRecord x1][StringTable "\0root\0"][Directory
    // x1][Renderable x1]
    const uint32_t offset_nodes = static_cast<uint32_t>(sizeof(SceneAssetDesc));
    const uint32_t nodes_bytes = static_cast<uint32_t>(sizeof(NodeRecord));

    const std::array<std::byte, 6> strings
      = { std::byte { 0 }, std::byte { 'r' }, std::byte { 'o' },
          std::byte { 'o' }, std::byte { 't' }, std::byte { 0 } };
    const uint32_t offset_strings = offset_nodes + nodes_bytes;
    const uint32_t strings_bytes = static_cast<uint32_t>(strings.size());

    const uint32_t offset_directory = offset_strings + strings_bytes;
    const uint32_t dir_bytes = static_cast<uint32_t>(
      sizeof(oxygen::data::pak::world::SceneComponentTableDesc));

    const uint32_t offset_renderables = offset_directory + dir_bytes;

    desc.nodes.offset = offset_nodes;
    desc.nodes.count = 1;
    desc.nodes.entry_size = sizeof(NodeRecord);

    desc.scene_strings.offset = offset_strings;
    desc.scene_strings.size = strings_bytes;

    desc.component_table_directory_offset = offset_directory;
    desc.component_table_count = 1;

    // Write desc as raw bytes (packed, no floats).
    auto desc_write = writer_.WriteBlob(std::span<const std::byte>(
      reinterpret_cast<const std::byte*>(&desc), sizeof(desc)));
    ASSERT_TRUE(desc_write) << desc_write.error().message();

    NodeRecord node {};
    node.scene_name_offset = 1; // "root"
    node.parent_index = 0;

    auto node_write = writer_.WriteBlob(std::span<const std::byte>(
      reinterpret_cast<const std::byte*>(&node), sizeof(node)));
    ASSERT_TRUE(node_write) << node_write.error().message();

    auto strings_write = writer_.WriteBlob(strings);
    ASSERT_TRUE(strings_write) << strings_write.error().message();

    oxygen::data::pak::world::SceneComponentTableDesc table_desc {};
    table_desc.component_type
      = static_cast<uint32_t>(oxygen::data::ComponentType::kRenderable);
    table_desc.table.offset = offset_renderables;
    table_desc.table.count = 1;
    table_desc.table.entry_size = sizeof(RenderableRecord);

    auto dir_write = writer_.WriteBlob(std::span<const std::byte>(
      reinterpret_cast<const std::byte*>(&table_desc), sizeof(table_desc)));
    ASSERT_TRUE(dir_write) << dir_write.error().message();

    RenderableRecord renderable {};
    renderable.node_index = 0;
    renderable.geometry_key = geometry_key;
    renderable.material_key = material_key;

    auto rend_write = writer_.WriteBlob(std::span<const std::byte>(
      reinterpret_cast<const std::byte*>(&renderable), sizeof(renderable)));
    ASSERT_TRUE(rend_write) << rend_write.error().message();

    WriteEmptyEnvironmentBlock();

    auto flush_res = writer_.Flush();
    ASSERT_TRUE(flush_res) << flush_res.error().message();
  }

  MockStream stream_;
  Writer writer_;
  Reader reader_;
};

NOLINT_TEST_F(SceneLoaderTest, LoadSceneParseOnlySucceeds)
{
  WriteMinimalSceneWithRenderable(oxygen::data::AssetKey {});

  auto asset = LoadSceneAsset(MakeContextParseOnly());
  ASSERT_NE(asset, nullptr);
  EXPECT_EQ(asset->GetNodes().size(), 1U);
  EXPECT_EQ(asset->GetNodeName(asset->GetRootNode()), "root");
}

NOLINT_TEST_F(SceneLoaderTest, LoadSceneDecodeCollectsRenderableDependencies)
{
  auto geom_bytes
    = std::array<std::uint8_t, oxygen::data::AssetKey::kSizeBytes> {};
  geom_bytes[0] = 0xABU;
  geom_bytes[1] = 0xCDU;
  const auto geom = oxygen::data::AssetKey::FromBytes(geom_bytes);
  auto material_bytes
    = std::array<std::uint8_t, oxygen::data::AssetKey::kSizeBytes> {};
  material_bytes[0] = 0x12U;
  material_bytes[1] = 0x34U;
  const auto material = oxygen::data::AssetKey::FromBytes(material_bytes);

  WriteMinimalSceneWithRenderable(geom, material);

  auto [context, collector] = MakeContextDecode();
  auto asset = LoadSceneAsset(context);
  ASSERT_NE(asset, nullptr);

  EXPECT_THAT(collector->AssetDependencies(),
    ::testing::UnorderedElementsAre(geom, material));
}

//! Inspection collects scene references with no mounted sources or resource
//! readers.
NOLINT_TEST_F(SceneLoaderTest, InspectDependenciesInParseOnlyMode)
{
  const auto geometry
    = oxygen::data::AssetKey::FromVirtualPath("/Art/Mesh.ogeo");
  const auto material
    = oxygen::data::AssetKey::FromVirtualPath("/Art/Material.omat");
  WriteMinimalSceneWithRenderable(geometry, material);
  auto context = MakeContextParseOnly();
  const auto result = oxygen::content::InspectDescriptorDependencies(
    *context.desc_reader, {}, oxygen::data::AssetType::kScene);
  EXPECT_TRUE(result.complete);
  ASSERT_EQ(result.assets.size(), 2U);
  EXPECT_NE(std::find(result.assets.begin(), result.assets.end(), geometry),
    result.assets.end());
  EXPECT_NE(std::find(result.assets.begin(), result.assets.end(), material),
    result.assets.end());
}

} // namespace
