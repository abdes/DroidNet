//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <ranges>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/VirtualPath.h>
#include <Oxygen/Cooker/Import/ImportCodes.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportManifest.h>
#include <Oxygen/Cooker/Import/SourceMounts.h>
#include <Oxygen/Cooker/test/Support/AuthoredFiles.h>
#include <Oxygen/Cooker/test/Support/Diagnostics.h>
#include <Oxygen/Cooker/testing/CookedBytes.h>
#include <Oxygen/Cooker/testing/CookedPak.h>
#include <Oxygen/Cooker/testing/GeometryBuilder.h>
#include <Oxygen/Cooker/testing/LooseCookedRoot.h>
#include <Oxygen/Cooker/testing/MaterialBuilder.h>
#include <Oxygen/Cooker/testing/MemoryFileSystem.h>
#include <Oxygen/Cooker/testing/ScriptBuilder.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/LooseCookedIndexEncoder.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Data/SurfaceId.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::content::import::ImportDiagnostic;

namespace fs = std::filesystem;
namespace codes = oxygen::content::import::mount_codes;
using nlohmann::json;
using oxygen::make_observer;
using oxygen::content::import::CheckMountNameConflicts;
using oxygen::content::import::CommittedPath;
using oxygen::content::import::ImportManifest;
using oxygen::content::import::kMountsFileName;
using oxygen::content::import::MountDeclaration;
using oxygen::content::import::MountDeclarationForm;
using oxygen::content::import::MountKind;
using oxygen::content::import::MountOrigin;
using oxygen::content::import::MountRole;
using oxygen::content::import::MountScope;
using oxygen::content::import::MountState;
using oxygen::content::import::MountTable;
using oxygen::content::import::MountTableOwner;
using oxygen::content::import::MountTableRequest;
using oxygen::content::import::ParseMountKind;
using oxygen::content::import::ResolveMountTable;
using oxygen::cooker::test::DiagnosticSummary;
using oxygen::cooker::test::HasDiagnosticCode;
using oxygen::cooker::testing::CookedBytes;
using oxygen::cooker::testing::CookedPak;
using oxygen::cooker::testing::GeometryBuilder;
using oxygen::cooker::testing::LooseCookedFiles;
using oxygen::cooker::testing::LooseCookedRoot;
using oxygen::cooker::testing::MemoryFileSystem;
using oxygen::data::AssetKey;
using oxygen::data::AssetType;

//! Projects live at `<root>/ws/proj` on a file system held in memory, under a
//! root no test creates. A `.mounts.json` with `"root": true` at the root
//! fences every walk.
class SourceMountsTest : public testing::Test {
protected:
  auto SetUp() -> void override
  {
    WriteMounts(Root(),
      json {
        { "schema_version", oxygen::content::import::kMountsSchemaVersion },
        { "root", true },
      });
    MakeFolder(Project() / "Content");
  }

  static auto Root() -> fs::path
  {
    return fs::current_path().root_path() / "OxygenSourceMounts";
  }

  [[nodiscard]] static auto Workspace() -> fs::path { return Root() / "ws"; }

  [[nodiscard]] static auto Project() -> fs::path
  {
    return Workspace() / "proj";
  }

  auto MakeFolder(const fs::path& folder) -> void { files_.AddFolder(folder); }

  auto WriteText(const fs::path& path, std::string_view text) -> void
  {
    files_.AddFile(path, text);
  }

  auto WriteMounts(const fs::path& dir, const json& doc) -> fs::path
  {
    const auto path = dir / kMountsFileName;
    WriteText(path, doc.dump());
    return path;
  }

  static auto MountsDoc(const json& mounts) -> json
  {
    return json {
      {
        "schema_version",
        oxygen::content::import::kMountsSchemaVersion,
      },
      { "mounts", mounts },
    };
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
      .file_system = make_observer(&files_),
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
    return path.lexically_normal();
  }

  //! The files of a loose cooked root of `mount` that holds no asset.
  [[nodiscard]] static auto EmptyLooseFiles(std::string mount)
    -> LooseCookedFiles
  {
    return LooseCookedRoot { std::move(mount) }.Files();
  }

  //! A valid pak of `mount`.
  [[nodiscard]] static auto PakBytesOf(std::string mount)
    -> std::vector<std::byte>
  {
    auto library = LooseCookedRoot { std::move(mount) };
    library.Material("Content.omat");
    return CookedPak { std::move(library) }.Encode();
  }

  MemoryFileSystem files_;
};

