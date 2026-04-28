// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentBrowser.Panes.Assets.Layouts;

namespace Oxygen.Editor.ContentBrowser.Tests;

[TestClass]
public sealed class AssetsLayoutViewModelTests
{
    [TestMethod]
    public void NormalizeSelectedFolders_WhenRootAndConcreteFolderAreSelected_ShouldDropRoot()
    {
        var folders = AssetsLayoutViewModel.NormalizeSelectedFolders(["/", "/Content/Materials"]);

        Assert.AreEqual(1, folders.Count);
        Assert.AreEqual("/Content/Materials", folders[0]);
    }

    [TestMethod]
    public void IsInSelectedFolders_WhenConcreteFolderIsSelected_ShouldOnlyMatchThatFolder()
    {
        var folders = AssetsLayoutViewModel.NormalizeSelectedFolders(["/", "/Content/Materials"]);

        Assert.IsTrue(AssetsLayoutViewModel.IsInSelectedFolders(
            "/Content/Materials/Red.omat.json",
            "/Content/Materials/Red.omat.json",
            folders,
            hasActiveProject: true));
        Assert.IsFalse(AssetsLayoutViewModel.IsInSelectedFolders(
            "/Content/Scenes/Default.oscene.json",
            "/Content/Scenes/Default.oscene.json",
            folders,
            hasActiveProject: true));
    }

    [TestMethod]
    public void IsInSelectedFolders_WhenCookedRootIsSelected_ShouldMatchCookedProjectionOnly()
    {
        var folders = AssetsLayoutViewModel.NormalizeSelectedFolders(["/Cooked"]);

        Assert.IsTrue(AssetsLayoutViewModel.IsInSelectedFolders(
            "/Content/Materials/Red.omat.json",
            "/Content/Materials/Red.omat.json",
            folders,
            hasActiveProject: true,
            cookedAbsolutePath: "/Content/Materials/Red.omat",
            hasCookedProjection: true));
        Assert.IsFalse(AssetsLayoutViewModel.IsInSelectedFolders(
            "/Content/Materials/Uncooked.omat.json",
            "/Content/Materials/Uncooked.omat.json",
            folders,
            hasActiveProject: true,
            cookedAbsolutePath: "/Content/Materials/Uncooked.omat",
            hasCookedProjection: false));
    }

    [TestMethod]
    public void IsInSelectedFolders_WhenCookedSubfolderIsSelected_ShouldMatchRuntimeCookedPath()
    {
        var folders = AssetsLayoutViewModel.NormalizeSelectedFolders(["/Cooked/Content/Materials"]);

        Assert.IsTrue(AssetsLayoutViewModel.IsInSelectedFolders(
            "/Content/Materials/Red.omat.json",
            "/Content/Materials/Red.omat.json",
            folders,
            hasActiveProject: true,
            cookedAbsolutePath: "/Content/Materials/Red.omat",
            hasCookedProjection: true));
        Assert.IsFalse(AssetsLayoutViewModel.IsInSelectedFolders(
            "/Content/Scenes/Main.oscene.json",
            "/Content/Scenes/Main.oscene.json",
            folders,
            hasActiveProject: true,
            cookedAbsolutePath: "/Content/Scenes/Main.oscene",
            hasCookedProjection: true));
    }
}
