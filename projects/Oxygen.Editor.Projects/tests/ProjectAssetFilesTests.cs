// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;

namespace Oxygen.Editor.Projects.Tests;

/// <summary>Verifies the one mapping between the project's assets and their files.</summary>
[TestClass]
[TestCategory("Unit")]
public sealed class ProjectAssetFilesTests
{
    private const string ProjectRoot = @"C:\Games\Demo";

    private static readonly ProjectContext Project = new()
    {
        ProjectId = Guid.NewGuid(),
        Name = "Demo",
        Category = Category.Games,
        ProjectRoot = ProjectRoot,
        AuthoringMounts = [new ProjectMountPoint("Content", "Content"), new ProjectMountPoint("Levels", @"Content\Levels")],
        LocalFolderMounts = [new LocalFolderMount("Shared", @"D:\Library")],
    };

    [TestMethod]
    public void FindByAssetUri_ForAnAuthoringFile_ResolvesItsFileAndMount()
    {
        var location = Project.FindByAssetUri(new Uri("asset:///Content/Materials/Wood.omat.json"));

        _ = location.Should().NotBeNull();
        _ = location!.FilePath.Should().Be(@"C:\Games\Demo\Content\Materials\Wood.omat.json");
        _ = location.MountName.Should().Be("Content");
        _ = location.MountRelativePath.Should().Be("Materials/Wood.omat.json");
        _ = location.VirtualPath.Should().Be("/Content/Materials/Wood.omat.json");
        _ = location.MountKind.Should().Be(ProjectMountKind.Authoring);
        _ = location.IsMountRoot.Should().BeFalse();
    }

    [TestMethod]
    public void FindByAssetUri_ForALocalFolderFile_ResolvesItUnderTheFolder()
    {
        var location = Project.FindByAssetUri(new Uri("asset:///Shared/Wood.omat.json"));

        _ = location!.FilePath.Should().Be(@"D:\Library\Wood.omat.json");
        _ = location.MountKind.Should().Be(ProjectMountKind.LocalFolder);
    }

    [TestMethod]
    public void FindByAssetUri_ForTheMount_ResolvesItsFolder()
    {
        var location = Project.FindByAssetUri(new Uri("asset:///Content"));

        _ = location!.FilePath.Should().Be(@"C:\Games\Demo\Content");
        _ = location.IsMountRoot.Should().BeTrue();
    }

    [TestMethod]
    [DataRow("asset:///Unknown/Wood.omat.json", DisplayName = "Unknown mount")]
    [DataRow("asset:///Content/../Secrets.json", DisplayName = "Parent segment")]
    [DataRow("asset:///Content/%2E%2E/Secrets.json", DisplayName = "Escaped parent segment")]
    [DataRow("asset:///Content/Materials//Wood.omat.json", DisplayName = "Empty segment")]
    [DataRow("asset:///Content/C:/Windows/win.ini", DisplayName = "Rooted segment")]
    [DataRow("file:///Content/Wood.omat.json", DisplayName = "Not an asset URI")]
    public void FindByAssetUri_ForAPlaceOutsideTheMounts_IsNull(string uri)
        => _ = Project.FindByAssetUri(new Uri(uri)).Should().BeNull();

    [TestMethod]
    public void FindByFilePath_WritesTheCatalogsUri()
    {
        var location = Project.FindByFilePath(@"C:\Games\Demo\Content\New folder (2)\Wood mat.omat.json");

        _ = location!.AssetUri.Should().Be(new Uri("asset:///Content/New folder (2)/Wood mat.omat.json"));
        _ = location.AssetUri.AbsoluteUri.Should().Be("asset:///Content/New%20folder%20(2)/Wood%20mat.omat.json");
    }

    [TestMethod]
    public void FindByFilePath_InNestedMounts_ChoosesTheInnermost()
    {
        var location = Project.FindByFilePath(@"C:\Games\Demo\Content\Levels\Intro.oscene.json");

        _ = location!.MountName.Should().Be("Levels");
        _ = location.VirtualPath.Should().Be("/Levels/Intro.oscene.json");
    }

    [TestMethod]
    public void FindByFilePath_ForTheMountsFolder_IsTheMountRoot()
    {
        var location = Project.FindByFilePath(@"C:\Games\Demo\Content\");

        _ = location!.IsMountRoot.Should().BeTrue();
        _ = location.VirtualPath.Should().Be("/Content");
    }

    [TestMethod]
    [DataRow(@"C:\Games\Demo\project.oxy", DisplayName = "Project file")]
    [DataRow(@"C:\Games\Demo\ContentOther\Wood.omat.json", DisplayName = "Sibling with the mount's prefix")]
    [DataRow(@"E:\Elsewhere\Wood.omat.json", DisplayName = "Another drive")]
    public void FindByFilePath_OutsideTheMounts_IsNull(string path)
        => _ = Project.FindByFilePath(path).Should().BeNull();

    [TestMethod]
    public void FindByFilePath_RoundTripsThroughTheAssetUri()
    {
        var path = @"C:\Games\Demo\Content\Materials\Wood.omat.json";

        var location = Project.FindByAssetUri(Project.FindByFilePath(path)!.AssetUri);

        _ = location!.FilePath.Should().Be(path);
    }

    [TestMethod]
    public void FindByVirtualPath_ResolvesLikeTheAssetUri()
    {
        var location = Project.FindByVirtualPath("/Content/Materials/Wood.omat.json");

        _ = location.Should().Be(Project.FindByAssetUri(new Uri("asset:///Content/Materials/Wood.omat.json")));
    }

    [TestMethod]
    [DataRow("Content/Wood.omat.json", DisplayName = "No leading slash")]
    [DataRow("/", DisplayName = "No mount")]
    [DataRow("/Content/../Wood.omat.json", DisplayName = "Parent segment")]
    [DataRow("/Content/./Wood.omat.json", DisplayName = "Current segment")]
    [DataRow("/Content/Materials//Wood.omat.json", DisplayName = "Empty segment")]
    public void FindByVirtualPath_ForAPlaceOutsideTheMounts_IsNull(string virtualPath)
        => _ = Project.FindByVirtualPath(virtualPath).Should().BeNull();
}
