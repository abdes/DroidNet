// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Projects.Mounts;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Managed.Core;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.Projects.Tests;

/// <summary>Verifies the one mapping between the project's virtual paths and their files.</summary>
[TestClass]
[TestCategory("Unit")]
public sealed class ProjectAssetFilesTests
{
    private static readonly ProjectContext Project = CreateProject();

    [TestMethod]
    public void Find_ForAnAuthoringFile_ResolvesItsFileAndMount()
    {
        var location = Project.Find(AssetPath.Parse("/Content/Materials/Wood.omat.json"));

        _ = location.Should().NotBeNull();
        _ = location!.File.Should().Be(FileSystemPath.From(@"C:\Games\Demo\Content\Materials\Wood.omat.json"));
        _ = location.MountRoot.Should().Be(FileSystemPath.From(@"C:\Games\Demo\Content"));
        _ = location.Path.Should().Be(AssetPath.Parse("/Content/Materials/Wood.omat.json"));
        _ = location.MountKind.Should().Be(ProjectMountKind.Authoring);
        _ = location.IsMountRoot.Should().BeFalse();
    }

    [TestMethod]
    public void Find_ForASourceMountOutsideTheProject_ResolvesItUnderItsBoundFolder()
    {
        var location = Project.Find(AssetPath.Parse("/Shared/Wood.omat.json"));

        _ = location!.File.Should().Be(FileSystemPath.From(@"D:\Library\Wood.omat.json"));
        _ = location.MountKind.Should().Be(ProjectMountKind.Authoring);
    }

    [TestMethod]
    public void Find_ForACookedLibrary_ResolvesItUnderItsFolder()
    {
        var location = Project.Find(AssetPath.Parse("/Vendor/Wood.omat"));

        _ = location!.File.Should().Be(FileSystemPath.From(@"E:\Vendor\Wood.omat"));
        _ = location.MountKind.Should().Be(ProjectMountKind.Cooked);
    }

    [TestMethod]
    public void Find_ForAnUnboundMount_IsNull()
        => _ = Project.Find(AssetPath.Parse("/Studio/Wood.omat.json")).Should().BeNull();

    [TestMethod]
    public void Find_ForTheMount_ResolvesItsFolder()
    {
        var location = Project.Find(AssetPath.Content);

        _ = location!.File.Should().Be(FileSystemPath.From(@"C:\Games\Demo\Content"));
        _ = location.IsMountRoot.Should().BeTrue();
    }

    [TestMethod]
    public void Find_ForAnUnknownMount_IsNull()
        => _ = Project.Find(AssetPath.Parse("/Unknown/Wood.omat.json")).Should().BeNull();

    [TestMethod]
    public void FindByFilePath_GivesTheVirtualPath()
    {
        var location = Project.FindByFilePath(FileSystemPath.From(@"C:\Games\Demo\Content\New folder (2)\Wood mat.omat.json"));

        _ = location!.Path.Should().Be(AssetPath.Parse("/Content/New folder (2)/Wood mat.omat.json"));
        _ = location.Path.ToUriString().Should().Be("asset:///Content/New%20folder%20%282%29/Wood%20mat.omat.json");
    }

    [TestMethod]
    public void FindByFilePath_InASecondSourceMount_IsNamedFromIt()
    {
        var location = Project.FindByFilePath(FileSystemPath.From(@"C:\Games\Demo\Levels\Intro.oscene.json"));

        _ = location!.Path.Should().Be(AssetPath.Parse("/Levels/Intro.oscene.json"));
    }

    [TestMethod]
    public void FindByFilePath_InASourceMountOutsideTheProject_IsNamedFromIt()
        => _ = Project.FindByFilePath(FileSystemPath.From(@"D:\Library\Rock.omat.json"))!.Path.Should().Be(AssetPath.Parse("/Shared/Rock.omat.json"));