//! Expects one mount bound to `folder` by `origin`.
auto ExpectBound(const MountTable& table, std::string_view name,
  const fs::path& folder, MountOrigin origin) -> void
{
  const auto mount = table.Find(name);
  ASSERT_TRUE(mount) << name << "\n" << DiagnosticSummary(table.diagnostics);
  EXPECT_EQ(mount->state, MountState::kBound);
  EXPECT_EQ(mount->folder, folder.lexically_normal());
  EXPECT_EQ(mount->origin, origin);
}

// -- Names -------------------------------------------------------------------

//! A committed name the mount rule refuses, a hidden one included, is
//! `mount.name_invalid`.
NOLINT_TEST_F(SourceMountsTest, InvalidCommittedNameIsRefused)
{
  for (const auto* const name : { "1Bad", ".Hidden" }) {
    const auto table
      = ResolveMountTable(Request({ Declared(name, "Content") }));

    EXPECT_FALSE(table.IsValid()) << name;
    EXPECT_TRUE(HasDiagnosticCode(table.diagnostics, codes::kNameInvalid.id))
      << name;
    EXPECT_FALSE(table.Find(name)) << name;
  }
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
  MakeFolder(Workspace() / "Alt");
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
  EXPECT_EQ(it->json_pointer, "/mounts/0/path");
  // The refused path is reported once; the mount is not a placeholder.
  EXPECT_FALSE(HasDiagnosticCode(table.diagnostics, codes::kUnbound.id));
}

//! `--mount` options are command lines: absolute paths are allowed, and
//! relative ones resolve against the working directory.
NOLINT_TEST_F(SourceMountsTest, CommandLineMountsAcceptAbsoluteAndRelative)
{
  MakeFolder(Workspace() / "Rel");
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
  MakeFolder(Workspace() / "Media" / "Showcase");

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
  MakeFolder(Project() / "Content" / "Free");

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
  MakeFolder(Project() / "ContentExtra");

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
  MakeFolder(Project() / ".cooked" / "Imports");
  MakeFolder(Project() / ".anything");

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
  MakeFolder(Workspace() / ".studio");

  const auto table
    = ResolveMountTable(Request({ Declared("Studio", "../.studio") }));

  EXPECT_TRUE(table.IsValid()) << DiagnosticSummary(table.diagnostics);
}

//! Loose cooked roots and paks live in output folders by design.
NOLINT_TEST_F(SourceMountsTest, CookedMountsMayLiveInOutput)
{
  files_.AddFiles(
    Project() / ".cooked" / "Content", EmptyLooseFiles("Content"));
  files_.AddFile(Project() / ".build" / "content.pak", PakBytesOf("Pak"));

  const auto table = ResolveMountTable(Request({
    MountDeclaration {
      .name = "Cooked",
      .kind = MountKind::kCooked,
      .path = ".cooked/Content",
    },
    MountDeclaration {
      .name = "Pak",
      .kind = MountKind::kCooked,
      .path = ".build/content.pak",
    },
  }));

  EXPECT_TRUE(table.IsValid()) << DiagnosticSummary(table.diagnostics);
  EXPECT_TRUE(table.diagnostics.empty())
    << DiagnosticSummary(table.diagnostics);
}

//! A `Source` mount inside the project root is the project's own; a mount
//! outside it, or a `Cooked` mount anywhere, is shared.
NOLINT_TEST_F(SourceMountsTest, RoleFollowsTheProjectRoot)
{
  MakeFolder(Workspace() / "Shared");
  files_.AddFiles(
    Project() / ".cooked" / "Content", EmptyLooseFiles("Content"));

  const auto table = ResolveMountTable(Request({
    Declared("Content", "Content"),
    Declared("Shared", "../Shared"),
    MountDeclaration {
      .name = "Cooked",
      .kind = MountKind::kCooked,
      .path = ".cooked/Content",
    },
  }));

  ASSERT_TRUE(table.IsValid()) << DiagnosticSummary(table.diagnostics);
  EXPECT_EQ(table.Find("Content")->role, MountRole::kOwn);
  EXPECT_EQ(table.Find("Shared")->role, MountRole::kShared);
  EXPECT_EQ(table.Find("Cooked")->role, MountRole::kShared);
}

