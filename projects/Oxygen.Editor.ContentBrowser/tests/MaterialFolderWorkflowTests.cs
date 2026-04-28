// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentBrowser;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;

namespace Oxygen.Editor.ContentBrowser.Tests;

[TestClass]
public sealed class MaterialFolderWorkflowTests
{
    [DataTestMethod]
    [DataRow(null, "/Content/Materials")]
    [DataRow("", "/Content/Materials")]
    [DataRow("Scenes", "/Content/Materials")]
    [DataRow("/Scenes", "/Content/Materials")]
    [DataRow("project", "/Content/Materials")]
    [DataRow("/Content", "/Content/Materials")]
    [DataRow("Content", "/Content/Materials")]
    [DataRow("Content/Materials", "/Content/Materials")]
    [DataRow("/Content/Materials", "/Content/Materials")]
    [DataRow("Content/Materials/Subfolder", "/Content/Materials/Subfolder")]
    public void NormalizeMaterialFolder_ShouldOnlyUseContentAuthoringFolders(string? selected, string expected)
    {
        var actual = AssetsViewModel.NormalizeMaterialFolder(selected);

        Assert.AreEqual(expected, actual);
    }

    [TestMethod]
    public void NormalizeMaterialFolder_WhenContentMountUsesDifferentFolder_ShouldReturnContentMountUri()
    {
        var project = new ProjectContext
        {
            ProjectId = Guid.NewGuid(),
            Name = "Test",
            Category = Category.Games,
            ProjectRoot = @"C:\Project",
            AuthoringMounts = [new ProjectMountPoint("Content", "Authoring")],
            LocalFolderMounts = [],
            Scenes = [],
        };

        var actual = AssetsViewModel.NormalizeMaterialFolder("Authoring/Materials/Subfolder", project);

        Assert.AreEqual("/Content/Materials/Subfolder", actual);
    }

    [TestMethod]
    public void CreateMaterialTargetSelection_WhenLocalMountIsSelected_ShouldPassLocalMountName()
    {
        var project = new ProjectContext
        {
            ProjectId = Guid.NewGuid(),
            Name = "Test",
            Category = Category.Games,
            ProjectRoot = @"C:\Project",
            AuthoringMounts = [new ProjectMountPoint("Content", "Content")],
            LocalFolderMounts = [new LocalFolderMount("StudioLibrary", @"D:\Studio\SharedContent")],
            Scenes = [],
        };

        var selection = AssetsViewModel.CreateMaterialTargetSelection(project, "/StudioLibrary/Materials/Shared");

        Assert.AreEqual("/StudioLibrary/Materials/Shared", selection.ProjectRelativeFolder);
        Assert.AreEqual("StudioLibrary", selection.LocalMountName);
    }
}
