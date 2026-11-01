//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Pak/PakPlanBuilder.cpp (resource compaction)

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Cooker/Pak/PakBuildReport.h>
#include <Oxygen/Cooker/Pak/PakPlan.h>
#include <Oxygen/Cooker/Pak/PakPlanBuilder.h>
#include <Oxygen/Cooker/test/Support/Diagnostics.h>
#include <Oxygen/Cooker/testing/Descriptors.h>
#include <Oxygen/Cooker/testing/LooseCookedRoot.h>
#include <Oxygen/Cooker/testing/PakWorkspace.h>
#include <Oxygen/Cooker/testing/Seeds.h>
#include <Oxygen/Core/Meta/Data/ResourceIndex.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Testing/GTest.h>

namespace {

namespace data = oxygen::data;
namespace fixtures = oxygen::cooker::testing;
namespace pak = oxygen::content::pak;

using oxygen::ResourceIndexT;
using oxygen::cooker::test::HasDiagnostic;
using oxygen::cooker::test::HasNoErrors;
using oxygen::cooker::testing::LooseCookedRoot;
using oxygen::cooker::testing::PakWorkspace;
using oxygen::cooker::testing::SeedBytes;

constexpr auto kDroppedCode = std::string_view {
  "pak.plan.unreferenced_resources_dropped",
};
constexpr auto kSidecarBufferCode = std::string_view {
  "pak.plan.sidecar_buffer_dropped",
};

//! Every resource row holds this many bytes, so row `n` lies at
//! `(n - 1) * kPayloadSize` of its data file.
constexpr auto kPayloadSize = uint32_t { sizeof(uint32_t) };

//! The binding of an asset with one resource reference.
constexpr auto kFirstBinding = data::ResourceReferenceIndex { 0U };

//! The payload of a resource row, told apart by `seed`.
auto Payload(const uint8_t seed) -> std::vector<std::byte>
{
  return SeedBytes(seed, kPayloadSize);
}

//! A library of the root every virtual path of these tests names.
auto ContentRoot(const uint8_t seed = 1U) -> LooseCookedRoot
{
  return LooseCookedRoot { "Content", seed };
}

//! Adds the material `Wood`, whose base color binds texture `texture`.
auto AddWood(LooseCookedRoot& library, const ResourceIndexT texture)
  -> data::AssetKey
{
  auto wood = fixtures::TexturedMaterialDescriptor("Wood", texture);
  return library.Add({
    .type = data::AssetType::kMaterial,
    .leaf = "Materials/Wood.omat",
    .descriptor = std::move(wood.bytes),
    .references = std::move(wood.references),
  });
}

//! Adds the geometry `Crate`, which binds buffer `buffer`.
auto AddCrate(LooseCookedRoot& library, const ResourceIndexT buffer)
  -> data::AssetKey
{
  return library.Add({
    .type = data::AssetType::kGeometry,
    .leaf = "Geometry/Crate.ogeo",
    .descriptor = fixtures::GeometryDescriptor("Crate").bytes,
    .references = data::AssetReferences::Create(
      { { .kind = data::ResourceKind::kBuffer, .index = buffer } }, {})
      .value(),
  });
}

auto TableCount(const pak::PakPlan& plan, const std::string_view name)
  -> uint32_t
{
  const auto tables = plan.Tables();
  const auto table
    = std::ranges::find(tables, name, &pak::PakTablePlan::table_name);
  return table == tables.end() ? 0U : table->count;
}

//! The diagnostics of `code`, for the tests that count them.
auto WithCode(const std::vector<pak::PakDiagnostic>& diagnostics,
  const std::string_view code) -> std::vector<pak::PakDiagnostic>
{
  auto found = std::vector<pak::PakDiagnostic> {};
  std::ranges::copy_if(diagnostics, std::back_inserter(found),
    [code](const pak::PakDiagnostic& diagnostic) -> bool {
      return diagnostic.code == code;
    });
  return found;
}

//! A full pak of a root holding entries of earlier cooks packs only the
//! resources its assets bind, renumbers them, and reports the dropped entries
//! as one information per kind.
NOLINT_TEST(PakCompactionTest, FullPakDropsStaleEntriesAndRemapsBindings)
{
  // Arrange
  auto workspace = PakWorkspace {};
  auto content = ContentRoot();
  content.AddTexture(Payload(1U)); // An earlier cook's: nothing binds it.
  const auto wood = AddWood(content, content.AddTexture(Payload(2U)));
  content.AddBuffer(Payload(3U)); // An earlier cook's: nothing binds it.
  const auto crate = AddCrate(content, content.AddBuffer(Payload(4U)));
  const auto source = workspace.Add(content);
  const auto request = workspace.FullRequest({ source });

  // Act
  const auto packed = oxygen::cooker::testing::PakWorkspace::Pack(request);

  // Assert
  ASSERT_THAT(packed.Diagnostics(), HasNoErrors());
  ASSERT_HAS_VALUE(packed.planned.plan);
  EXPECT_EQ(TableCount(*packed.planned.plan, "texture_table"), 2U);
  EXPECT_EQ(TableCount(*packed.planned.plan, "buffer_table"), 2U);
  const auto pak = workspace.Open(request.output_pak_path);
  EXPECT_EQ(pak.Resolve(wood, kFirstBinding, data::ResourceKind::kTexture),
    ResourceIndexT { 1U });
  EXPECT_EQ(pak.Resolve(crate, kFirstBinding, data::ResourceKind::kBuffer),
    ResourceIndexT { 1U });
  const auto dropped = WithCode(packed.planned.diagnostics, kDroppedCode);
  ASSERT_EQ(dropped.size(), 2U);
  for (const auto& diagnostic : dropped) {
    EXPECT_EQ(diagnostic.severity, pak::PakDiagnosticSeverity::kInfo);
    EXPECT_EQ(diagnostic.path.filename(), source.path.filename());
    EXPECT_THAT(diagnostic.message, ::testing::HasSubstr("Dropped 1 "));
  }
  EXPECT_THAT(packed.planned.diagnostics,
    ::testing::Not(HasDiagnostic(kSidecarBufferCode)));
}

//! A texture re-cooked under the same name leaves its old entry behind; the
//! pak packs only the entry the name now resolves to.
NOLINT_TEST(PakCompactionTest, TextureRecookedUnderTheSameNameKeepsTheNewEntry)
{
  // Arrange
  auto workspace = PakWorkspace {};
  auto content = ContentRoot();
  content.AddTexture(Payload(1U)); // The texture's previous cook.
  const auto sky = content.DescribeTexture(
    "Textures/Sky.otex", content.AddTexture(Payload(2U)));
  const auto request = workspace.FullRequest({ workspace.Add(content) });

  // Act
  const auto packed = oxygen::cooker::testing::PakWorkspace::Pack(request);

  // Assert
  ASSERT_THAT(packed.Diagnostics(), HasNoErrors());
  ASSERT_HAS_VALUE(packed.planned.plan);
  EXPECT_EQ(TableCount(*packed.planned.plan, "texture_table"), 2U);
  const auto pak = workspace.Open(request.output_pak_path);
  EXPECT_EQ(
    pak.Pak().ResolveTextureIndexByVirtualPath(sky), ResourceIndexT { 1U });
  const auto dropped = WithCode(packed.planned.diagnostics, kDroppedCode);
  ASSERT_EQ(dropped.size(), 1U);
  EXPECT_THAT(
    dropped.front().message, ::testing::HasSubstr("1 unreferenced texture"));
}

//! A texture re-cooked under a new name retires its old descriptor, so the old
//! entry is unreachable and the pak drops it.
NOLINT_TEST(PakCompactionTest, TextureRecookedUnderANewNameDropsTheOldEntry)
{
  // Arrange
  auto workspace = PakWorkspace {};
  auto content = ContentRoot();
  content.AddTexture(Payload(1U)); // Sky's entry; its descriptor is retired.
  const auto night = content.DescribeTexture(
    "Textures/Night.otex", content.AddTexture(Payload(2U)));
  const auto request = workspace.FullRequest({ workspace.Add(content) });

  // Act
  const auto packed = oxygen::cooker::testing::PakWorkspace::Pack(request);

  // Assert
  ASSERT_THAT(packed.Diagnostics(), HasNoErrors());
  ASSERT_HAS_VALUE(packed.planned.plan);
  EXPECT_EQ(TableCount(*packed.planned.plan, "texture_table"), 2U);
  const auto pak = workspace.Open(request.output_pak_path);
  EXPECT_EQ(
    pak.Pak().ResolveTextureIndexByVirtualPath(night), ResourceIndexT { 1U });
  EXPECT_FALSE(pak.Pak().ResolveTextureIndexByVirtualPath(
    content.VirtualPath("Textures/Sky.otex")));
  EXPECT_EQ(WithCode(packed.planned.diagnostics, kDroppedCode).size(), 1U);
}

//! A buffer a geometry binds is packed; one only a live `.obuf` sidecar names
//! is dropped with a warning that gives the sidecar's path.
NOLINT_TEST(PakCompactionTest, BufferWithoutGeometryWarnsWithItsSidecar)
{
  // Arrange
  auto workspace = PakWorkspace {};
  auto content = ContentRoot();
  const auto crate_buffer = content.AddBuffer(Payload(1U));
  AddCrate(content, crate_buffer);
  content.DescribeBuffer("Buffers/Crate.obuf", crate_buffer)
    .DescribeBuffer("Buffers/Orphan.obuf", content.AddBuffer(Payload(2U)));
  const auto request = workspace.FullRequest({ workspace.Add(content) });

  // Act
  const auto planned = pak::PakPlanBuilder {}.Build(request);

  // Assert
  ASSERT_THAT(planned.diagnostics, HasNoErrors());
  ASSERT_HAS_VALUE(planned.plan);
  EXPECT_EQ(TableCount(*planned.plan, "buffer_table"), 2U);
  const auto warnings = WithCode(planned.diagnostics, kSidecarBufferCode);
  ASSERT_EQ(warnings.size(), 1U);
  EXPECT_EQ(warnings.front().severity, pak::PakDiagnosticSeverity::kWarning);
  EXPECT_EQ(warnings.front().path.filename(), "Orphan.obuf");
  EXPECT_THAT(planned.diagnostics, ::testing::Not(HasDiagnostic(kDroppedCode)));
}

//! A patch over a compacted base packs only the resources its changed assets
//! bind: a resource an unchanged asset binds, and a keyed texture the base
//! holds, stay with the base.
NOLINT_TEST(PakCompactionTest, PatchOverCompactedBasePacksOnlyChangedResources)
{
  // Arrange
  auto workspace = PakWorkspace {};
  auto base = ContentRoot(1U);
  base.AddTexture(Payload(1U)); // An earlier cook's: nothing binds it.
  AddWood(base, base.AddTexture(Payload(2U)));
  const auto sky
    = base.DescribeTexture("Textures/Sky.otex", base.AddTexture(Payload(3U)));
  AddCrate(base, base.AddBuffer(Payload(4U)));
  const auto base_request
    = workspace.FullRequest({ workspace.Add(base) }, "base.pak");
  const auto base_packed
    = oxygen::cooker::testing::PakWorkspace::Pack(base_request);
  ASSERT_THAT(base_packed.Diagnostics(), HasNoErrors());
  ASSERT_HAS_VALUE(base_packed.planned.plan);
  ASSERT_EQ(TableCount(*base_packed.planned.plan, "texture_table"), 3U);

  // The wood is re-cooked onto a new texture; the rest is as in the base.
  auto recooked = ContentRoot(2U);
  recooked.AddTexture(Payload(1U));
  recooked.AddTexture(Payload(2U));
  recooked.DescribeTexture(
    "Textures/Sky.otex", recooked.AddTexture(Payload(3U)));
  const auto crate = AddCrate(recooked, recooked.AddBuffer(Payload(4U)));
  const auto wood = AddWood(recooked, recooked.AddTexture(Payload(5U)));
  auto request = workspace.PatchRequest({ workspace.Add(recooked) },
    { workspace.Open(base_request.output_pak_path).Pak().Catalog() });
  request.base_texture_keys = { data::TextureKeyFromVirtualPath(sky) };

  // Act
  const auto packed = oxygen::cooker::testing::PakWorkspace::Pack(request);

  // Assert
  ASSERT_THAT(packed.Diagnostics(), HasNoErrors());
  ASSERT_HAS_VALUE(packed.planned.plan);
  const auto& plan = *packed.planned.plan;
  const auto actions = plan.PatchActions();
  const auto action_of
    = [&actions](
        const data::AssetKey& key) -> std::optional<pak::PakPatchAction> {
    const auto found
      = std::ranges::find(actions, key, &pak::PakPatchActionRecord::asset_key);
    return found == actions.end() ? std::nullopt
                                  : std::optional { found->action };
  };
  EXPECT_EQ(action_of(wood), pak::PakPatchAction::kReplace);
  EXPECT_EQ(action_of(crate), pak::PakPatchAction::kUnchanged);
  EXPECT_EQ(TableCount(plan, "texture_table"), 2U);
  EXPECT_EQ(TableCount(plan, "buffer_table"), 0U);
  const auto patch = workspace.Open(request.output_pak_path);
  EXPECT_EQ(patch.Resolve(wood, kFirstBinding, data::ResourceKind::kTexture),
    ResourceIndexT { 1U });
  const auto dropped = WithCode(packed.planned.diagnostics, kDroppedCode);
  ASSERT_EQ(dropped.size(), 1U);
  EXPECT_THAT(dropped.front().message, ::testing::HasSubstr("Dropped 2 "));
}

} // namespace
