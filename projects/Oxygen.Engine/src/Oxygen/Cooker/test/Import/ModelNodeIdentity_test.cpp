//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <Oxygen/Base/Uuid.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/Internal/ModelNodeIdentity.h>
#include <Oxygen/Cooker/Import/Internal/NodePathTable.h>
#include <Oxygen/Cooker/Import/ModelImportState.h>
#include <Oxygen/Cooker/Import/SurfaceProvenance.h>
#include <Oxygen/Data/SceneIds.h>
#include <Oxygen/Testing/GTest.h>

namespace {
using oxygen::Uuid;
using oxygen::content::import::ImportDiagnostic;
using oxygen::content::import::ImportRequest;
using oxygen::content::import::ModelImportInputs;
using oxygen::content::import::ModelNodeContinuity;
using oxygen::content::import::SurfaceProvenance;
using oxygen::content::import::internal::ModelNodeIdentity;
using oxygen::content::import::internal::NodePathTable;
using oxygen::data::NodeId;

auto Identity() -> Uuid
{
  const auto identity
    = Uuid::FromString("0196f2a0-5b4c-7d3e-8f10-2a3b4c5d6e7f");
  if (!identity.has_value()) {
    throw std::logic_error("Invalid fixture source identity");
  }
  return identity.value();
}

//! An anchor is the path of names from the model's roots; same-named siblings
//! are numbered from the second, unnamed ones from the first, and separators
//! in a name are escaped.
NOLINT_TEST(ModelNodeIdentityTest, AnchorsFollowTheSourceNamePath)
{
  auto identity = ModelNodeIdentity(Identity());

  const auto body = identity.SourceAnchor({ .name = "Body" });
  const auto door = identity.SourceAnchor({
    .parent_anchor = body,
    .name = "Door_FL",
  });
  const auto first = identity.SourceAnchor({
    .parent_anchor = body,
    .name = "Mirror",
  });
  const auto second = identity.SourceAnchor({
    .parent_anchor = body,
    .name = "Mirror",
  });
  const auto escaped = identity.SourceAnchor({ .name = "a/b[c]#d\\e" });
  const auto unnamed_root = identity.SourceAnchor({});
  const auto unnamed = identity.SourceAnchor({ .parent_anchor = body });
  const auto unnamed_second = identity.SourceAnchor({ .parent_anchor = body });

  EXPECT_EQ(door, "Body/Door_FL");
  EXPECT_EQ(first, "Body/Mirror");
  EXPECT_EQ(second, "Body/Mirror[2]");
  EXPECT_EQ(unnamed_root, "[1]");
  EXPECT_EQ(unnamed, "Body/[1]");
  EXPECT_EQ(unnamed_second, "Body/[2]");
  EXPECT_EQ(escaped, "a\\/b\\[c]\\#d\\\\e");
  EXPECT_EQ(ModelNodeIdentity::AddedAnchor({
              .owner_anchor = door,
              .role = "Light",
            }),
    "Body/Door_FL#light");
}

//! A node's ID derives from the source identity and its anchor alone: the
//! same anchor gives the same ID on every import, another anchor another ID.
NOLINT_TEST(ModelNodeIdentityTest, IdsAreStablePerSourceAndAnchor)
{
  const auto first = ModelNodeIdentity(Identity());
  const auto again = ModelNodeIdentity(Identity());
  const auto other = ModelNodeIdentity(Uuid::Generate());

  EXPECT_EQ(first.IdOf("Body/Door_FL"), again.IdOf("Body/Door_FL"));
  EXPECT_NE(first.IdOf("Body/Door_FL"), first.IdOf("Body/Door_FR"));
  EXPECT_NE(first.IdOf("Body/Door_FL"), other.IdOf("Body/Door_FL"));
  EXPECT_FALSE(first.IdOf("Body").IsNil());
}

//! An import without a record still has node IDs, but they change on every
//! import, which it reports. With a record, it reports nothing.
NOLINT_TEST(ModelNodeIdentityTest, ReportsAMissingSourceIdentity)
{
  auto diagnostics = std::vector<ImportDiagnostic> {};
  static_cast<void>(
    ModelNodeIdentity::ForRequest(ImportRequest {}, "model.glb", diagnostics));
  ASSERT_EQ(diagnostics.size(), 1U);
  EXPECT_EQ(diagnostics.front().code, "import.source_identity_missing");

  auto request = ImportRequest {};
  request.surface_provenance
    = std::make_shared<const SurfaceProvenance>(Identity());
  request.model_import = ModelImportInputs {};
  diagnostics.clear();
  const auto identity
    = ModelNodeIdentity::ForRequest(request, "model.glb", diagnostics);
  EXPECT_TRUE(diagnostics.empty());
  EXPECT_EQ(identity.IdOf("Body"), ModelNodeIdentity(Identity()).IdOf("Body"));
}

//! The codes of `diagnostics`, in order.
auto Codes(const std::vector<ImportDiagnostic>& diagnostics)
  -> std::vector<std::string>
{
  auto codes = std::vector<std::string> {};
  for (const auto& diagnostic : diagnostics) {
    codes.push_back(diagnostic.code);
  }
  return codes;
}

auto WithContinuity(std::vector<ModelNodeContinuity> entries)
  -> ModelNodeIdentity
{
  return ModelNodeIdentity(Identity(),
    ModelImportInputs {
      .node_continuity = std::move(entries),
      .published_nodes = {},
    });
}

//! A continuing node has the ID of the anchor it continues, following chains
//! to the first; a node the importer adds follows its owner.
NOLINT_TEST(ModelNodeIdentityTest, ContinuityKeepsTheEarlierAnchorsId)
{
  const auto plain = ModelNodeIdentity(Identity());
  auto identity = WithContinuity({
    { .anchor = "Chassis/Door_FL", .continues = "Body/Door_Front_L" },
    { .anchor = "Body/Door_Front_L", .continues = "Body/Door_FL" },
  });
  auto diagnostics = std::vector<ImportDiagnostic> {};

  const auto anchors
    = std::vector<std::string> { "Chassis", "Chassis/Door_FL" };
  ASSERT_TRUE(identity.ResolveContinuity(anchors, "car.glb", diagnostics));

  EXPECT_TRUE(diagnostics.empty());
  EXPECT_EQ(identity.IdOf("Chassis/Door_FL"), plain.IdOf("Body/Door_FL"));
  EXPECT_EQ(
    identity.IdOf("Chassis/Door_FL#light"), plain.IdOf("Body/Door_FL#light"));
  EXPECT_EQ(identity.IdOf("Chassis"), plain.IdOf("Chassis"));
}

//! An entry that would give two nodes one ID is dropped with a warning: its
//! continued anchor is in the file again, or another entry continues it.
NOLINT_TEST(ModelNodeIdentityTest, ConflictingContinuityIsDropped)
{
  const auto plain = ModelNodeIdentity(Identity());
  auto identity = WithContinuity({
    { .anchor = "Door_New", .continues = "Door" },
    { .anchor = "Mirror_A", .continues = "Mirror" },
    { .anchor = "Mirror_B", .continues = "Mirror" },
  });
  auto diagnostics = std::vector<ImportDiagnostic> {};

  const auto anchors
    = std::vector<std::string> { "Door", "Door_New", "Mirror_A", "Mirror_B" };
  ASSERT_TRUE(identity.ResolveContinuity(anchors, "car.glb", diagnostics));

  EXPECT_THAT(Codes(diagnostics),
    testing::ElementsAre(
      "import.node_continuity_conflict", "import.node_continuity_conflict"));
  EXPECT_EQ(identity.IdOf("Door_New"), plain.IdOf("Door_New"));
  EXPECT_EQ(identity.IdOf("Mirror_A"), plain.IdOf("Mirror"));
  EXPECT_EQ(identity.IdOf("Mirror_B"), plain.IdOf("Mirror_B"));
}

//! Entries that continue each other in a circle are an error.
NOLINT_TEST(ModelNodeIdentityTest, AContinuityCycleIsAnError)
{
  auto identity = WithContinuity({
    { .anchor = "A", .continues = "B" },
    { .anchor = "B", .continues = "A" },
  });
  auto diagnostics = std::vector<ImportDiagnostic> {};

  EXPECT_FALSE(identity.ResolveContinuity(
    std::vector<std::string> { "A" }, "car.glb", diagnostics));

  ASSERT_EQ(diagnostics.size(), 1U);
  EXPECT_EQ(diagnostics.front().code, "import.node_continuity_cycle");
  EXPECT_EQ(diagnostics.front().severity,
    oxygen::content::import::ImportSeverity::kError);
}

//! The emitted nodes are published with their IDs; against the previous
//! publication, the vanished and new anchors are reported, and a node that
//! comes back has its old ID.
NOLINT_TEST(ModelNodeIdentityTest, PublishedNodesReportWhatChanged)
{
  auto first = ModelNodeIdentity(Identity());
  static_cast<void>(first.Emit("Body"));
  static_cast<void>(first.Emit("Body/Headlight_R"));
  auto diagnostics = std::vector<ImportDiagnostic> {};
  const auto published = first.TakePublishedNodes("car.glb", diagnostics);
  ASSERT_EQ(published.published.size(), 2U);
  EXPECT_EQ(published.published[1].anchor, "Body/Headlight_R");
  EXPECT_EQ(published.published[1].node_id, first.IdOf("Body/Headlight_R"));
  EXPECT_TRUE(diagnostics.empty()) << "a first import has nothing to compare";
  EXPECT_TRUE(published.added.empty());
  EXPECT_TRUE(published.vanished.empty());

  auto second = ModelNodeIdentity(Identity(),
    ModelImportInputs {
      .node_continuity = {},
      .published_nodes = published.published,
      .vanished_nodes = published.vanished,
    });
  static_cast<void>(second.Emit("Body"));
  static_cast<void>(second.Emit("Body/Spoiler"));
  const auto next = second.TakePublishedNodes("car.glb", diagnostics);

  ASSERT_EQ(diagnostics.size(), 1U);
  EXPECT_EQ(diagnostics.front().code, "import.nodes_changed");
  EXPECT_EQ(diagnostics.front().severity,
    oxygen::content::import::ImportSeverity::kInfo);
  EXPECT_THAT(diagnostics.front().message,
    testing::AllOf(testing::HasSubstr("Vanished: 'Body/Headlight_R'"),
      testing::HasSubstr("New: 'Body/Spoiler'")));
  EXPECT_EQ(
    next.published.front().node_id, published.published.front().node_id);
  ASSERT_EQ(next.added.size(), 1U);
  EXPECT_EQ(next.added.front().anchor, "Body/Spoiler");
  ASSERT_EQ(next.vanished.size(), 1U);
  EXPECT_EQ(next.vanished.front(), published.published[1]);

  auto third = ModelNodeIdentity(Identity(),
    ModelImportInputs {
      .node_continuity = {},
      .published_nodes = next.published,
      .vanished_nodes = next.vanished,
    });
  static_cast<void>(third.Emit("Body"));
  static_cast<void>(third.Emit("Body/Spoiler"));
  const auto unchanged = third.TakePublishedNodes("car.glb", diagnostics);
  EXPECT_TRUE(unchanged.added.empty()) << "new only against the previous";
  ASSERT_EQ(unchanged.vanished.size(), 1U)
    << "a vanished node stays listed until it comes back";
  EXPECT_EQ(unchanged.vanished.front(), published.published[1]);

  auto fourth = ModelNodeIdentity(Identity(),
    ModelImportInputs {
      .node_continuity = {},
      .published_nodes = unchanged.published,
      .vanished_nodes = unchanged.vanished,
    });
  static_cast<void>(fourth.Emit("Body"));
  static_cast<void>(fourth.Emit("Body/Spoiler"));
  EXPECT_EQ(fourth.Emit("Body/Headlight_R"), published.published[1].node_id)
    << "a node that comes back has its old ID";
  const auto returned = fourth.TakePublishedNodes("car.glb", diagnostics);
  EXPECT_TRUE(returned.vanished.empty());
}

//! Identical paths share one record; records follow the order paths are
//! first interned.
NOLINT_TEST(NodePathTableTest, InternsEachDistinctPathOnce)
{
  const auto lamp = NodeId::FromStableIdentity("/test/Lamp");
  const auto bulb = NodeId::FromStableIdentity("/test/Bulb");
  auto table = NodePathTable {};

  const auto lamp_path = table.Intern(std::array { lamp });
  const auto bulb_path = table.Intern(std::array { lamp, bulb });
  const auto again = table.Intern(std::array { lamp, bulb });

  EXPECT_EQ(lamp_path, 0U);
  EXPECT_EQ(bulb_path, 1U);
  EXPECT_EQ(again, bulb_path);
  ASSERT_EQ(table.Records().size(), 2U);
  EXPECT_EQ(table.Records()[1].first, 1U);
  EXPECT_EQ(table.Records()[1].count, 2U);
  EXPECT_EQ(table.Elements().size(), 3U);
}

} // namespace
