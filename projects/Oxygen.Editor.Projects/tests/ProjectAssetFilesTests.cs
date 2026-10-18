// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Projects.Tests;

/// <summary>Verifies the one mapping between the project's virtual paths and their files.</summary>
[TestClass]
[TestCategory("Unit")]
public sealed class ProjectAssetFilesTests
{
    private static readonly ProjectContext Project = new()
    {
        ProjectId = Guid.NewGuid(),
        Name = "Demo",
        Category = Category.Games,
        ProjectRoot = FileSystemPath.From(@"C:\Games\Demo"),
        AuthoringMounts = [new ProjectMountPoint("Content", "Content"), new ProjectMountPoint("Levels", @"Content\Levels")],
        LocalFolderMounts = [new LocalFolderMount("Shared", FileSystemPath.From(@"D:\Library"))],
    };

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
    public void Find_ForALocalFolderFile_ResolvesItUnderTheFolder()
    {
        var location = Project.Find(AssetPath.Parse("/Shared/Wood.omat.json"));

        _ = location!.File.Should().Be(FileSystemPath.From(@"D:\Library\Wood.omat.json"));
        _ = location.MountKind.Should().Be(ProjectMountKind.LocalFolder);
    }

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
    public void FindByFilePath_InNestedMounts_ChoosesTheInnermost()
    {
        var location = Project.FindByFilePath(FileSystemPath.From(@"C:\Games\Demo\Content\Levels\Intro.oscene.json"));

        _ = location!.Path.Should().Be(AssetPath.Parse("/Levels/Intro.oscene.json"));
    }

    [TestMethod]
    public void FindByFilePath_ForTheMountsFolder_IsTheMountRoot()
    {
        var location = Project.FindByFilePath(FileSystemPath.From(@"C:\Games\Demo\Content\"));

        _ = location!.IsMountRoot.Should().BeTrue();
        _ = location.Path.Should().Be(AssetPath.Content);
    }

    [TestMethod]
    [DataRow(@"C:\Games\Demo\project.oxy", DisplayName = "Project file")]
    [DataRow(@"C:\Games\Demo\ContentOther\Wood.omat.json", DisplayName = "Sibling with the mount's prefix")]
    [DataRow(@"E:\Elsewhere\Wood.omat.json", DisplayName = "Another drive")]
    [DataRow(@"C:\Games\Demo\Content\CON.omat.json", DisplayName = "A name the rules refuse")]
    public void FindByFilePath_OutsideTheMounts_IsNull(string path)
        => _ = Project.FindByFilePath(FileSystemPath.From(path)).Should().BeNull();

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
        var root = Directory.CreateTempSubdirectory("ProjectAssetFiles").FullName;
        try
        {
            var project = Project with { ProjectRoot = FileSystemPath.From(root) };
            _ = Directory.CreateDirectory(Path.Combine(root, "Content", "Scripts"));
            File.WriteAllText(Path.Combine(root, "Content", "Scripts", "Orbit.luau"), string.Empty);
            _ = Directory.CreateDirectory(Path.Combine(root, "Content", "Materials"));
            File.WriteAllText(Path.Combine(root, "Content", "Materials", "Wood.omat.json"), "{}");

            _ = project.FindSource(AssetPath.Parse("/Content/Scripts/Orbit.oscript"))!.Path.Should().Be(AssetPath.Parse("/Content/Scripts/Orbit.luau"));
            _ = project.FindSource(AssetPath.Parse("/Content/Materials/Wood.omat"))!.Path.Should().Be(AssetPath.Parse("/Content/Materials/Wood.omat.json"));
            _ = project.FindSource(AssetPath.Parse("/Content/Materials/Gone.omat")).Should().BeNull();
        }
        finally
        {
            Directory.Delete(root, recursive: true);
        }
    }
}
