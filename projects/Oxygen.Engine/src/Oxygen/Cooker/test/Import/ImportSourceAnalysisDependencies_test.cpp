//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <string>
#include <utility>
#include <vector>

#include <Oxygen/Content/VirtualPath.h>
#include <Oxygen/Cooker/Import/ImportSourceAnalysis.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::content::VirtualPathError;
using oxygen::content::VirtualPathRuleSet;
using oxygen::content::import::ExternalBufferReferences;
using oxygen::content::import::ImportDependencyKind;
using oxygen::content::import::ImportDependencyText;
using oxygen::content::import::IsInputActionReference;
using oxygen::content::import::ResolveDependency;

constexpr auto kAnchor = "/Content/Scenes/Level.oscene";

auto Text(std::string written, const ImportDependencyKind kind,
  std::string json_pointer = {}) -> ImportDependencyText
{
  return {
    .written = std::move(written),
    .kind = kind,
    .json_pointer = std::move(json_pointer),
  };
}

//! A relative reference keeps its written text and resolves against the folder
//! of the referring file.
NOLINT_TEST(ImportSourceAnalysisDependenciesTest, RelativeTextResolves)
{
  const auto dependency = ResolveDependency(kAnchor,
    Text("../Materials/Wall.omat", ImportDependencyKind::kMaterial, "/a/b"),
    VirtualPathRuleSet::kAuthored);

  ASSERT_TRUE(dependency.has_value());
  EXPECT_EQ(dependency->written, "../Materials/Wall.omat");
  EXPECT_EQ(dependency->virtual_path, "/Content/Materials/Wall.omat");
  EXPECT_EQ(dependency->kind, ImportDependencyKind::kMaterial);
  EXPECT_EQ(dependency->json_pointer, "/a/b");
  EXPECT_TRUE(dependency->required);
}

//! An absolute reference in another mount is its own virtual path.
NOLINT_TEST(ImportSourceAnalysisDependenciesTest, AbsoluteTextInAnotherMount)
{
  const auto dependency = ResolveDependency(kAnchor,
    Text("/Engine/Materials/Default.omat", ImportDependencyKind::kMaterial),
    VirtualPathRuleSet::kAuthored);

  ASSERT_TRUE(dependency.has_value());
  EXPECT_EQ(dependency->written, "/Engine/Materials/Default.omat");
  EXPECT_EQ(dependency->virtual_path, "/Engine/Materials/Default.omat");
}

//! The grammar's refusals are returned, not thrown.
NOLINT_TEST(ImportSourceAnalysisDependenciesTest, RefusedTextIsAnError)
{
  EXPECT_EQ(
    ResolveDependency(kAnchor,
      Text("/Content/Materials/Wall.omat", ImportDependencyKind::kMaterial),
      VirtualPathRuleSet::kAuthored)
      .error(),
    VirtualPathError::kReferenceSelfAbsolute);
  EXPECT_EQ(ResolveDependency(kAnchor,
              Text("../../Wall.omat", ImportDependencyKind::kMaterial),
              VirtualPathRuleSet::kAuthored)
              .error(),
    VirtualPathError::kReferenceOutsideMount);
}

//! Text ending with `.oiact` names another source's action; a declared name
//! does not.
NOLINT_TEST(ImportSourceAnalysisDependenciesTest, InputActionRule)
{
  EXPECT_TRUE(IsInputActionReference("/Content/Input/Player.input/Jump.oiact"));
  EXPECT_TRUE(IsInputActionReference("Jump.oiact"));
  EXPECT_TRUE(IsInputActionReference("../Other.input/Jump.oiact"));
  EXPECT_FALSE(IsInputActionReference("Jump"));
  EXPECT_FALSE(IsInputActionReference("/Content/Input/Jump"));
  EXPECT_FALSE(IsInputActionReference(""));
}

//! A buffer the geometry declares is not a reference, however it is spelled:
//! the comparison is of resolved paths.
NOLINT_TEST(ImportSourceAnalysisDependenciesTest, LocalBuffersMatchResolved)
{
  const auto anchor = std::string("/Content/Geometry/Ship.ogeo");
  const auto local = std::vector<std::string> {
    "/Content/Geometry/Ship.vb.obuf",
    "/Content/Geometry/Ship.ib.obuf",
  };
  const auto references = std::vector<ImportDependencyText> {
    Text("./Ship.vb.obuf", ImportDependencyKind::kBuffer, "vb"),
    Text("../Geometry/Ship.ib.obuf", ImportDependencyKind::kBuffer, "ib"),
    Text("../Shared/Common.obuf", ImportDependencyKind::kBuffer, "/shared"),
  };

  const auto result = ExternalBufferReferences(anchor, local, references);

  EXPECT_TRUE(result.refused.empty());
  ASSERT_EQ(result.dependencies.size(), 1U);
  EXPECT_EQ(result.dependencies[0].written, "../Shared/Common.obuf");
  EXPECT_EQ(result.dependencies[0].virtual_path, "/Content/Shared/Common.obuf");
  EXPECT_EQ(result.dependencies[0].json_pointer, "/shared");
}

//! A buffer in another folder is external even when its file name matches a
//! local buffer's.
NOLINT_TEST(ImportSourceAnalysisDependenciesTest, SameNameElsewhereIsExternal)
{
  const auto local
    = std::vector<std::string> { "/Content/Geometry/Ship.vb.obuf" };
  const auto references = std::vector<ImportDependencyText> {
    Text("../Other/Ship.vb.obuf", ImportDependencyKind::kBuffer),
  };

  const auto result = ExternalBufferReferences(
    "/Content/Geometry/Ship.ogeo", local, references);

  ASSERT_EQ(result.dependencies.size(), 1U);
  EXPECT_EQ(result.dependencies[0].virtual_path, "/Content/Other/Ship.vb.obuf");
}

//! A buffer reference the grammar refuses is reported with its field.
NOLINT_TEST(ImportSourceAnalysisDependenciesTest, RefusedBufferIsReported)
{
  const auto references = std::vector<ImportDependencyText> {
    Text("../../Escape.obuf", ImportDependencyKind::kBuffer, "/lods/0"),
  };

  const auto result = ExternalBufferReferences(
    "/Content/Geometry/Ship.ogeo", std::vector<std::string> {}, references);

  EXPECT_TRUE(result.dependencies.empty());
  ASSERT_EQ(result.refused.size(), 1U);
  EXPECT_EQ(result.refused[0].written, "../../Escape.obuf");
  EXPECT_EQ(result.refused[0].json_pointer, "/lods/0");
  EXPECT_EQ(result.refused[0].error, VirtualPathError::kReferenceOutsideMount);
}

} // namespace
