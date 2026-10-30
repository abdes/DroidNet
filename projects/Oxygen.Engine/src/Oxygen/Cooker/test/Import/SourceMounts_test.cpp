//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <ios>
#include <optional>
#include <ranges>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/SourceMounts.h>
#include <Oxygen/Cooker/test/Support/Diagnostics.h>
#include <Oxygen/Cooker/test/Support/TempDir.h>
#include <Oxygen/Testing/GTest.h>

namespace {

namespace fs = std::filesystem;
namespace codes = oxygen::content::import::mount_codes;
using nlohmann::json;
using oxygen::content::import::CheckMountNameConflicts;
using oxygen::content::import::CommittedPath;
using oxygen::content::import::IsValidMountName;
using oxygen::content::import::kMountsFileName;
using oxygen::content::import::MountDeclaration;
using oxygen::content::import::MountDeclarationForm;
using oxygen::content::import::MountKind;
using oxygen::content::import::MountOrigin;
using oxygen::content::import::MountScope;
using oxygen::content::import::MountState;
using oxygen::content::import::MountTable;
using oxygen::content::import::MountTableOwner;
using oxygen::content::import::MountTableRequest;
using oxygen::content::import::ResolveMountTable;
using oxygen::cooker::test::DiagnosticSummary;
using oxygen::cooker::test::HasDiagnosticCode;
using oxygen::cooker::test::TempDirTest;

//! Projects live at `<temp>/ws/proj`. A `.mounts.json` with `"root": true` at
//! the temp folder fences every walk off from the machine's own files.
class SourceMountsTest : public TempDirTest {
protected:
  auto SetUp() -> void override
  {
    WriteMounts(TempDir(), json { { "version", 1 }, { "root", true } });
    fs::create_directories(Project() / "Content");
  }

  [[nodiscard]] auto Workspace() const -> fs::path { return TempDir() / "ws"; }

  [[nodiscard]] auto Project() const -> fs::path
  {
    return Workspace() / "proj";
  }

  static auto WriteText(const fs::path& path, std::string_view text) -> void
  {
    fs::create_directories(path.parent_path());
    auto stream = std::ofstream { path, std::ios::binary };
    stream << text;
  }

  static auto WriteMounts(const fs::path& dir, const json& doc) -> fs::path
  {
    const auto path = dir / kMountsFileName;
    WriteText(path, doc.dump());
    return path;
  }

  static auto MountsDoc(const json& mounts) -> json
  {
    return json { { "version", 1 }, { "mounts", mounts } };
  }

  [[nodiscard]] auto Request(std::vector<MountDeclaration> declarations) const
    -> MountTableRequest
  {
    return MountTableRequest {
      .project_root = Project(),
      .declarations = std::move(declarations),
      .form = MountDeclarationForm::kCommittedFile,
      .declared_in = "manifest.json",
      .working_directory = Workspace(),
      .mounts_option = std::nullopt,
      .owner = MountTableOwner::kProject,
    };
  }

  static auto Declared(std::string name, std::string path) -> MountDeclaration
  {
    return MountDeclaration {
      .name = std::move(name),
      .kind = MountKind::kSource,
      .path = std::move(path),
    };
  }

  static auto Placeholder(std::string name) -> MountDeclaration
  {
    return MountDeclaration {
      .name = std::move(name),
      .kind = MountKind::kSource,
      .path = std::nullopt,
    };
  }