    [TestMethod]
    public void FindByFilePath_ForTheMountsFolder_IsTheMountRoot()
    {
        var location = Project.FindByFilePath(FileSystemPath.From(@"C:\Games\Demo\Content\"));

        _ = location!.IsMountRoot.Should().BeTrue();
        _ = location.Path.Should().Be(AssetPath.Content);
    }

    [TestMethod]
    [DataRow(@"E:\Elsewhere\Wood.omat.json", DisplayName = "Another drive")]
    [DataRow(@"C:\Games\Demo\Content\CON.omat.json", DisplayName = "A name the rules refuse")]
    public void FindByFilePath_OutsideTheMounts_IsNull(string path)
        => _ = Project.FindByFilePath(FileSystemPath.From(path)).Should().BeNull();

    [TestMethod]
    [DataRow(@"C:\Games\Demo\project.oxy", "/project/project.oxy", DisplayName = "Project file")]
    [DataRow(@"C:\Games\Demo\ContentOther\Wood.omat.json", "/project/ContentOther/Wood.omat.json", DisplayName = "Sibling with the mount's prefix")]
    [DataRow(@"C:\Games\Demo\Config", "/project/Config", DisplayName = "A folder at the root")]
    public void FindByFilePath_UnderTheRootOutsideTheOtherMounts_IsAProjectFile(string path, string expected)
    {
        var location = Project.FindByFilePath(FileSystemPath.From(path));

        _ = location!.Path.Should().Be(AssetPath.Parse(expected));
        _ = location.MountKind.Should().Be(ProjectMountKind.ProjectFiles);
        _ = location.MountRoot.Should().Be(Project.ProjectRoot);
    }

    [TestMethod]
    public void Find_ForAProjectFilesFolder_ResolvesItUnderTheRoot()
    {
        var location = Project.Find(AssetPath.Parse("/project/Config"));

        _ = location!.File.Should().Be(FileSystemPath.From(@"C:\Games\Demo\Config"));
        _ = location.MountKind.Should().Be(ProjectMountKind.ProjectFiles);
        _ = location.IsMountRoot.Should().BeFalse();
    }

    [TestMethod]
    [DataRow("project", DisplayName = "The project's own files")]
    [DataRow("Project", DisplayName = "Its name in another case")]
    [DataRow("Engine", DisplayName = "The engine's content")]
    [DataRow("Cooked", DisplayName = "The Cooked output folder node")]
    [DataRow("build", DisplayName = "The Build output folder node in another case")]
    public void ValidateDeclaredMountName_AMountEveryProjectHas_IsRefused(string name)
        => _ = ProjectAssetFiles.ValidateDeclaredMountName(name).Should().NotBeNull();

    [TestMethod]
    public void ValidateDeclaredMountName_AnAuthoredName_IsAccepted()
        => _ = ProjectAssetFiles.ValidateDeclaredMountName("Levels").Should().BeNull();

    [TestMethod]
    public void FindByFilePath_InsideAnotherMount_IsNamedFromThatMount()
        => _ = Project.FindByFilePath(FileSystemPath.From(@"C:\Games\Demo\Content\Wood.omat.json"))!.Path.Should().Be(AssetPath.Parse("/Content/Wood.omat.json"));

    [TestMethod]
    public void FindByFilePath_RoundTripsThroughTheVirtualPath()
    {
        var path = FileSystemPath.From(@"C:\Games\Demo\Content\Materials\Wood.omat.json");

        var location = Project.Find(Project.FindByFilePath(path)!.Path);

        _ = location!.File.Should().Be(path);
    }

    [TestMethod]
    public void FindSource_OfAnIdentity_IsTheSourceThatExists()
    {
        var fileSystem = new MockFileSystem();
        _ = fileSystem.Directory.CreateDirectory(@"C:\Games\Demo\Content\Scripts");
        fileSystem.File.WriteAllText(@"C:\Games\Demo\Content\Scripts\Orbit.luau", string.Empty);
        _ = fileSystem.Directory.CreateDirectory(@"C:\Games\Demo\Content\Materials");
        fileSystem.File.WriteAllText(@"C:\Games\Demo\Content\Materials\Wood.omat.json", "{}");

        var project = Project with { FileSystem = fileSystem };

        _ = project.FindSource(AssetPath.Parse("/Content/Scripts/Orbit.oscript"))!.Path.Should().Be(AssetPath.Parse("/Content/Scripts/Orbit.luau"));
        _ = project.FindSource(AssetPath.Parse("/Content/Materials/Wood.omat"))!.Path.Should().Be(AssetPath.Parse("/Content/Materials/Wood.omat.json"));
        _ = project.FindSource(AssetPath.Parse("/Content/Materials/Gone.omat")).Should().BeNull();
    }

    [TestMethod]
    public void SourceMountPathOf_NamesAFileByItsMountWhereverTheMountIs()
    {
        _ = Project.SourceMountPathOf(FileSystemPath.From(@"C:\Games\Demo\Content\Models\My Car.FBX")).Should().Be("/Content/Models/My Car.FBX");
        _ = Project.SourceMountPathOf(FileSystemPath.From(@"D:\Library\Trees\Oak.omat.json")).Should().Be("/Shared/Trees/Oak.omat.json");
    }

    [TestMethod]
    public void SourceMountPathOf_AFileOutsideEverySourceMount_IsNull()
    {
        _ = Project.SourceMountPathOf(FileSystemPath.From(@"C:\Games\Demo\Imports\Content\Car.glb.import.json")).Should().BeNull("records are project files, not sources");
        _ = Project.SourceMountPathOf(FileSystemPath.From(@"E:\Vendor\Wood.omat")).Should().BeNull("a cooked library is not a source mount");
        _ = Project.SourceMountPathOf(FileSystemPath.From(@"C:\Games\Demo\Content")).Should().BeNull("a mount's own folder is not a file in it");
    }

    [TestMethod]
    public void SourceFileOf_ResolvesAMountPathThroughTheBoundFolder()
    {
        _ = Project.SourceFileOf("/Shared/Trees/Oak.omat.json").Should().Be(FileSystemPath.From(@"D:\Library\Trees\Oak.omat.json"));
        _ = Project.SourceFileOf("/Content").Should().Be(FileSystemPath.From(@"C:\Games\Demo\Content"));
        _ = Project.LocateSourceMountPath("/Shared/Trees/Oak.omat.json")!.Value.PathBelow.Should().Be("Trees/Oak.omat.json");
    }

    [TestMethod]
    [DataRow("/Studio/Wood.omat.json")]
    [DataRow("/Vendor/Wood.omat")]
    [DataRow("/Unknown/Wood.omat.json")]
    [DataRow("Content/Wood.omat.json")]
    [DataRow("/")]
    public void SourceFileOf_AMountPathOfNoBoundSourceMount_IsNull(string mountPath)
        => _ = Project.SourceFileOf(mountPath).Should().BeNull();

    [TestMethod]
    public void AuthoredFolders_AreTheProjectRootAndTheSourceMountsOutsideIt()
        => _ = Project.AuthoredFolders.Should().Equal(FileSystemPath.From(@"C:\Games\Demo"), FileSystemPath.From(@"D:\Library"));

    // Content and Levels in the project; Shared, a source mount outside it, and Vendor, a cooked library, bound by
    // the project's .mounts.json; Studio, a placeholder nothing binds.
    private static ProjectContext CreateProject()
    {
        var fileSystem = new MockFileSystem();
        foreach (var folder in new[] { @"C:\Games\Demo\Content", @"C:\Games\Demo\Levels", @"D:\Library", @"E:\Vendor" })
        {
            _ = fileSystem.Directory.CreateDirectory(folder);
        }

        _ = TestProject.BindMounts(fileSystem, FileSystemPath.From(@"C:\Games\Demo"), ("Shared", @"D:\Library"), ("Vendor", @"E:\Vendor"));
        var info = new ProjectInfo("Demo", Category.Games, @"C:\Games\Demo")
        {
            Mounts =
            [
                new ProjectMount("Content", MountKind.Source, "Content"),
                new ProjectMount("Levels", MountKind.Source, "Levels"),
                new ProjectMount("Shared"),
                new ProjectMount("Vendor", MountKind.Cooked),
                new ProjectMount("Studio"),
            ],
        };
        return ProjectContext.FromProjectInfo(info, fileSystem);
    }
}