//! An `Invalid` cooked mount keeps its finding, also in the table's
//! diagnostics, and stays in the table as a warning.
NOLINT_TEST_F(SourceMountsTest, InvalidCookedMountKeepsItsFinding)
{
  MakeFolder(Workspace() / "NotCooked");

  const auto table = ResolveMountTable(Request({
    MountDeclaration {
      .name = "Props",
      .kind = MountKind::kCooked,
      .path = "../NotCooked",
    },
  }));

  EXPECT_TRUE(table.IsValid()) << DiagnosticSummary(table.diagnostics);
  const auto mount = table.Find("Props");
  ASSERT_TRUE(mount);
  EXPECT_EQ(mount->state, MountState::kInvalid);
  ASSERT_EQ(mount->findings.size(), 1U);
  EXPECT_EQ(mount->findings.front().code, codes::kCookedFormatUnsupported.id);
  EXPECT_EQ(mount->findings.front().object_name, "Props");
  EXPECT_TRUE(
    HasDiagnosticCode(table.diagnostics, codes::kCookedFormatUnsupported.id));
}

// -- Path kind, missing and unbound ------------------------------------------

//! A `Source` mount names a folder.
NOLINT_TEST_F(SourceMountsTest, SourceMountBoundToAFileIsRefused)
{
  WriteText(Project() / "file.txt", "x");

  const auto table
    = ResolveMountTable(Request({ Declared("Source", "file.txt") }));

  EXPECT_FALSE(table.IsValid());
  EXPECT_TRUE(HasDiagnosticCode(table.diagnostics, codes::kPathKind.id));
}

//! Only `Source` and `Cooked` are kinds.
NOLINT_TEST(SourceMountKindTest, ParsesTheTwoKinds)
{
  EXPECT_EQ(ParseMountKind("Source"), MountKind::kSource);
  EXPECT_EQ(ParseMountKind("Cooked"), MountKind::kCooked);
  EXPECT_FALSE(ParseMountKind("Pak"));
  EXPECT_FALSE(ParseMountKind("source"));
  EXPECT_FALSE(ParseMountKind(""));
}

//! A manifest mount of another kind is `mount.kind_unknown`, at its field.
NOLINT_TEST(SourceMountKindTest, ManifestRefusesAnUnknownKind)
{
  auto errors = std::vector<ImportDiagnostic> {};

  const auto manifest = ImportManifest::Parse(
    oxygen::cooker::test::ManifestText(
      R"({"jobs":[],"mounts":[{"name":"Props","kind":"Pak","path":"a.pak"}]})"),
    fs::current_path().root_path() / "OxygenSourceMounts", {}, errors);

  EXPECT_FALSE(manifest.has_value());
  ASSERT_EQ(errors.size(), 1U) << DiagnosticSummary(errors);
  EXPECT_EQ(
    errors.front().code, oxygen::content::import::mount_codes::kKindUnknown.id);
  EXPECT_EQ(errors.front().json_pointer, "/mounts/0/kind");
}

