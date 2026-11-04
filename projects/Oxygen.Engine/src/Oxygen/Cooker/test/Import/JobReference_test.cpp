//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <Oxygen/Content/VirtualPath.h>
#include <Oxygen/Cooker/Import/ImportCodes.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/Internal/Utils/MountedCookedSources.h>
#include <Oxygen/Cooker/Import/Internal/Utils/VirtualPathResolution.h>
#include <Oxygen/Cooker/Import/PhysicsImportSettings.h>
#include <Oxygen/Cooker/Import/SourceMounts.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::content::VirtualPathError;
using oxygen::content::VirtualPathRuleSet;
using oxygen::content::import::ImportRequest;
using oxygen::content::import::Mount;
using oxygen::content::import::MountKind;
using oxygen::content::import::MountOrigin;
using oxygen::content::import::MountRole;
using oxygen::content::import::MountScope;
using oxygen::content::import::MountState;
using oxygen::content::import::MountTable;
using oxygen::content::import::PhysicsImportSettings;
using oxygen::content::import::import_codes::kReferenceInvalid;
using oxygen::content::import::import_codes::kReferenceUnmounted;
using oxygen::content::import::internal::AssetReferenceRefusal;
using oxygen::content::import::internal::AssetReferenceRefused;
using oxygen::content::import::internal::BuildMountedCookedRoots;
using oxygen::content::import::internal::DescribeLookup;
using oxygen::content::import::internal::RefuseReference;
using oxygen::content::import::internal::RefuseUnmountedReference;
using oxygen::content::import::internal::ResolvedReference;
using oxygen::content::import::internal::ResolveJobReference;

auto MaterialRequest() -> ImportRequest
{
  auto request = ImportRequest {};
  request.source_path = "Wall.omat.json";
  request.asset_virtual_path = "/Content/Materials/Wall.omat";
  return request;
}

//! A relative reference resolves against the folder of the job's identity and
//! keeps the text as written.
NOLINT_TEST(JobReferenceTest, RelativeReferenceResolvesAgainstTheJobIdentity)
{
  const auto request = MaterialRequest();

  const auto reference = ResolveJobReference(request, "../Textures/Brick.otex");

  ASSERT_TRUE(reference.has_value());
  EXPECT_EQ(reference->written, "../Textures/Brick.otex");
  EXPECT_EQ(reference->resolved, "/Content/Textures/Brick.otex");
}

//! A sidecar has no identity of its own in the request: it anchors at its
//! target scene, which lives in the sidecar's folder.
NOLINT_TEST(JobReferenceTest, SidecarAnchorsAtItsTargetScene)
{
  auto request = ImportRequest {};
  request.physics = PhysicsImportSettings {
    .target_scene_virtual_path = "/Content/Scenes/Level.oscene",
    .inline_bindings_json = {},
  };

  const auto reference = ResolveJobReference(request, "Shapes/Floor.ocshape");

  ASSERT_TRUE(reference.has_value());
  EXPECT_EQ(reference->resolved, "/Content/Scenes/Shapes/Floor.ocshape");
}

//! The grammar's refusals reach the job unchanged.
NOLINT_TEST(JobReferenceTest, RefusalIsTheGrammarCode)
{
  const auto request = MaterialRequest();

  EXPECT_EQ(ResolveJobReference(request, "../../Brick.otex").error(),
    VirtualPathError::kReferenceOutsideMount);
  EXPECT_EQ(
    ResolveJobReference(request, "/Content/Textures/Brick.otex").error(),
    VirtualPathError::kReferenceSelfAbsolute);
}

//! An asset reference refuses derived content; a file reference accepts it.
NOLINT_TEST(JobReferenceTest, FileReferenceMayNameDerivedContent)
{
  const auto request = MaterialRequest();

  EXPECT_EQ(ResolveJobReference(request, "../SourceMedia/Brick.png").error(),
    VirtualPathError::kDerivedContent);
  EXPECT_EQ(ResolveJobReference(
              request, "../SourceMedia/Brick.png", VirtualPathRuleSet::kCooked)
              ->resolved,
    "/Content/SourceMedia/Brick.png");
}

