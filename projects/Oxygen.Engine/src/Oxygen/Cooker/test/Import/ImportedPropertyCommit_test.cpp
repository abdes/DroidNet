//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/ImportedPropertyCommit.h and the imported light color
// commit of Import/Internal/ImportedLightSemantics.h.

#include <array>
#include <limits>
#include <string_view>
#include <vector>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/ImportedLightSemantics.h>
#include <Oxygen/Cooker/Import/Internal/ImportedPropertyCommit.h>
#include <Oxygen/Data/LightValidation.h>
#include <Oxygen/Data/SceneRecordDefaults.h>
#include <Oxygen/Data/Schema/Generated.SceneComponents.h>
#include <Oxygen/Testing/GTest.h>

namespace {

namespace schema = oxygen::data::schema;
namespace world = oxygen::data::pak::world;
using oxygen::content::import::ImportDiagnostic;
using oxygen::content::import::ImportSeverity;
using oxygen::content::import::detail::CommitImportedLightColor;
using oxygen::content::import::detail::ImportedPropertyCommit;
using oxygen::content::import::detail::kImportedPropertyInvalidCode;
using ::testing::ElementsAre;
using ::testing::IsEmpty;

NOLINT_TEST(ImportedPropertyCommitTest, KeepsAValidValue)
{
  auto commit = ImportedPropertyCommit {};

  const auto near_plane
    = commit("near_plane", schema::perspective_camera::kNearPlaneRange, 0.5F,
      schema::perspective_camera::kNearPlaneDefault);

  EXPECT_FLOAT_EQ(near_plane, 0.5F);
  EXPECT_THAT(commit.Rejected(), IsEmpty());
}

NOLINT_TEST(ImportedPropertyCommitTest, ClampsAColorOutsideItsRangeToTheBound)
{
  auto commit = ImportedPropertyCommit {};
  auto common = world::MakeDefaultRecord<world::PointLightRecord>().common;

  CommitImportedLightColor(commit, oxygen::data::kPointLightCommonRanges,
    std::array { 2.0F, -1.0F, 0.5F }, common);

  EXPECT_FLOAT_EQ(common.color_rgb[0], 1.0F);
  EXPECT_FLOAT_EQ(common.color_rgb[1], 0.0F);
  EXPECT_FLOAT_EQ(common.color_rgb[2], 0.5F);
  EXPECT_THAT(commit.Rejected(), IsEmpty());
}

NOLINT_TEST(
  ImportedPropertyCommitTest, InvalidValueTakesTheSchemaDefaultAndWarns)
{
  namespace camera = schema::perspective_camera;
  auto commit = ImportedPropertyCommit {};

  // The near plane range does not clamp; a color never clamps a NaN.
  const auto near_plane = commit(
    "near_plane", camera::kNearPlaneRange, -1.0F, camera::kNearPlaneDefault);
  auto common = world::MakeDefaultRecord<world::SpotLightRecord>().common;
  CommitImportedLightColor(commit, oxygen::data::kSpotLightCommonRanges,
    std::array { std::numeric_limits<float>::quiet_NaN(), 0.25F, 0.25F },
    common);

  EXPECT_FLOAT_EQ(near_plane, camera::kNearPlaneDefault);
  EXPECT_FLOAT_EQ(common.color_rgb[0], schema::spot_light::kColorRDefault);
  EXPECT_THAT(commit.Rejected(),
    ElementsAre(
      std::string_view { "near_plane" }, std::string_view { "color_rgb[0]" }));

  auto diagnostics = std::vector<ImportDiagnostic> {};
  commit.Report("model.gltf", "Camera", diagnostics);
  ASSERT_EQ(diagnostics.size(), 2U);
  for (const auto& diagnostic : diagnostics) {
    EXPECT_EQ(diagnostic.severity, ImportSeverity::kWarning);
    EXPECT_EQ(diagnostic.code, kImportedPropertyInvalidCode);
    EXPECT_EQ(diagnostic.source_path, "model.gltf");
    EXPECT_EQ(diagnostic.object_path, "Camera");
  }
}

} // namespace
