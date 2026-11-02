//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Base/Span.h>
#include <Oxygen/Content/Internal/DependencyCollector.h>
#include <Oxygen/Content/LoaderContext.h>
#include <Oxygen/Content/Loaders/SceneLoader.h>
#include <Oxygen/Cooker/testing/DescriptorDefects.h>
#include <Oxygen/Cooker/testing/SceneBuilder.h>
#include <Oxygen/Core/Meta/Data/ResourceIndex.h>
#include <Oxygen/Core/Types/PostProcess.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/PakFormatSerioWriters.h> // IWYU pragma: keep
#include <Oxygen/Data/Schema/Generated.SceneComponents.h>
#include <Oxygen/Data/SourceOrigin.h>
#include <Oxygen/Data/SurfaceId.h>
#include <Oxygen/Serio/ByteReader.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Reader.h>
#include <Oxygen/Serio/Writer.h>
#include <Oxygen/Testing/GTest.h>

using oxygen::content::loaders::LoadSceneAsset;

using oxygen::base::CheckedAt;

namespace {

using oxygen::cooker::testing::DescriptorDefects;
using oxygen::cooker::testing::NodeFlags;
using oxygen::cooker::testing::NodeTransform;
using oxygen::cooker::testing::SceneBuilder;

//! A valid one-node scene whose node has a transform after its flag words.
auto MakeSceneWithFlags(const NodeFlags flags = {}) -> std::vector<std::byte>
{
  auto scene = SceneBuilder { "test" };
  const auto root = scene.Node("");
  constexpr float kTranslation = 3.5F;
  scene.Place(root,
    NodeTransform {
      .translation = { kTranslation, 0.0F, 0.0F },
      .scale = { 1.0F, 1.0F, 2.0F },
    });
  scene.Flags(root, flags);
  return scene.Bytes();
}

//! A valid one-node scene whose node holds the raw flag words, whether or not
//! a cook writes them.
auto MakeSceneWithRawFlags(const NodeFlags flags) -> std::vector<std::byte>
{
  auto bytes = MakeSceneWithFlags();
  auto node = DescriptorDefects::NodeAt(bytes, 0U);
  node.node_flags = flags.values;
  node.inherited_flags = flags.inherited;
  DescriptorDefects::SetNode(bytes, 0U, node);
  return bytes;
}

NOLINT_TEST(SceneFlagRecordTest, LoaderPreservesFlagModesAndFollowingTransform)
{
  namespace world = oxygen::data::pak::world;
  auto bytes = MakeSceneWithFlags({
    .values = world::kSceneNodeFlag_Visible,
    .inherited = world::kSceneNodeFlag_CastsShadows
      | world::kSceneNodeFlag_ReceivesShadows,
  });
  auto stream
    = oxygen::serio::ReadOnlyMemoryStream(std::span<const std::byte>(bytes));
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

NOLINT_TEST(SceneFlagRecordTest, RejectsUnknownAndAmbiguousFlags)
{
  namespace world = oxygen::data::pak::world;
  for (const auto [values, inherited] : std::array {
         std::pair { uint32_t { 1U << 31U }, uint32_t { 0 } },
         std::pair { uint32_t { 0 }, world::kSceneNodeFlag_Static },
         std::pair {
           world::kSceneNodeFlag_Visible, world::kSceneNodeFlag_Visible },
       }) {
    auto bytes
      = MakeSceneWithRawFlags({ .values = values, .inherited = inherited });
    EXPECT_THROW((oxygen::data::SceneAsset(oxygen::data::AssetKey {}, bytes)),
      std::runtime_error);
    auto stream
      = oxygen::serio::ReadOnlyMemoryStream(std::span<const std::byte>(bytes));
    auto reader = oxygen::serio::Reader(stream);
    auto context = oxygen::content::LoaderContext {};
    context.desc_reader = &reader;
    context.parse_only = true;
    EXPECT_THROW(LoadSceneAsset(context), std::runtime_error);
  }
}

//! A parent past the last node, or a name past the end of the string table.
NOLINT_TEST(SceneFlagRecordTest, RejectsInvalidNodeReferences)
{
  const auto valid = MakeSceneWithFlags();
  const auto desc
    = oxygen::serio::CopyAt<oxygen::data::pak::world::SceneAssetDesc>(
      std::span<const std::byte>(valid));
  ASSERT_HAS_VALUE(desc);
  const auto past_strings = desc->header.strings.size;
  for (const auto [parent, name] :
    std::array { std::pair { 1U, 0U }, std::pair { 0U, past_strings } }) {
    auto bytes = valid;
    auto node = DescriptorDefects::NodeAt(bytes, 0U);
    node.parent_index = parent;
    node.scene_name_offset = name;
    DescriptorDefects::SetNode(bytes, 0U, node);
    EXPECT_THROW((oxygen::data::SceneAsset(oxygen::data::AssetKey {}, bytes)),
      std::runtime_error);
    auto stream
      = oxygen::serio::ReadOnlyMemoryStream(std::span<const std::byte>(bytes));
    auto reader = oxygen::serio::Reader(stream);
    auto context = oxygen::content::LoaderContext {};
    context.desc_reader = &reader;
    context.parse_only = true;
    EXPECT_THROW(LoadSceneAsset(context), std::runtime_error);
  }
}

NOLINT_TEST(SceneFlagRecordTest, RejectsRetiredSceneVersionFour)
{
  auto bytes = MakeSceneWithFlags();
  DescriptorDefects::SetVersion(bytes, 4U);
  EXPECT_THROW((oxygen::data::SceneAsset(oxygen::data::AssetKey {}, bytes)),
    std::runtime_error);
  auto stream
    = oxygen::serio::ReadOnlyMemoryStream(std::span<const std::byte>(bytes));
  auto reader = oxygen::serio::Reader(stream);
  auto context = oxygen::content::LoaderContext {};
  context.desc_reader = &reader;
  context.parse_only = true;
  EXPECT_THROW(LoadSceneAsset(context), std::runtime_error);
}

//! A scene with a post process volume, and the metering mask it names.
struct ExposureScene final {
  std::vector<std::byte> bytes;
  oxygen::data::ResourceReferenceIndex metering_mask;
};

auto MakeExposureScene() -> ExposureScene
{
  namespace world = oxygen::data::pak::world;
  auto scene = SceneBuilder { "test" };
  auto post = world::PostProcessVolumeEnvironmentRecord {};
  post.curve_key_count = 2U;
  constexpr float kBlackInfluence = 0.35F;
  post.auto_exposure_black_influence = kBlackInfluence;
  constexpr float kTransitionEv = 2.25F;
  post.auto_exposure_transition_distance_ev = kTransitionEv;
  constexpr uint32_t kMeteringMaskRow = 7U;
  post.auto_exposure_metering_mask
    = scene.Texture(oxygen::ResourceIndexT { kMeteringMaskRow });
  constexpr std::array keys {
    world::ExposureCompensationKeyRecord {
      .metered_ev = -4.0F,
      .compensation_ev = 1.0F,
    },
    world::ExposureCompensationKeyRecord {
      .metered_ev = 12.0F,
      .compensation_ev = -0.5F,
    },
  };
  scene.Environment(post, std::as_bytes(std::span(keys)));
  return {
    .bytes = scene.Bytes(),
    .metering_mask = post.auto_exposure_metering_mask,
  };
}

NOLINT_TEST(SceneExposureRecordTest, PreservesCurrentPrefixAndVariableCurve)
{
  const auto exposure = MakeExposureScene();
  const auto scene
    = oxygen::data::SceneAsset(oxygen::data::AssetKey {}, exposure.bytes);
  const auto post = scene.TryGetPostProcessVolumeEnvironment();
  ASSERT_HAS_VALUE(post);
  // The record's prefix, then the two curve keys the scene holds.
  EXPECT_EQ(post->header.record_size,
    sizeof(oxygen::data::pak::world::PostProcessVolumeEnvironmentRecord)
      + (2U * sizeof(oxygen::data::pak::world::ExposureCompensationKeyRecord)));
  EXPECT_EQ(post->auto_exposure_metering_mask, exposure.metering_mask);
  EXPECT_FLOAT_EQ(post->auto_exposure_black_influence, 0.35F);
  EXPECT_FLOAT_EQ(post->auto_exposure_transition_distance_ev, 2.25F);
  const auto keys = scene.GetPostProcessCompensationCurve();
  ASSERT_EQ(keys.size(), 2U);
  EXPECT_FLOAT_EQ(CheckedAt(keys, 0).metered_ev, -4.0F);
  EXPECT_FLOAT_EQ(CheckedAt(keys, 0).compensation_ev, 1.0F);
  EXPECT_FLOAT_EQ(CheckedAt(keys, 1).metered_ev, 12.0F);
  EXPECT_FLOAT_EQ(CheckedAt(keys, 1).compensation_ev, -0.5F);
}

NOLINT_TEST(SceneExposureRecordTest, RejectsObsoleteAndMalformedLayouts)
{
  namespace world = oxygen::data::pak::world;
  using Record = world::PostProcessVolumeEnvironmentRecord;
  // The retired prefix size, an extension version before and after the
  // current one, and a curve count that mismatches the keys or the limit.
  constexpr auto kRecordSize = offsetof(Record, header)
    + offsetof(world::SceneEnvironmentSystemRecordHeader, record_size);
  constexpr auto kExtension = offsetof(Record, exposure_extension_version);
  constexpr auto kCurveCount = offsetof(Record, curve_key_count);
  constexpr auto kRetiredPrefixSize = uint32_t { 104U };
  for (const auto [offset, value] : std::array {
         std::pair { kRecordSize, kRetiredPrefixSize },
         std::pair { kExtension, world::kExposureExtensionVersion - 1U },
         std::pair { kExtension, world::kExposureExtensionVersion + 1U },
         std::pair { kCurveCount, uint32_t { 1U } },
         std::pair { kCurveCount,
           static_cast<uint32_t>(
             oxygen::engine::kMaxExposureCompensationCurveKeys + 1U) },
       }) {
    SCOPED_TRACE(offset);
    auto bytes = MakeExposureScene().bytes;
    const auto record = DescriptorDefects::EnvironmentRecord(
      bytes, world::EnvironmentComponentType::kPostProcessVolume);
    std::memcpy(record.subspan(offset).data(), &value, sizeof(value));
    EXPECT_THROW((oxygen::data::SceneAsset(oxygen::data::AssetKey {}, bytes)),
      std::runtime_error);
  }
  auto bytes = MakeExposureScene().bytes;
  const auto record = DescriptorDefects::EnvironmentRecord(
    bytes, world::EnvironmentComponentType::kPostProcessVolume);
  constexpr auto kBadValue = -4.0F;
  std::memcpy(
    record
      .subspan(sizeof(Record) + sizeof(world::ExposureCompensationKeyRecord))
      .data(),
    &kBadValue, sizeof(kBadValue));
  EXPECT_THROW((oxygen::data::SceneAsset(oxygen::data::AssetKey {}, bytes)),
    std::runtime_error);
}

NOLINT_TEST(SceneExposureRecordTest, RejectsSceneVersionFive)
{
  constexpr uint8_t kRetiredSceneVersion = 5U;
  auto bytes = MakeSceneWithFlags();
  DescriptorDefects::SetVersion(bytes, kRetiredSceneVersion);
  EXPECT_THROW((oxygen::data::SceneAsset(oxygen::data::AssetKey {}, bytes)),
    std::runtime_error);
}

class SceneLoaderTest : public testing::Test {
protected:
  using MemoryStream = oxygen::serio::MemoryStream;
  using Reader = oxygen::serio::Reader<MemoryStream>;
  using Writer = oxygen::serio::Writer<MemoryStream>;

  SceneLoaderTest()
    : writer_(stream_)
    , reader_(stream_)
  {
  }

  auto MakeContextParseOnly() -> oxygen::content::LoaderContext
  {
    if (!stream_.Seek(oxygen::serio::ByteOffset { 0 })) {
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
    if (!stream_.Seek(oxygen::serio::ByteOffset { 0 })) {
      throw std::runtime_error("Failed to seek stream");
    }

    auto collector
      = std::make_shared<oxygen::content::internal::DependencyCollector>();

    return { oxygen::content::LoaderContext {
               .current_asset_key = oxygen::data::AssetKey {},
               .source_instance = oxygen::data::SourceInstanceId(1U),
               .desc_reader = &reader_,
               .work_offline = true,
               .dependency_collector = collector,
               .parse_only = false,
             },
      collector };
  }

  struct RenderableReferences {
    oxygen::data::AssetKey geometry;
    oxygen::data::AssetKey material;
  };

  auto WriteMinimalSceneWithRenderable(const RenderableReferences references)
    -> void
  {
    auto scene = SceneBuilder { "test" };
    const auto root = scene.Node("root");
    scene.Renderable(root, references.geometry);
    if (references.material != oxygen::data::AssetKey {}) {
      auto assignment = oxygen::data::pak::world::OverrideValue {};
      assignment.as_asset = references.material;
      auto layout_revision = oxygen::base::Sha256Digest {};
      layout_revision.front() = 1U;
      scene.Override(root,
        oxygen::data::schema::OverrideId::kGeometrySurfaceMaterial, assignment,
        oxygen::data::SurfaceId::FromStableIdentity("SceneLoaderTest/slot"),
        layout_revision);
    }
    const auto bytes = scene.Bytes();
    const auto written = writer_.WriteBlob(bytes);
    ASSERT_TRUE(written) << written.error().message();
    auto flush_res = writer_.Flush();
    ASSERT_TRUE(flush_res) << flush_res.error().message();
  }

private:
  MemoryStream stream_;
  Writer writer_;
  Reader reader_;
};

NOLINT_TEST_F(SceneLoaderTest, LoadSceneParseOnlySucceeds)
{
  WriteMinimalSceneWithRenderable({});

  auto asset = LoadSceneAsset(MakeContextParseOnly());
  ASSERT_NE(asset, nullptr);
  EXPECT_EQ(asset->GetNodes().size(), 1U);
  EXPECT_EQ(asset->GetNodeName(asset->GetRootNode()), "root");
}

NOLINT_TEST_F(SceneLoaderTest, LoadSceneDecodeCollectsRenderableDependencies)
{
  const auto geom = oxygen::data::AssetKey::FromVirtualPath("/test/Mesh.ogeo");
  const auto material
    = oxygen::data::AssetKey::FromVirtualPath("/test/Surface.omat");

  WriteMinimalSceneWithRenderable({ .geometry = geom, .material = material });

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
  WriteMinimalSceneWithRenderable(
    { .geometry = geometry, .material = material });
  auto context = MakeContextParseOnly();
  const auto collector
    = std::make_shared<oxygen::content::internal::DependencyCollector>();
  context.dependency_collector = collector;
  const auto scene = LoadSceneAsset(context);
  ASSERT_NE(scene, nullptr);
  EXPECT_THAT(collector->AssetDependencies(),
    ::testing::UnorderedElementsAre(geometry, material));
}

NOLINT_TEST(
  SceneLoaderDependencyTest, SceneWithoutComponentsCollectsNoDependencies)
{
  auto bytes = MakeSceneWithFlags();
  oxygen::serio::ReadOnlyMemoryStream stream { std::span<const std::byte>(
    bytes) };
  oxygen::serio::Reader reader(stream);
  auto context = oxygen::content::LoaderContext {};
  context.desc_reader = &reader;
  context.parse_only = true;
  const auto collector
    = std::make_shared<oxygen::content::internal::DependencyCollector>();
  context.dependency_collector = collector;
  const auto scene = LoadSceneAsset(context);
  ASSERT_NE(scene, nullptr);
  EXPECT_TRUE(collector->AssetDependencies().empty());
  EXPECT_TRUE(collector->ResourceRefDependencies().empty());
  EXPECT_TRUE(collector->ResourceKeyDependencies().empty());
}

} // namespace