//! An invalid reference is reported with the shared code, at the field, and
//! names the grammar's code.
NOLINT_TEST(JobReferenceTest, InvalidReferenceDiagnosticNamesTheField)
{
  const auto request = MaterialRequest();

  const auto diagnostic = RefuseReference(request, "../../Brick.otex",
    VirtualPathError::kReferenceOutsideMount, "/textures/base_color");

  EXPECT_EQ(diagnostic.code, kReferenceInvalid);
  EXPECT_EQ(diagnostic.json_pointer, "/textures/base_color");
  EXPECT_TRUE(diagnostic.object_path.empty());
  EXPECT_NE(diagnostic.message.find(
              oxygen::content::vpath_codes::kReferenceOutsideMount),
    std::string::npos);
}

//! An unmounted reference names both of its forms.
NOLINT_TEST(JobReferenceTest, UnmountedReferenceDiagnosticNamesBothForms)
{
  const auto request = MaterialRequest();
  const auto reference = ResolvedReference {
    .written = "/Props/Crate.omat",
    .resolved = "/Props/Crate.omat",
  };

  const auto diagnostic
    = RefuseUnmountedReference(request, reference, "/material_ref");

  EXPECT_EQ(diagnostic.code, kReferenceUnmounted);
  EXPECT_NE(diagnostic.message.find("/Props/Crate.omat"), std::string::npos);
}

//! A refused lookup says what it resolved to and each file it looked for.
NOLINT_TEST(JobReferenceTest, LookupDescriptionNamesTheFilesLookedFor)
{
  const auto reference = ResolvedReference {
    .written = "../Textures/Brick.otex",
    .resolved = "/Content/Textures/Brick.otex",
  };
  const auto refused = AssetReferenceRefused {
    .reason = AssetReferenceRefusal::kMissing,
    .found_type = std::nullopt,
    .looked_up = {
      std::filesystem::path { "C:/Cooked/Content/Textures/Brick.otex" },
      std::filesystem::path { "C:/Base/Content/Textures/Brick.otex" },
    },
  };

  EXPECT_EQ(DescribeLookup(reference, refused),
    "'../Textures/Brick.otex' resolves to /Content/Textures/Brick.otex; looked "
    "for C:/Cooked/Content/Textures/Brick.otex, "
    "C:/Base/Content/Textures/Brick.otex");
}

//! A job reads its output root, then the bound libraries of its source's
//! mount table in table order, then its cooked context roots; a `Source`
//! mount and an `Invalid` or missing library are never read.
NOLINT_TEST(JobReferenceTest, JobReadsTheBoundLibrariesOfItsMountTable)
{
  const auto mount = [](const std::string& name, const MountKind kind,
                       const MountState state) -> Mount {
    return Mount {
      .name = name,
      .kind = kind,
      .scope = MountScope::kDeclared,
      .declared_path = std::nullopt,
      .folder = std::filesystem::path { "C:/Libs" } / name,
      .origin = MountOrigin::kCommitted,
      .origin_file = {},
      .state = state,
      .role = MountRole::kShared,
      .findings = {},
    };
  };
  auto table = std::make_shared<MountTable>();
  table->mounts = {
    mount("Content", MountKind::kSource, MountState::kBound),
    mount("Vehicles", MountKind::kCooked, MountState::kBound),
    mount("Broken", MountKind::kCooked, MountState::kInvalid),
    mount("Gone", MountKind::kCooked, MountState::kMissing),
    mount("Props", MountKind::kCooked, MountState::kBound),
  };
  auto request = MaterialRequest();
  request.cooked_root = std::filesystem::path { "C:/Cooked" };
  request.cooked_context_roots = { std::filesystem::path { "C:/Context" } };
  request.source_mounts = table;

  EXPECT_EQ(BuildMountedCookedRoots(request),
    (std::vector<std::filesystem::path> {
      "C:/Cooked",
      "C:/Libs/Vehicles",
      "C:/Libs/Props",
      "C:/Context",
    }));
}

} // namespace
