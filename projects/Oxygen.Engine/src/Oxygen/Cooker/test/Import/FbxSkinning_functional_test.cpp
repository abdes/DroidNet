//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/fbx/FbxAdapter.cpp (skin weights)

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <ios>
#include <memory>
#include <utility>

#include <Oxygen/Content/LoaderContext.h>
#include <Oxygen/Content/Loaders/GeometryLoader.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/test/Support/Diagnostics.h>
#include <Oxygen/Cooker/test/Support/ModelImportTestBase.h>
#include <Oxygen/Cooker/test/Support/TestPaths.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Serio/FileStream.h>
#include <Oxygen/Serio/Reader.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::import::test {
namespace {

  using oxygen::cooker::test::DiagnosticSummary;
  using oxygen::cooker::test::HasDiagnosticCode;

  class FbxSkinningTest : public ModelImportTestBase { };

  //! A skin weighs control points while the import writes one vertex per face
  //! corner: every corner takes its control point's joints and weights, so the
  //! skinned mesh keeps them (Kenney's CC0 characterMedium.fbx).
  NOLINT_TEST_F(FbxSkinningTest, CornersTakeTheWeightsOfTheirControlPoints)
  {
    auto request = ImportRequest {};
    request.source_path
      = oxygen::cooker::test::ModelPath("skinned_character.fbx");
    request.asset_virtual_path
      = oxygen::cooker::test::ModelVirtualPath(request.source_path);
    request.cooked_root = MakeTempDir("fbx_skinning") / "cooked";

    const auto imported = RunImport(std::move(request));

    ASSERT_TRUE(imported.report.success)
      << DiagnosticSummary(imported.report.diagnostics);
    EXPECT_FALSE(
      HasDiagnosticCode(imported.report.diagnostics, "mesh.skinning.mismatch"))
      << DiagnosticSummary(imported.report.diagnostics);
    const auto inspection = LoadInspection(imported.report.cooked_root);
    auto skinned_lods = std::size_t { 0 };
    for (const auto& asset : inspection.Assets()) {
      if (asset.asset_type
        != static_cast<uint8_t>(data::AssetType::kGeometry)) {
        continue;
      }
      const auto path = imported.report.cooked_root / asset.descriptor_relpath;
      serio::FileStream<> stream(path, std::ios::in);
      serio::Reader reader(stream);
      const auto geometry = loaders::LoadGeometryAsset(LoaderContext {
        .current_asset_key = asset.key,
        .desc_reader = &reader,
        .work_offline = true,
        .parse_only = true,
      });
      ASSERT_NE(geometry, nullptr);
      for (const auto& mesh : geometry->Meshes()) {
        ASSERT_NE(mesh, nullptr);
        EXPECT_TRUE(mesh->IsSkinned());
        ++skinned_lods;
      }
    }
    EXPECT_GT(skinned_lods, 0U);
  }

} // namespace
} // namespace oxygen::content::import::test