  static auto Canonical(const fs::path& path) -> fs::path
  {
    return fs::weakly_canonical(path);
  }
};

//! Expects one mount bound to `folder` by `origin`.
auto ExpectBound(const MountTable& table, std::string_view name,
  const fs::path& folder, MountOrigin origin) -> void
{
  const auto mount = table.Find(name);
  ASSERT_TRUE(mount) << name << "\n" << DiagnosticSummary(table.diagnostics);
  EXPECT_EQ(mount->state, MountState::kBound);
  EXPECT_EQ(mount->folder, fs::weakly_canonical(folder));
  EXPECT_EQ(mount->origin, origin);
}

// -- Names -------------------------------------------------------------------

//! Names follow `^[A-Za-z][A-Za-z0-9_-]{0,63}$`.
NOLINT_TEST(SourceMountNameTest, FollowsTheNameRule)
{
  EXPECT_TRUE(IsValidMountName("Content"));
  EXPECT_TRUE(IsValidMountName("a_b-9"));
  EXPECT_TRUE(IsValidMountName(std::string(64, 'a')));
  EXPECT_FALSE(IsValidMountName(""));
  EXPECT_FALSE(IsValidMountName("9Content"));
  EXPECT_FALSE(IsValidMountName("_Content"));
  EXPECT_FALSE(IsValidMountName("Con tent"));
  EXPECT_FALSE(IsValidMountName("Con.tent"));
  EXPECT_FALSE(IsValidMountName(std::string(65, 'a')));
}

//! An invalid committed name is `mount.name_invalid`.
NOLINT_TEST_F(SourceMountsTest, InvalidCommittedNameIsRefused)
{
  const auto table
    = ResolveMountTable(Request({ Declared("1Bad", "Content") }));

  EXPECT_FALSE(table.IsValid());
  EXPECT_TRUE(HasDiagnosticCode(table.diagnostics, codes::kNameInvalid.id));
  EXPECT_FALSE(table.Find("1Bad"));
}

//! An invalid name in `.mounts.json` is `mount.name_invalid`.
NOLINT_TEST_F(SourceMountsTest, InvalidFileNameIsRefused)
{
  WriteMounts(Project(), MountsDoc({ { "Bad Name", "Content" } }));

  const auto table = ResolveMountTable(Request({}));

  EXPECT_FALSE(table.IsValid());
  EXPECT_TRUE(HasDiagnosticCode(table.diagnostics, codes::kNameInvalid.id));
}

//! A project mount named `Engine` is refused; the engine content build
//! declares it.
NOLINT_TEST_F(SourceMountsTest, EngineIsReservedForEngineContent)
{
  auto request = Request({ Declared("Engine", "Content") });

  const auto project = ResolveMountTable(request);
  EXPECT_TRUE(HasDiagnosticCode(project.diagnostics, codes::kNameInvalid.id));

  request.owner = MountTableOwner::kEngineContent;
  const auto engine = ResolveMountTable(request);
  EXPECT_TRUE(engine.IsValid()) << DiagnosticSummary(engine.diagnostics);
  ExpectBound(engine, "Engine", Project() / "Content", MountOrigin::kCommitted);
}

//! A project's `.mounts.json` cannot add `Engine` either.
NOLINT_TEST_F(SourceMountsTest, EngineIsReservedInMountsFiles)
{
  WriteMounts(Project(), MountsDoc({ { "Engine", "Content" } }));

  const auto table = ResolveMountTable(Request({}));

  EXPECT_TRUE(HasDiagnosticCode(table.diagnostics, codes::kNameInvalid.id));
  EXPECT_FALSE(table.Find("Engine"));
}

//! Names are unique ignoring case.
NOLINT_TEST_F(SourceMountsTest, DuplicateNamesIgnoringCaseAreRefused)
{
  const auto table = ResolveMountTable(
    Request({ Declared("Content", "Content"), Declared("content", "Other") }));

  EXPECT_FALSE(table.IsValid());
  EXPECT_TRUE(HasDiagnosticCode(table.diagnostics, codes::kNameDuplicate.id));
}

//! A `.mounts.json` name that differs from a declared one only by case is a
//! duplicate, not a binding.
NOLINT_TEST_F(SourceMountsTest, FileNameDifferingByCaseIsADuplicate)
{
  fs::create_directories(Workspace() / "Alt");
  WriteMounts(Project(), MountsDoc({ { "content", "../Alt" } }));

  const auto table
    = ResolveMountTable(Request({ Declared("Content", "Content") }));

  EXPECT_TRUE(HasDiagnosticCode(table.diagnostics, codes::kNameDuplicate.id));
  ExpectBound(table, "Content", Project() / "Content", MountOrigin::kCommitted);
}

//! Two keys of one `.mounts.json` that differ only by case are duplicates.
NOLINT_TEST_F(SourceMountsTest, FileKeysDifferingByCaseAreDuplicates)
{
  WriteMounts(Project(), MountsDoc({ { "Studio", "a" }, { "studio", "b" } }));

  const auto table = ResolveMountTable(Request({}));

  EXPECT_TRUE(HasDiagnosticCode(table.diagnostics, codes::kNameDuplicate.id));
}

// -- Paths -------------------------------------------------------------------

//! A committed file holds no absolute path: `path.absolute_committed` names
//! the file and the field.
NOLINT_TEST_F(SourceMountsTest, AbsoluteCommittedPathIsRefused)
{
  const auto absolute = (Project() / "Content").generic_string();

  const auto table
    = ResolveMountTable(Request({ Declared("Content", absolute) }));

  EXPECT_FALSE(table.IsValid());
  const auto it
    = std::ranges::find(table.diagnostics, codes::kAbsoluteCommitted.id,
      &oxygen::content::import::ImportDiagnostic::code);
  ASSERT_NE(it, table.diagnostics.end());
  EXPECT_EQ(it->source_path, "manifest.json");
  EXPECT_EQ(it->object_path, "mounts[0].path");
  // The refused path is reported once; the mount is not a placeholder.
  EXPECT_FALSE(HasDiagnosticCode(table.diagnostics, codes::kUnbound.id));
}

//! `--mount` options are command lines: absolute paths are allowed, and
//! relative ones resolve against the working directory.
NOLINT_TEST_F(SourceMountsTest, CommandLineMountsAcceptAbsoluteAndRelative)
{
  fs::create_directories(Workspace() / "Rel");
  auto request = Request({
    Declared("Content", (Project() / "Content").generic_string()),
    Declared("Rel", "Rel"),
  });
  request.form = MountDeclarationForm::kCommandLine;
  request.declared_in = "--mount";

  const auto table = ResolveMountTable(request);

  EXPECT_TRUE(table.IsValid()) << DiagnosticSummary(table.diagnostics);
  ExpectBound(table, "Content", Project() / "Content", MountOrigin::kCommitted);
  ExpectBound(table, "Rel", Workspace() / "Rel", MountOrigin::kCommitted);
}

//! A committed path may leave the project root.
NOLINT_TEST_F(SourceMountsTest, CommittedPathMayLeaveTheProjectRoot)
{
  fs::create_directories(Workspace() / "Media" / "Showcase");

  const auto table
    = ResolveMountTable(Request({ Declared("Showcase", "../Media/Showcase") }));

  EXPECT_TRUE(table.IsValid()) << DiagnosticSummary(table.diagnostics);
  ExpectBound(table, "Showcase", Workspace() / "Media" / "Showcase",
    MountOrigin::kCommitted);
}

// -- Overlap and output folders ----------------------------------------------

//! Two Source folders may not contain one another.
NOLINT_TEST_F(SourceMountsTest, NestedFoldersOverlap)
{
  fs::create_directories(Project() / "Content" / "Free");

  const auto table = ResolveMountTable(Request({
    Declared("Studio", "Content"),
    Declared("StudioFree", "Content/Free"),
  }));

  EXPECT_FALSE(table.IsValid());
  EXPECT_TRUE(HasDiagnosticCode(table.diagnostics, codes::kFolderOverlap.id));
}

//! Two spellings of one folder are one folder: the check runs on canonical
//! folders.
NOLINT_TEST_F(SourceMountsTest, TwoSpellingsOfOneFolderOverlap)
{
  const auto table = ResolveMountTable(Request({
    Declared("A", "Content"),
    Declared("B", "./Content/../Content/"),
  }));

  EXPECT_TRUE(HasDiagnosticCode(table.diagnostics, codes::kFolderOverlap.id));
}

//! A local mount added by `.mounts.json` is checked like a declared one.
NOLINT_TEST_F(SourceMountsTest, LocalMountOverlapIsRefused)
{
  WriteMounts(Workspace(), MountsDoc({ { "Inner", "proj/Content" } }));

  const auto table
    = ResolveMountTable(Request({ Declared("Content", "Content") }));

  EXPECT_TRUE(HasDiagnosticCode(table.diagnostics, codes::kFolderOverlap.id));
}

//! Sibling folders do not overlap, even when one name prefixes the other.
NOLINT_TEST_F(SourceMountsTest, SiblingFoldersDoNotOverlap)
{
  fs::create_directories(Project() / "ContentExtra");

  const auto table = ResolveMountTable(Request({
    Declared("Content", "Content"),
    Declared("Extra", "ContentExtra"),
  }));

  EXPECT_TRUE(table.IsValid()) << DiagnosticSummary(table.diagnostics);
}

//! A Source folder may not be or lie in a dot-named folder below the project
//! root.
NOLINT_TEST_F(SourceMountsTest, FolderInOutputIsRefused)
{
  fs::create_directories(Project() / ".cooked" / "Imports");
  fs::create_directories(Project() / ".anything");

  const auto table = ResolveMountTable(Request({
    Declared("Cache", ".cooked/Imports"),
    Declared("Any", ".anything"),
  }));

  EXPECT_FALSE(table.IsValid());
  EXPECT_EQ(std::ranges::count(table.diagnostics, codes::kFolderInOutput.id,
              &oxygen::content::import::ImportDiagnostic::code),
    2);
}

//! Dot-named folders outside the project root are not output folders.
NOLINT_TEST_F(SourceMountsTest, DotFolderOutsideTheProjectIsAllowed)
{
  fs::create_directories(Workspace() / ".studio");

  const auto table
    = ResolveMountTable(Request({ Declared("Studio", "../.studio") }));

  EXPECT_TRUE(table.IsValid()) << DiagnosticSummary(table.diagnostics);
}

//! Cooked roots and paks live in output folders by design.
NOLINT_TEST_F(SourceMountsTest, CookedAndPakMountsMayLiveInOutput)
{
  fs::create_directories(Project() / ".cooked" / "Content");
  WriteText(Project() / ".build" / "content.pak", "pak");

  const auto table = ResolveMountTable(Request({
    MountDeclaration {
      .name = "Cooked",
      .kind = MountKind::kCooked,
      .path = ".cooked/Content",
    },
    MountDeclaration {
      .name = "Pak",
      .kind = MountKind::kPak,
      .path = ".build/content.pak",
    },
  }));

  EXPECT_TRUE(table.IsValid()) << DiagnosticSummary(table.diagnostics);
}

// -- Path kind, missing and unbound ------------------------------------------

//! A `Pak` mount names a file; the other kinds name folders.
NOLINT_TEST_F(SourceMountsTest, PathKindMismatchIsRefused)
{
  WriteText(Project() / "file.txt", "x");

  const auto table = ResolveMountTable(Request({
    MountDeclaration {
      .name = "Pak",
      .kind = MountKind::kPak,
      .path = "Content",
    },
    Declared("Source", "file.txt"),
  }));

  EXPECT_EQ(std::ranges::count(table.diagnostics, codes::kPathKind.id,
              &oxygen::content::import::ImportDiagnostic::code),
    2);
}

//! A folder that does not exist warns and is `Missing`.
NOLINT_TEST_F(SourceMountsTest, MissingFolderWarns)
{
  const auto table
    = ResolveMountTable(Request({ Declared("Gone", "Gone/Deeper") }));

  EXPECT_TRUE(table.IsValid()) << DiagnosticSummary(table.diagnostics);
  EXPECT_TRUE(HasDiagnosticCode(table.diagnostics, codes::kFolderMissing.id));
  const auto mount = table.Find("Gone");
  ASSERT_TRUE(mount);
  EXPECT_EQ(mount->state, MountState::kMissing);
  EXPECT_EQ(mount->folder, Canonical(Project() / "Gone" / "Deeper"));
}

//! A placeholder nothing binds warns and is `Unbound`.
NOLINT_TEST_F(SourceMountsTest, UnboundPlaceholderWarns)
{
  const auto table = ResolveMountTable(Request({ Placeholder("Studio") }));

  EXPECT_TRUE(table.IsValid()) << DiagnosticSummary(table.diagnostics);
  EXPECT_TRUE(HasDiagnosticCode(table.diagnostics, codes::kUnbound.id));
  const auto mount = table.Find("Studio");
  ASSERT_TRUE(mount);
  EXPECT_EQ(mount->state, MountState::kUnbound);
  EXPECT_EQ(mount->origin, MountOrigin::kNone);
  EXPECT_TRUE(mount->folder.empty());
}

// -- The mounts file ---------------------------------------------------------

//! A version other than 1 is `mount.file_version`; the file binds nothing.
NOLINT_TEST_F(SourceMountsTest, OtherFileVersionIsRefused)
{
  WriteMounts(Project(),
    json { { "version", 2 }, { "mounts", { { "Studio", "Content" } } } });

  const auto table = ResolveMountTable(Request({ Placeholder("Studio") }));

  EXPECT_TRUE(HasDiagnosticCode(table.diagnostics, codes::kFileVersion.id));
  EXPECT_TRUE(HasDiagnosticCode(table.diagnostics, codes::kUnbound.id));
}

//! `.mounts.json` holds only mounts: another key, a kind (kind is fixed by
//! the committed declaration) or malformed JSON is `mount.file_invalid`.
NOLINT_TEST_F(SourceMountsTest, InvalidFileIsRefused)
{
  const auto cases = std::array {
    json { { "version", 1 }, { "cooked_root", "x" } }.dump(),
    MountsDoc({ { "Studio", { { "kind", "Pak" }, { "path", "a.pak" } } } })
      .dump(),
    json { { "mounts", json::object() } }.dump(),
    std::string { "{ not json" },
  };
  for (const auto& text : cases) {
    WriteText(Project() / kMountsFileName, text);

    const auto table = ResolveMountTable(Request({}));

    EXPECT_FALSE(table.IsValid()) << text;
    EXPECT_TRUE(HasDiagnosticCode(table.diagnostics, codes::kFileInvalid.id))
      << text << "\n"
      << DiagnosticSummary(table.diagnostics);
  }
}

//! A `--mounts` file that does not exist is `mount.file_invalid`.
NOLINT_TEST_F(SourceMountsTest, MissingMountsOptionFileIsRefused)
{
  auto request = Request({});
  request.mounts_option = Workspace() / "absent.json";

  const auto table = ResolveMountTable(request);

  EXPECT_TRUE(HasDiagnosticCode(table.diagnostics, codes::kFileInvalid.id));
}

//! `mounts` is optional: a file may only stop the walk.
NOLINT_TEST_F(SourceMountsTest, FileMayOmitMounts)
{
  WriteMounts(Project(), json { { "version", 1 }, { "root", true } });

  const auto table
    = ResolveMountTable(Request({ Declared("Content", "Content") }));

  EXPECT_TRUE(table.IsValid()) << DiagnosticSummary(table.diagnostics);
}

// -- Precedence --------------------------------------------------------------

//! The project's own `.mounts.json` overrides a committed path, relative to
//! its own folder.
NOLINT_TEST_F(SourceMountsTest, ProjectFileOverridesCommittedPath)
{
  fs::create_directories(Workspace() / "Alt");
  const auto file
    = WriteMounts(Project(), MountsDoc({ { "Content", "../Alt" } }));

  const auto table
    = ResolveMountTable(Request({ Declared("Content", "Content") }));

  EXPECT_TRUE(table.IsValid()) << DiagnosticSummary(table.diagnostics);
  ExpectBound(table, "Content", Workspace() / "Alt", MountOrigin::kMountsFile);
  const auto mount = table.Find("Content");
  ASSERT_TRUE(mount);
  EXPECT_EQ(Canonical(mount->origin_file), Canonical(file));
  EXPECT_EQ(mount->scope, MountScope::kDeclared);
}

//! The `--mounts` file overrides the project's own file.
NOLINT_TEST_F(SourceMountsTest, MountsOptionOverridesProjectFile)
{
  fs::create_directories(Workspace() / "Alt");
  fs::create_directories(Workspace() / "Opt");
  WriteMounts(Project(), MountsDoc({ { "Content", "../Alt" } }));
  const auto option = Workspace() / "run" / "mounts.json";
  WriteText(option, MountsDoc({ { "Content", "../Opt" } }).dump());
  auto request = Request({ Declared("Content", "Content") });
  request.mounts_option = option;

  const auto table = ResolveMountTable(request);

  EXPECT_TRUE(table.IsValid()) << DiagnosticSummary(table.diagnostics);
  ExpectBound(
    table, "Content", Workspace() / "Opt", MountOrigin::kMountsOption);
}

//! A `--mounts` file with `"root": true` is the only mounts file read.
NOLINT_TEST_F(SourceMountsTest, RootMountsOptionIsTheOnlyFileRead)
{
  WriteMounts(Project(), MountsDoc({ { "Studio", "Content" } }));
  const auto option = Workspace() / "run" / "mounts.json";
  WriteText(option, json { { "version", 1 }, { "root", true } }.dump());
  auto request = Request({ Placeholder("Studio") });
  request.mounts_option = option;

  const auto table = ResolveMountTable(request);

  EXPECT_TRUE(HasDiagnosticCode(table.diagnostics, codes::kUnbound.id));
}

//! A parent file binds a placeholder and adds a local Source mount.
NOLINT_TEST_F(SourceMountsTest, ParentFileBindsPlaceholdersAndAddsLocals)
{
  fs::create_directories(Workspace() / "Studio");
  fs::create_directories(Workspace() / "Local");
  WriteMounts(
    Workspace(), MountsDoc({ { "Studio", "Studio" }, { "Extra", "Local" } }));

  const auto table = ResolveMountTable(
    Request({ Declared("Content", "Content"), Placeholder("Studio") }));

  EXPECT_TRUE(table.IsValid()) << DiagnosticSummary(table.diagnostics);
  ExpectBound(
    table, "Studio", Workspace() / "Studio", MountOrigin::kMountsFile);
  ExpectBound(table, "Extra", Workspace() / "Local", MountOrigin::kMountsFile);
  const auto extra = table.Find("Extra");
  ASSERT_TRUE(extra);
  EXPECT_EQ(extra->scope, MountScope::kLocal);
  EXPECT_EQ(extra->kind, MountKind::kSource);
}

//! A parent file cannot retarget a committed path: `mount.parent_ignored`
//! names the file and the committed path is used.
NOLINT_TEST_F(SourceMountsTest, ParentFileCannotRetargetCommittedPath)
{
  fs::create_directories(Workspace() / "Elsewhere");
  const auto parent
    = WriteMounts(Workspace(), MountsDoc({ { "Content", "Elsewhere" } }));

  const auto table
    = ResolveMountTable(Request({ Declared("Content", "Content") }));

  EXPECT_TRUE(table.IsValid()) << DiagnosticSummary(table.diagnostics);
  const auto it = std::ranges::find(table.diagnostics, codes::kParentIgnored.id,
    &oxygen::content::import::ImportDiagnostic::code);
  ASSERT_NE(it, table.diagnostics.end());
  EXPECT_EQ(Canonical(it->source_path), Canonical(parent));
  ExpectBound(table, "Content", Project() / "Content", MountOrigin::kCommitted);
}

//! The project's own file binds a placeholder before any parent file.
NOLINT_TEST_F(SourceMountsTest, ProjectFileBeatsParentFile)
{
  fs::create_directories(Workspace() / "Parent");
  fs::create_directories(Workspace() / "Own");
  WriteMounts(Workspace(), MountsDoc({ { "Studio", "Parent" } }));
  WriteMounts(Project(), MountsDoc({ { "Studio", "../Own" } }));

  const auto table = ResolveMountTable(Request({ Placeholder("Studio") }));

  ExpectBound(table, "Studio", Workspace() / "Own", MountOrigin::kMountsFile);
}

//! The nearer parent wins; `"root": true` stops the walk above it.
NOLINT_TEST_F(SourceMountsTest, NearestParentWinsAndRootStopsTheWalk)
{
  const auto nested = Workspace() / "group" / "proj";
  fs::create_directories(nested / "Content");
  fs::create_directories(Workspace() / "Near");
  fs::create_directories(Workspace() / "Far");
  WriteMounts(Workspace() / "group",
    json {
      { "version", 1 },
      { "root", true },
      { "mounts", { { "Studio", "../Near" } } },
    });
  WriteMounts(
    Workspace(), MountsDoc({ { "Studio", "Far" }, { "Beyond", "Far" } }));
  auto request = Request({ Placeholder("Studio") });
  request.project_root = nested;

  const auto table = ResolveMountTable(request);

  ExpectBound(table, "Studio", Workspace() / "Near", MountOrigin::kMountsFile);
  EXPECT_FALSE(table.Find("Beyond"));
}

//! A relative project root resolves against the working directory.
NOLINT_TEST_F(SourceMountsTest, RelativeProjectRootUsesWorkingDirectory)
{
  auto request = Request({ Declared("Content", "Content") });
  request.project_root = "proj";

  const auto table = ResolveMountTable(request);

  EXPECT_TRUE(table.IsValid()) << DiagnosticSummary(table.diagnostics);
  EXPECT_EQ(table.project_root, Canonical(Project()));
  ExpectBound(table, "Content", Project() / "Content", MountOrigin::kCommitted);
}

// -- References --------------------------------------------------------------

//! A mount path names a file of the mount, whatever the referrer's folder.
NOLINT_TEST_F(SourceMountsTest, LocateReferenceResolvesAMountPath)
{
  fs::create_directories(Project() / "Content" / "Images");
  const auto table
    = ResolveMountTable(Request({ Declared("Content", "Content") }));
  auto findings = std::vector<oxygen::content::import::ImportDiagnostic> {};

  const auto located = table.LocateReference(
    Project() / "Content" / "Materials" / "wood.material.json",
    CommittedPath { .text = "/Content/Images/wood.png", .field = "source" },
    findings);

  ASSERT_HAS_VALUE(located) << DiagnosticSummary(findings);
  EXPECT_TRUE(findings.empty());
  EXPECT_EQ(located->file, Canonical(Project() / "Content/Images/wood.png"));
  EXPECT_EQ(located->mount_path, "/Content/Images/wood.png");
}

//! A relative path resolves in the referrer's folder when it stays in the
//! referrer's mount.
NOLINT_TEST_F(SourceMountsTest, LocateReferenceResolvesARelativePathInTheMount)
{
  const auto table
    = ResolveMountTable(Request({ Declared("Content", "Content") }));
  auto findings = std::vector<oxygen::content::import::ImportDiagnostic> {};

  const auto located = table.LocateReference(
    Project() / "Content" / "Materials" / "wood.material.json",
    CommittedPath { .text = "../Images/wood.png", .field = "source" },
    findings);

  ASSERT_HAS_VALUE(located) << DiagnosticSummary(findings);
  EXPECT_TRUE(findings.empty());
  EXPECT_EQ(located->file, Canonical(Project() / "Content/Images/wood.png"));
  EXPECT_EQ(located->mount_path, "/Content/Images/wood.png");
}

//! A relative path that lands in another mount is refused with
//! `import.reference_outside_mount`; a mount path may name that mount.
NOLINT_TEST_F(
  SourceMountsTest, LocateReferenceRefusesARelativePathLeavingTheMount)
{
  fs::create_directories(Project() / "Other");
  const auto table = ResolveMountTable(
    Request({ Declared("Content", "Content"), Declared("Other", "Other") }));
  ASSERT_TRUE(table.IsValid()) << DiagnosticSummary(table.diagnostics);
  auto findings = std::vector<oxygen::content::import::ImportDiagnostic> {};
  const auto referrer = Project() / "Content" / "wood.material.json";

  const auto refused = table.LocateReference(referrer,
    CommittedPath { .text = "../Other/wood.png", .field = "source" }, findings);

  EXPECT_FALSE(refused.has_value());
  EXPECT_TRUE(HasDiagnosticCode(findings, codes::kReferenceOutsideMount.id))
    << DiagnosticSummary(findings);

  findings.clear();
  const auto named = table.LocateReference(referrer,
    CommittedPath { .text = "/Other/wood.png", .field = "source" }, findings);
  EXPECT_TRUE(named.has_value()) << DiagnosticSummary(findings);
}

//! A reference holds no absolute path: `path.absolute_committed`.
NOLINT_TEST_F(SourceMountsTest, LocateReferenceRefusesAnAbsolutePath)
{
  const auto table
    = ResolveMountTable(Request({ Declared("Content", "Content") }));
  auto findings = std::vector<oxygen::content::import::ImportDiagnostic> {};
  const auto absolute = (Project() / "Content" / "wood.png").generic_string();

  const auto located
    = table.LocateReference(Project() / "Content" / "wood.material.json",
      CommittedPath { .text = absolute, .field = "source" }, findings);

  EXPECT_FALSE(located.has_value());
  EXPECT_TRUE(HasDiagnosticCode(findings, codes::kAbsoluteCommitted.id))
    << DiagnosticSummary(findings);
}

// -- Batches -----------------------------------------------------------------

//! One batch may not map one name to two folders; two spellings of one
//! folder are one folder. The conflict names the declaration that disagrees.
NOLINT_TEST_F(SourceMountsTest, BatchNameConflictIsRefused)
{
  fs::create_directories(Workspace() / "Other");
  const auto resolve
    = [&](const std::string& manifest, const std::string& path) -> MountTable {
    auto request = Request({ Declared("Content", path) });
    request.declared_in = manifest;
    return ResolveMountTable(request);
  };
  const auto first = resolve("a.manifest.json", "Content");
  const auto same = resolve("b.manifest.json", "./Content/");
  const auto other = resolve("c.manifest.json", "../Other");

  const auto agreeing = std::array { first, same };
  EXPECT_TRUE(CheckMountNameConflicts(agreeing).empty());

  const auto conflicting = std::array { first, other };
  const auto diagnostics = CheckMountNameConflicts(conflicting);
  ASSERT_EQ(diagnostics.size(), 1U) << DiagnosticSummary(diagnostics);
  EXPECT_EQ(diagnostics.front().code, codes::kNameConflict.id);
  EXPECT_EQ(diagnostics.front().source_path, "c.manifest.json");
  EXPECT_EQ(diagnostics.front().object_name, "Content");
}

// -- Shared cases ------------------------------------------------------------

//! Runs `MountTableCases.json`, which the editor's mount table reader also
//! runs. Each case gets its own fenced folder.
class MountTableCasesTest : public TempDirTest {
protected:
  static auto Cases() -> const json&
  {
    static const auto cases = [] -> json {
      auto stream = std::ifstream { OXYGEN_COOKER_MOUNT_TABLE_CASES };
      if (!stream) {
        throw std::runtime_error("MountTableCases.json is missing");
      }
      return json::parse(stream);
    }();
    return cases;
  }

