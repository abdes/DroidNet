// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Mounts;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Managed.Core;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.ContentPipeline.Unit.Tests.Cooking;

/// <summary>
/// The cook names each file it reads by its cook path: a source by its mount path, inside or outside the project tree,
/// and a project file by its path below the project root. A source in a mount this machine cannot read is a coded
/// failure, and the ImportTool gets the table the editor shows as its mounts file.
/// </summary>
[TestClass]
internal sealed class CookPathsTests
{
    private const string Root = @"C:\Games\Demo";
    private const string Studio = @"D:\Studio\Shared";

    private readonly MockFileSystem fileSystem = new();
    private readonly ProjectContext project;

    public CookPathsTests()
    {
        _ = this.fileSystem.Directory.CreateDirectory(Path.Combine(Root, "Content", "Materials"));
        _ = this.fileSystem.Directory.CreateDirectory(Path.Combine(Studio, "Materials"));
        this.fileSystem.File.WriteAllText(Path.Combine(Root, "Content", "Materials", "Red.omat.json"), "{}");
        this.fileSystem.File.WriteAllText(Path.Combine(Studio, "Materials", "Brick.omat.json"), "{}");
        this.fileSystem.File.WriteAllText(Path.Combine(Root, ProjectMounts.FileName), """{"schema_version":2,"mounts":{"Studio":"D:/Studio/Shared"}}""");
        // Opened as saved, so the committed Media folder is not created: it is missing on this machine.
        var info = new ProjectInfo(Guid.NewGuid(), "Demo", Category.Games, Root)
        {
            Mounts =
            [
                new ProjectMount("Content", MountKind.Source, "Content"),
                new ProjectMount("Studio"),
                new ProjectMount("Vendor"),
                new ProjectMount("Media", MountKind.Source, "../Media"),
            ],
        };
        this.project = TestProject.Open(info, this.fileSystem).Context;
    }

    [TestMethod]
    public void ASourceIsNamedByItsMountPath_InsideOrOutsideTheProject()
    {
        _ = CookPaths.Of(this.project, FileSystemPath.From(Path.Combine(Root, "Content", "Materials", "Red.omat.json"))).Should().Be("/Content/Materials/Red.omat.json");
        _ = CookPaths.Of(this.project, FileSystemPath.From(Path.Combine(Studio, "Materials", "Brick.omat.json"))).Should().Be("/Studio/Materials/Brick.omat.json");
    }

    [TestMethod]
    public void AProjectFileOutsideEverySourceMount_IsNamedBelowTheProjectRoot()
        => _ = CookPaths.Of(this.project, FileSystemPath.From(Path.Combine(Root, "Imports", "Content", "Car.glb.import.json"))).Should().Be("Imports/Content/Car.glb.import.json");

    [TestMethod]
    public void AFileOutsideEveryMountAndTheProject_HasNoCookPath()
    {
        var outside = FileSystemPath.From(@"E:\Elsewhere\Wood.omat.json");

        _ = CookPaths.TryOf(this.project, outside).Should().BeNull();
        _ = ((Action)(() => CookPaths.Of(this.project, outside))).Should().Throw<InvalidDataException>();
    }

    [TestMethod]
    public void ACookPathResolvesThroughTheMountTable()
    {
        _ = CookPaths.FileOf(this.project, "/Studio/Materials/Brick.omat.json").Should().Be(FileSystemPath.From(Path.Combine(Studio, "Materials", "Brick.omat.json")));
        _ = CookPaths.FileOf(this.project, "Imports/Content/Car.glb.import.json").Should().Be(FileSystemPath.From(Path.Combine(Root, "Imports", "Content", "Car.glb.import.json")));
        _ = CookPaths.TryFileOf(this.project, "/Vendor/Materials/Brick.omat.json").Should().BeNull("an unbound mount names no file");
    }