//! A declaration whose kind is neither `Source` nor `Cooked` is
//! `mount.kind_unknown`, however it was built, and never read as `Source`.
NOLINT_TEST_F(SourceMountsTest, UnknownDeclaredKindIsRefused)
{
  auto declaration = Declared("Content", "Content");
  declaration.kind = static_cast<MountKind>(2);

  const auto table = ResolveMountTable(Request({ declaration }));

  EXPECT_FALSE(table.IsValid());
  EXPECT_TRUE(HasDiagnosticCode(table.diagnostics, codes::kKindUnknown.id))
    << DiagnosticSummary(table.diagnostics);
  EXPECT_FALSE(table.Find("Content"));
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

//! Another schema version is `mount.file_version`; the file binds nothing.
NOLINT_TEST_F(SourceMountsTest, OtherFileVersionIsRefused)
{
  WriteMounts(Project(),
    json {
      { "schema_version", oxygen::content::import::kMountsSchemaVersion + 1 },
      { "mounts", { { "Studio", "Content" } } },
    });

  const auto table = ResolveMountTable(Request({ Placeholder("Studio") }));

  EXPECT_TRUE(HasDiagnosticCode(table.diagnostics, codes::kFileVersion.id));
  EXPECT_TRUE(HasDiagnosticCode(table.diagnostics, codes::kUnbound.id));
}

//! `.mounts.json` holds only mounts: another key, a kind (kind is fixed by
//! the committed declaration) or malformed JSON is `mount.file_invalid`.
NOLINT_TEST_F(SourceMountsTest, InvalidFileIsRefused)
{
  const auto cases = std::array {
    json {
      { "schema_version", oxygen::content::import::kMountsSchemaVersion },
      { "cooked_root", "x" },
    }
      .dump(),
    MountsDoc({ { "Studio", { { "kind", "Cooked" }, { "path", "a.pak" } } } })
      .dump(),
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

//! A file that states no `schema_version` has no version this build reads.
NOLINT_TEST_F(SourceMountsTest, FileWithoutSchemaVersionIsRefused)
{
  WriteMounts(Project(), json { { "mounts", json::object() } });

  const auto table = ResolveMountTable(Request({}));

  EXPECT_FALSE(table.IsValid());
  EXPECT_TRUE(HasDiagnosticCode(table.diagnostics, codes::kFileVersion.id))
    << DiagnosticSummary(table.diagnostics);
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
  WriteMounts(Project(),
    json {
      { "schema_version", oxygen::content::import::kMountsSchemaVersion },
      { "root", true },
    });

  const auto table
    = ResolveMountTable(Request({ Declared("Content", "Content") }));

  EXPECT_TRUE(table.IsValid()) << DiagnosticSummary(table.diagnostics);
}

// -- Precedence --------------------------------------------------------------

//! The project's own `.mounts.json` overrides a committed path, relative to
//! its own folder.
NOLINT_TEST_F(SourceMountsTest, ProjectFileOverridesCommittedPath)
{
  MakeFolder(Workspace() / "Alt");
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
  MakeFolder(Workspace() / "Alt");
  MakeFolder(Workspace() / "Opt");
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
  WriteText(option,
    json {
      { "schema_version", oxygen::content::import::kMountsSchemaVersion },
      { "root", true },
    }
      .dump());
  auto request = Request({ Placeholder("Studio") });
  request.mounts_option = option;

  const auto table = ResolveMountTable(request);

  EXPECT_TRUE(HasDiagnosticCode(table.diagnostics, codes::kUnbound.id));
}

//! A parent file binds a placeholder and adds a local Source mount.
NOLINT_TEST_F(SourceMountsTest, ParentFileBindsPlaceholdersAndAddsLocals)
{
  MakeFolder(Workspace() / "Studio");
  MakeFolder(Workspace() / "Local");
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
  MakeFolder(Workspace() / "Elsewhere");
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
  MakeFolder(Workspace() / "Parent");
  MakeFolder(Workspace() / "Own");
  WriteMounts(Workspace(), MountsDoc({ { "Studio", "Parent" } }));
  WriteMounts(Project(), MountsDoc({ { "Studio", "../Own" } }));

  const auto table = ResolveMountTable(Request({ Placeholder("Studio") }));

  ExpectBound(table, "Studio", Workspace() / "Own", MountOrigin::kMountsFile);
}

//! The nearer parent wins; `"root": true` stops the walk above it.
NOLINT_TEST_F(SourceMountsTest, NearestParentWinsAndRootStopsTheWalk)
{
  const auto nested = Workspace() / "group" / "proj";
  MakeFolder(nested / "Content");
  MakeFolder(Workspace() / "Near");
  MakeFolder(Workspace() / "Far");
  WriteMounts(Workspace() / "group",
    json {
      { "schema_version", oxygen::content::import::kMountsSchemaVersion },
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

//! A mount path names a file of another mount, whatever the referrer's
//! folder.
NOLINT_TEST_F(SourceMountsTest, LocateReferenceResolvesAMountPath)
{
  MakeFolder(Project() / "Media" / "Images");
  const auto table = ResolveMountTable(
    Request({ Declared("Content", "Content"), Declared("Media", "Media") }));
  ASSERT_TRUE(table.IsValid()) << DiagnosticSummary(table.diagnostics);
  auto findings = std::vector<oxygen::content::import::ImportDiagnostic> {};

  const auto located = table.LocateReference(
    Project() / "Content" / "Materials" / "wood.material.json",
    CommittedPath {
      .text = "/Media/Images/wood.png",
      .json_pointer = "/source",
    },
    findings);

  ASSERT_HAS_VALUE(located) << DiagnosticSummary(findings);
  EXPECT_TRUE(findings.empty());
  EXPECT_EQ(located->file, Canonical(Project() / "Media/Images/wood.png"));
  EXPECT_EQ(located->mount_path, "/Media/Images/wood.png");
}

//! A file of the referrer's own mount is named relatively: its mount path is
//! refused with `import.reference_invalid`, naming
//! `vpath.reference_self_absolute`.
NOLINT_TEST_F(SourceMountsTest, LocateReferenceRefusesASelfAbsolutePath)
{
  const auto table
    = ResolveMountTable(Request({ Declared("Content", "Content") }));
  auto findings = std::vector<oxygen::content::import::ImportDiagnostic> {};

  const auto refused = table.LocateReference(
    Project() / "Content" / "Materials" / "wood.material.json",
    CommittedPath {
      .text = "/Content/Images/wood.png",
      .json_pointer = "/source",
    },
    findings);

  EXPECT_FALSE(refused.has_value());
  ASSERT_TRUE(HasDiagnosticCode(
    findings, oxygen::content::import::import_codes::kReferenceInvalid))
    << DiagnosticSummary(findings);
  EXPECT_NE(findings.front().message.find(
              oxygen::content::vpath_codes::kReferenceSelfAbsolute),
    std::string::npos);
  EXPECT_EQ(findings.front().json_pointer, "/source");
}

//! A relative path may name source media, which asset references refuse.
NOLINT_TEST_F(SourceMountsTest, LocateReferenceResolvesSourceMedia)
{
  const auto table
    = ResolveMountTable(Request({ Declared("Content", "Content") }));
  auto findings = std::vector<oxygen::content::import::ImportDiagnostic> {};

  const auto located = table.LocateReference(
    Project() / "Content" / "Textures" / "wood.otex.json",
    CommittedPath {
      .text = "../SourceMedia/Images/wood.png",
      .json_pointer = "/source",
    },
    findings);

  ASSERT_HAS_VALUE(located) << DiagnosticSummary(findings);
  EXPECT_EQ(located->mount_path, "/Content/SourceMedia/Images/wood.png");
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
    CommittedPath { .text = "../Images/wood.png", .json_pointer = "/source" },
    findings);

  ASSERT_HAS_VALUE(located) << DiagnosticSummary(findings);
  EXPECT_TRUE(findings.empty());
  EXPECT_EQ(located->file, Canonical(Project() / "Content/Images/wood.png"));
  EXPECT_EQ(located->mount_path, "/Content/Images/wood.png");
}

//! A relative path that climbs out of the referrer's mount is refused with
//! `import.reference_invalid`, naming `vpath.reference_outside_mount`; a mount
//! path may name the other mount.
NOLINT_TEST_F(
  SourceMountsTest, LocateReferenceRefusesARelativePathLeavingTheMount)
{
  MakeFolder(Project() / "Other");
  const auto table = ResolveMountTable(
    Request({ Declared("Content", "Content"), Declared("Other", "Other") }));
  ASSERT_TRUE(table.IsValid()) << DiagnosticSummary(table.diagnostics);
  auto findings = std::vector<oxygen::content::import::ImportDiagnostic> {};
  const auto referrer = Project() / "Content" / "wood.material.json";

  const auto refused = table.LocateReference(referrer,
    CommittedPath { .text = "../Other/wood.png", .json_pointer = "/source" },
    findings);

  EXPECT_FALSE(refused.has_value());
  ASSERT_TRUE(HasDiagnosticCode(
    findings, oxygen::content::import::import_codes::kReferenceInvalid))
    << DiagnosticSummary(findings);
  EXPECT_NE(findings.front().message.find(
              oxygen::content::vpath_codes::kReferenceOutsideMount),
    std::string::npos);

  findings.clear();
  const auto named = table.LocateReference(referrer,
    CommittedPath { .text = "/Other/wood.png", .json_pointer = "/source" },
    findings);
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
      CommittedPath { .text = absolute, .json_pointer = "/source" }, findings);

  EXPECT_FALSE(located.has_value());
  EXPECT_TRUE(HasDiagnosticCode(findings, codes::kAbsoluteCommitted.id))
    << DiagnosticSummary(findings);
}

//! A mount name is spelled as declared: `/content/...` from a `/Content`
//! file is `import.mount_misspelled`, never a second spelling of the
//! referrer's own mount.
NOLINT_TEST_F(SourceMountsTest, LocateReferenceRefusesAMisspelledMount)
{
  const auto table
    = ResolveMountTable(Request({ Declared("Content", "Content") }));
  auto findings = std::vector<oxygen::content::import::ImportDiagnostic> {};

  const auto located = table.LocateReference(
    Project() / "Content" / "Materials" / "wood.material.json",
    CommittedPath {
      .text = "/content/Images/wood.png",
      .json_pointer = "/source",
    },
    findings);

  EXPECT_FALSE(located.has_value());
  EXPECT_TRUE(HasDiagnosticCode(findings, codes::kSourceMountMisspelled.id))
    << DiagnosticSummary(findings);
}

//! A mount path the virtual path rules refuse is `import.reference_invalid`,
//! naming the rule, before any file is looked for.
NOLINT_TEST_F(SourceMountsTest, LocateSourceRefusesAPathTheRulesRefuse)
{
  const auto table
    = ResolveMountTable(Request({ Declared("Content", "Content") }));
  auto findings = std::vector<oxygen::content::import::ImportDiagnostic> {};

  const auto located = table.LocateSource(
    CommittedPath {
      .text = "/Content/Images/wood.png.",
      .json_pointer = "/source",
    },
    findings);

  EXPECT_FALSE(located.has_value());
  ASSERT_TRUE(HasDiagnosticCode(
    findings, oxygen::content::import::import_codes::kReferenceInvalid))
    << DiagnosticSummary(findings);
  EXPECT_NE(findings.front().message.find(
              oxygen::content::vpath_codes::kSegmentTrailingDot),
    std::string::npos);
  EXPECT_EQ(findings.front().json_pointer, "/source");
}

//! A link inside the mount the text names leaves the file in that mount: it
//! is named by its final location there.
NOLINT_TEST_F(SourceMountsTest, LocateSourceFollowsALinkWithinItsMount)
{
  files_.AddLink(
    Project() / "Content" / "Alias", Project() / "Content" / "Images");
  const auto table
    = ResolveMountTable(Request({ Declared("Content", "Content") }));
  auto findings = std::vector<oxygen::content::import::ImportDiagnostic> {};

  const auto located = table.LocateSource(
    CommittedPath {
      .text = "/Content/Alias/wood.png",
      .json_pointer = "/source",
    },
    findings);

  ASSERT_HAS_VALUE(located) << DiagnosticSummary(findings);
  EXPECT_EQ(located->mount_path, "/Content/Images/wood.png");
}

//! A link that leads into another mount is `import.source_crosses_mount`: the
//! file is named by the mount that holds it, never through the link.
NOLINT_TEST_F(SourceMountsTest, LocateReferenceRefusesALinkIntoAnotherMount)
{
  MakeFolder(Project() / "Media" / "Images");
  files_.AddLink(
    Project() / "Content" / "Shared", Project() / "Media" / "Images");
  const auto table = ResolveMountTable(
    Request({ Declared("Content", "Content"), Declared("Media", "Media") }));
  ASSERT_TRUE(table.IsValid()) << DiagnosticSummary(table.diagnostics);
  auto findings = std::vector<oxygen::content::import::ImportDiagnostic> {};

  const auto located = table.LocateReference(
    Project() / "Content" / "Materials" / "wood.material.json",
    CommittedPath { .text = "../Shared/wood.png", .json_pointer = "/source" },
    findings);

  EXPECT_FALSE(located.has_value());
  EXPECT_TRUE(HasDiagnosticCode(findings, codes::kSourceCrossesMount.id))
    << DiagnosticSummary(findings);
}

//! A link that leads out of every mount is `import.source_outside_mount`.
NOLINT_TEST_F(SourceMountsTest, LocateSourceRefusesALinkOutOfEveryMount)
{
  files_.AddLink(Project() / "Content" / "Elsewhere", Root() / "Elsewhere");
  const auto table
    = ResolveMountTable(Request({ Declared("Content", "Content") }));
  auto findings = std::vector<oxygen::content::import::ImportDiagnostic> {};

  const auto located = table.LocateSource(
    CommittedPath {
      .text = "/Content/Elsewhere/wood.png",
      .json_pointer = "/source",
    },
    findings);

  EXPECT_FALSE(located.has_value());
  EXPECT_TRUE(HasDiagnosticCode(findings, codes::kSourceOutsideMount.id))
    << DiagnosticSummary(findings);
}

// -- Batches -----------------------------------------------------------------

//! One batch may not map one name to two folders; two spellings of one
//! folder are one folder. The conflict names the declaration that disagrees.
NOLINT_TEST_F(SourceMountsTest, BatchNameConflictIsRefused)
{
  MakeFolder(Workspace() / "Other");
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
//! runs. Each case gets its own folder on a file system held in memory.
class MountTableCasesTest : public testing::Test {
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

  static auto ToKind(const std::string_view kind) -> MountKind
  {
    const auto parsed = ParseMountKind(kind);
    if (!parsed.has_value()) {
      throw std::invalid_argument("unknown mount kind in a case");
    }
    return *parsed;
  }

  //! An asset of a loose cooked source, by virtual path.
  struct LooseEntry final {
    std::string path;
    AssetType type = AssetType::kUnknown;
    std::vector<std::byte> descriptor;
    std::vector<oxygen::data::loose_cooked::IndexedRequirement> requirements;
  };

  //! The descriptor of an asset of `type` that a case names.
  static auto DescriptorOf(const AssetType type) -> std::vector<std::byte>
  {
    switch (type) {
    case AssetType::kMaterial:
      return oxygen::cooker::testing::MaterialBuilder { "Case" }.Bytes();
    case AssetType::kGeometry:
      return GeometryBuilder { "Case" }.Bytes();
    default:
      throw std::invalid_argument("a case holds only materials and geometries");
    }
  }

  //! The entries of a loose source of `roots`: a material under each, and a
  //! script with an external source under the first when `external_script`.
  static auto RootEntries(const json& roots, bool external_script)
    -> std::vector<LooseEntry>
  {
    auto entries = std::vector<LooseEntry> {};
    for (const auto& root : roots) {
      const auto mount = root.get<std::string>();
      entries.push_back({
        .path = "/" + mount + "/Materials/Red.omat",
        .type = AssetType::kMaterial,
        .descriptor = DescriptorOf(AssetType::kMaterial),
      });
      if (external_script) {
        auto script = oxygen::cooker::testing::ScriptBuilder { "Spin" };
        script
          .Flags(oxygen::data::pak::scripting::ScriptAssetFlags::
              kAllowExternalSource)
          .ExternalSource("/" + mount + "/Scripts/Spin.lua");
        entries.push_back({
          .path = "/" + mount + "/Scripts/Spin.oscript",
          .type = AssetType::kScript,
          .descriptor = script.Bytes(),
        });
        external_script = false;
      }
    }
    return entries;
  }

  //! The files of a loose cooked root that holds `entries`, which may lie in
  //! several mounts.
  static auto LooseFilesOf(const std::vector<LooseEntry>& entries)
    -> LooseCookedFiles
  {
    auto root = oxygen::cooker::testing::LooseCookedRoot {};
    for (const auto& entry : entries) {
      // `/Mount/leaf`: every entry names its own mount.
      const auto separator = entry.path.find('/', 1);
      static_cast<void>(root.Add({
        .type = entry.type,
        .leaf = entry.path.substr(separator + 1),
        .mount = entry.path.substr(1, separator - 1),
        .descriptor = entry.descriptor,
        .requirements = entry.requirements,
      }));
    }
    return root.Files();
  }

  //! The asset type a case names, such as `Material`.
  static auto ParseAssetType(const std::string& name) -> AssetType
  {
    for (auto value = static_cast<uint8_t>(AssetType::kUnknown) + 1U;
      value <= static_cast<uint8_t>(AssetType::kMaxAssetType); ++value) {
      if (name == oxygen::data::to_string(static_cast<AssetType>(value))) {
        return static_cast<AssetType>(value);
      }
    }
    throw std::invalid_argument("unknown asset type in a case");
  }

  //! The leaf of the virtual path `path` below its mount `mount`.
  static auto LeafOf(const std::string& path, const std::string& mount)
    -> std::string
  {
    return path.substr(mount.size() + 2U);
  }

  //! The pak of a dependency mount that holds each asset a recipe requires of
  //! it, so the pak under test derives its requirements from the product.
  static auto DependencyOf(const std::string& mount, const json& requirements)
    -> CookedPak
  {
    auto library = LooseCookedRoot { mount };
    for (const auto& required : requirements) {
      if (required.at("mount").get<std::string>() != mount) {
        continue;
      }
      const auto type = ParseAssetType(required.at("type").get<std::string>());
      library.Add({
        .type = type,
        .leaf = LeafOf(required.at("path").get<std::string>(), mount),
        .descriptor = DescriptorOf(type),
      });
    }
    return CookedPak { std::move(library) };
  }

  //! Builds the pak a recipe names: its mount, its assets and what it
  //! requires of other mounts.
  /*!
   The pak also holds `Content.ogeo`, a geometry painted with every
   material the recipe requires, whose references become the pak's
   requirements; a pak holds at least one asset.

   @throw std::invalid_argument When a requirement is not a material: a
     geometry requires nothing else.
  */
  static auto EncodePak(const json& recipe, const bool unsupported)
    -> std::vector<std::byte>
  {
    const auto mount = recipe.at("roots").at(0).get<std::string>();
    auto library = LooseCookedRoot { mount };
    for (const auto& asset : recipe.value("assets", json::array())) {
      const auto type = ParseAssetType(asset.at("type").get<std::string>());
      library.Add({
        .type = type,
        .leaf = LeafOf(asset.at("path").get<std::string>(), mount),
        .descriptor = DescriptorOf(type),
      });
    }
    const auto requirements = recipe.value("requirements", json::array());
    auto required_mounts = std::set<std::string> {};
    // Without requirements, the geometry is the unit cube.
    auto content = GeometryBuilder { "Content" };
    const auto lod = requirements.empty() ? 0U : content.ProceduralLod();
    for (const auto& required : requirements) {
      required_mounts.insert(required.at("mount").get<std::string>());
      const auto path = required.at("path").get<std::string>();
      if (ParseAssetType(required.at("type").get<std::string>())
        != AssetType::kMaterial) {
        throw std::invalid_argument(
          "the recipe requires " + path + ", which is not a material");
      }
      content.Submesh(lod, oxygen::data::SurfaceId::FromStableIdentity(path),
        AssetKey::FromVirtualPath(path));
    }
    static_cast<void>(library.Geometry("Content.ogeo", content));
    auto pak = CookedPak { std::move(library) };
    for (const auto& required_mount : required_mounts) {
      pak.DependsOn(DependencyOf(required_mount, requirements));
    }
    auto bytes = pak.Encode();
    if (unsupported) {
      CookedBytes::SetPakVersion(bytes,
        static_cast<uint16_t>(
          oxygen::data::pak::core::kCurrentPakFormatVersion + 1U));
    }
    return bytes;
  }

  //! Builds the cooked source a recipe names with the kit.
  static auto AddCookedSource(
    MemoryFileSystem& files, const fs::path& temp, const json& recipe) -> void
  {
    const auto path = temp / recipe.at("path").get<std::string>();
    const auto unsupported = recipe.value("format", "current") == "unsupported";
    if (recipe.at("kind") == "pak") {
      files.AddFile(path, EncodePak(recipe, unsupported));
      return;
    }
    auto entries = RootEntries(recipe.value("roots", json::array()),
      recipe.value("external_script", false));
    for (const auto& asset : recipe.value("assets", json::array())) {
      const auto type = ParseAssetType(asset.at("type").get<std::string>());
      entries.push_back({
        .path = asset.at("path").get<std::string>(),
        .type = type,
        .descriptor = DescriptorOf(type),
      });
    }
    for (const auto& required : recipe.value("requirements", json::array())) {
      const auto path = required.at("path").get<std::string>();
      entries.front().requirements.push_back({
        .mount = required.at("mount").get<std::string>(),
        .kind = oxygen::data::KeyReferenceKind::kAsset,
        .key = oxygen::data::AssetKey::FromVirtualPath(path),
        .type = ParseAssetType(required.at("type").get<std::string>()),
      });
    }
    auto loose = LooseFilesOf(entries);
    if (unsupported) {
      CookedBytes::SetIndexVersion(loose,
        static_cast<uint16_t>(oxygen::data::loose_cooked::kIndexVersion + 1U));
    }
    files.AddFiles(path, loose);
  }
};

NOLINT_TEST_F(MountTableCasesTest, TablesFollowTheCaseTable)
{
  for (const auto& [index, test_case] :
    std::views::enumerate(Cases().at("cases"))) {
    const auto name = test_case.at("name").get<std::string>();
    SCOPED_TRACE(name);
    const auto temp = fs::current_path().root_path() / "OxygenMountTableCases"
      / std::to_string(index);
    auto files = MemoryFileSystem {};
    files.AddFile(temp / kMountsFileName,
      oxygen::cooker::test::MountsText(R"({"root":true})"));
    for (const auto& folder : test_case.value("folders", json::array())) {
      files.AddFolder(temp / folder.get<std::string>());
    }
    for (const auto& file : test_case.value("files", json::array())) {
      files.AddFile(temp / file.get<std::string>(), "x");
    }
    for (const auto& recipe :
      test_case.value("cooked_sources", json::array())) {
      AddCookedSource(files, temp, recipe);
    }
    for (const auto& [dir, doc] :
      test_case.value("mounts_files", json::object()).items()) {
      files.AddFile(temp / dir / kMountsFileName, Expand(doc.dump(), temp));
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
    files.AddFolder(project);
    const auto table = ResolveMountTable(MountTableRequest {
      .project_root = project,
      .declarations = std::move(declarations),
      .form = MountDeclarationForm::kCommittedFile,
      .declared_in = "Project.oxy",
      .working_directory = project,
      .mounts_option = std::nullopt,
      .owner = MountTableOwner::kProject,
      .file_system = make_observer(&files),
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
      EXPECT_EQ(to_string(mount->role), item.at("role").get<std::string>());
      EXPECT_EQ(to_string(mount->origin), item.at("origin").get<std::string>());
      EXPECT_EQ(mount->origin_file,
        item.contains("origin_file")
          ? (temp / item.at("origin_file").get<std::string>())
              .lexically_normal()
          : fs::path {});
      EXPECT_EQ(mount->folder,
        item.contains("folder")
          ? (temp / item.at("folder").get<std::string>()).lexically_normal()
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