  static auto Expand(std::string text, const fs::path& temp) -> std::string
  {
    constexpr auto kToken = std::string_view { "${temp}" };
    for (auto at = text.find(kToken); at != std::string::npos;
      at = text.find(kToken)) {
      text.replace(at, kToken.size(), temp.generic_string());
    }
    return text;
  }

  static auto ToKind(std::string_view kind) -> MountKind
  {
    return kind == "Cooked" ? MountKind::kCooked
      : kind == "Pak"       ? MountKind::kPak
                            : MountKind::kSource;
  }
};

NOLINT_TEST_F(MountTableCasesTest, TablesFollowTheCaseTable)
{
  for (const auto& [index, test_case] :
    std::views::enumerate(Cases().at("cases"))) {
    const auto name = test_case.at("name").get<std::string>();
    SCOPED_TRACE(name);
    const auto temp = TempDir() / std::to_string(index);
    fs::create_directories(temp);
    std::ofstream { temp / kMountsFileName } << R"({"version":1,"root":true})";
    for (const auto& folder : test_case.value("folders", json::array())) {
      fs::create_directories(temp / folder.get<std::string>());
    }
    for (const auto& file : test_case.value("files", json::array())) {
      const auto path = temp / file.get<std::string>();
      fs::create_directories(path.parent_path());
      std::ofstream { path } << "x";
    }
    for (const auto& [dir, doc] :
      test_case.value("mounts_files", json::object()).items()) {
      fs::create_directories(temp / dir);
      std::ofstream { temp / dir / kMountsFileName }
        << Expand(doc.dump(), temp);
    }

    auto declarations = std::vector<MountDeclaration> {};
    for (const auto& item : test_case.at("declarations")) {
      declarations.push_back(MountDeclaration {
        .name = item.at("name").get<std::string>(),
        .kind = ToKind(item.at("kind").get<std::string>()),
        .path = item.contains("path")
          ? std::optional { Expand(item.at("path").get<std::string>(), temp) }
          : std::nullopt,
      });
    }
    const auto project = temp / test_case.value("project", "ws/proj");
    fs::create_directories(project);
    const auto table = ResolveMountTable(MountTableRequest {
      .project_root = project,
      .declarations = std::move(declarations),
      .form = MountDeclarationForm::kCommittedFile,
      .declared_in = "Project.oxy",
      .working_directory = project,
      .mounts_option = std::nullopt,
      .owner = MountTableOwner::kProject,
    });

    const auto& expected = test_case.at("mounts");
    EXPECT_EQ(table.mounts.size(), expected.size())
      << DiagnosticSummary(table.diagnostics);
    for (const auto& item : expected) {
      const auto mount_name = item.at("name").get<std::string>();
      const auto mount = table.Find(mount_name);
      ASSERT_TRUE(mount) << mount_name;
      EXPECT_EQ(to_string(mount->kind), item.at("kind").get<std::string>());
      EXPECT_EQ(to_string(mount->scope), item.at("scope").get<std::string>());
      EXPECT_EQ(to_string(mount->state), item.at("state").get<std::string>());
      EXPECT_EQ(to_string(mount->origin), item.at("origin").get<std::string>());
      EXPECT_EQ(mount->origin_file,
        item.contains("origin_file")
          ? fs::weakly_canonical(
              temp / item.at("origin_file").get<std::string>())
          : fs::path {});
      EXPECT_EQ(mount->folder,
        item.contains("folder")
          ? fs::weakly_canonical(temp / item.at("folder").get<std::string>())
          : fs::path {});
    }

    auto actual_codes = std::set<std::string> {};
    for (const auto& diagnostic : table.diagnostics) {
      actual_codes.insert(diagnostic.code);
    }
    const auto expected_codes
      = test_case.at("codes").get<std::set<std::string>>();
    EXPECT_EQ(actual_codes, expected_codes)
      << DiagnosticSummary(table.diagnostics);
  }
}

} // namespace