    [TestMethod]
    public void ExistsExactly_ComparesCaseBelowTheMountFolder()
    {
        _ = CookPaths.ExistsExactly(this.project, "/Studio/Materials/Brick.omat.json").Should().BeTrue();
        _ = CookPaths.ExistsExactly(this.project, "/Studio/materials/Brick.omat.json").Should().BeFalse("a rename that changes only case renames the source");
        _ = CookPaths.ExistsExactly(this.project, "/Vendor/Materials/Brick.omat.json").Should().BeFalse();
    }

    [TestMethod]
    [DataRow("/Content/Materials/Red.omat.json", true)]
    [DataRow("Imports/Content/Car.glb.import.json", true)]
    [DataRow(".pipeline/Scenes/Content/Main.oscene.json", true)]
    [DataRow(".mounts/Content/Red.omat.json", false)]
    [DataRow("/Content/../Red.omat.json", false)]
    [DataRow("Content\\Red.omat.json", false)]
    [DataRow("", false)]
    public void IsValid_AcceptsMountAndProjectPathsOnly(string cookPath, bool valid)
        => _ = CookPaths.IsValid(cookPath).Should().Be(valid);

    [TestMethod]
    public void AMountPathIsCapturedBelowTheMountsFolderOfTheInputRoot()
    {
        _ = CookPaths.StoragePathOf("/Studio/Materials/Brick.omat.json").Should().Be(".mounts/Studio/Materials/Brick.omat.json");
        _ = CookPaths.StoragePathOf("Imports/Content/Car.glb.import.json").Should().Be("Imports/Content/Car.glb.import.json");
    }

    [TestMethod]
    public void ResolvingASourceOutsideTheProject_GivesItsMountPath()
    {
        var input = CookInputResolver.Resolve(this.project, AssetPath.Parse("/Studio/Materials/Brick.omat"), ContentCookInputRole.Primary);

        _ = input.SourceCookPath.Should().Be("/Studio/Materials/Brick.omat.json");
        _ = input.SourceAbsolutePath.Should().Be(FileSystemPath.From(Path.Combine(Studio, "Materials", "Brick.omat.json")));
        _ = input.MountName.Should().Be("Studio");
    }

    [TestMethod]
    [DataRow("/Vendor/Materials/Brick.omat", MountCodes.Unbound)]
    [DataRow("/Media/Materials/Brick.omat", MountCodes.FolderMissing)]
    public void ResolvingASourceInAMountThisMachineCannotRead_IsACodedFailure(string identity, string code)
    {
        var resolve = () => CookInputResolver.Resolve(this.project, AssetPath.Parse(identity), ContentCookInputRole.Primary);

        _ = resolve.Should().Throw<CookMountUnavailableException>().Which.Code.Should().Be(code);
    }

    [TestMethod]
    public void TheMountsFileBindsEveryBoundSourceMountAndStopsTheWalk()
    {
        var file = ContentImportMountsFile.Of(this.project);

        _ = file.Mounts.Should().BeEquivalentTo(new Dictionary<string, string>(StringComparer.Ordinal)
        {
            ["Content"] = "C:/Games/Demo/Content",
            ["Studio"] = "D:/Studio/Shared",
        });
        _ = file.Root.Should().BeTrue();
        _ = file.Resolve(Root, "/Studio/Materials/Brick.omat.json").Should().Be(Path.Combine(Studio, "Materials", "Brick.omat.json"));
        _ = file.Resolve(Root, "Imports/Content/Car.glb.import.json").Should().Be(Path.Combine(Root, "Imports", "Content", "Car.glb.import.json"));
        _ = file.Resolve(Root, "/Vendor/Brick.omat.json").Should().Be("/Vendor/Brick.omat.json", "a mount it does not bind is the tool's to report");
    }

    [TestMethod]
    public void TheManifestDeclaresTheCommittedSourceMountsAndThePlaceholders()
        => _ = ContentImportManifestBuilder.CreateMounts(this.project).Should().Equal(
            new ContentImportMount("Content", "Content"),
            new ContentImportMount("Studio", Path: null),
            new ContentImportMount("Vendor", Path: null),
            new ContentImportMount("Media", "../Media"));
}
