//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <stdexcept>
#include <string>
#include <vector>

#include <Oxygen/Base/Uuid.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/Internal/ModelNodeIdentity.h>
#include <Oxygen/Cooker/Import/Internal/NodePathTable.h>
#include <Oxygen/Data/SceneIds.h>
#include <Oxygen/Testing/GTest.h>

namespace {
using oxygen::Uuid;
using oxygen::content::import::ImportDiagnostic;
using oxygen::content::import::ImportRequest;
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
//! are numbered from the second, and separators in a name are escaped.
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

  EXPECT_EQ(door, "Body/Door_FL");
  EXPECT_EQ(first, "Body/Mirror");
  EXPECT_EQ(second, "Body/Mirror[2]");
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

//! An import without a source identity still has node IDs, but they change
//! on every import, which it reports.
NOLINT_TEST(ModelNodeIdentityTest, ReportsAMissingSourceIdentity)
{
  auto diagnostics = std::vector<ImportDiagnostic> {};

  static_cast<void>(
    ModelNodeIdentity::ForRequest(ImportRequest {}, "model.glb", diagnostics));

  ASSERT_EQ(diagnostics.size(), 1U);
  EXPECT_EQ(diagnostics.front().code, "import.source_identity_missing");
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
