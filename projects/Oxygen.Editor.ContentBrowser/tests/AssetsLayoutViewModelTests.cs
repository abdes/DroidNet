// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.ContentBrowser.Panes.Assets.Layouts;

namespace Oxygen.Editor.ContentBrowser.Tests;

[TestClass]
public sealed class AssetsLayoutViewModelTests
{
    /// <summary>A renamed derived mount filters cooked assets while an ordinary Cooked folder remains authored.</summary>
    [TestMethod]
    public void RenamedCookedMountUsesItsSavedAlias()
    {
        _ = AssetsLayoutViewModel.IsInSelectedFolders(
            "/Content/Materials/Red.omat.json",
            "/Content/Materials/Red.omat.json",
            ["/Published/Content/Materials"],
            hasActiveProject: true,
            cookedAbsolutePath: "/Content/Materials/Red.omat",
            hasCookedProjection: true,
            cookedFolders: ["/Published"]).Should().BeTrue();
        _ = AssetsLayoutViewModel.IsInSelectedFolders(
            "/Content/Materials/Red.omat.json",
            "/Content/Materials/Red.omat.json",
            ["/Published/Content/Materials"],
            hasActiveProject: true,
            cookedAbsolutePath: "/Content/Materials/Red.omat",
            hasCookedProjection: false,
            cookedFolders: ["/Published"]).Should().BeFalse();
        _ = AssetsLayoutViewModel.IsInSelectedFolders(
            "/Cooked/Red.omat.json",
            "/Cooked/Red.omat.json",
            ["/Cooked"],
            hasActiveProject: true,
            cookedFolders: ["/Published"]).Should().BeTrue();
    }

    [TestMethod]
    public void NormalizeSelectedFolders_WhenRootAndConcreteFolderAreSelected_ShouldDropRoot()
    {
        var folders = AssetsLayoutViewModel.NormalizeSelectedFolders(["/", "/Content/Materials"]);

        _ = folders.Should().ContainSingle();
        _ = folders[0].Should().Be("/Content/Materials");
    }

    [TestMethod]
    public void IsInSelectedFolders_WhenConcreteFolderIsSelected_ShouldOnlyMatchThatFolder()
    {
        var folders = AssetsLayoutViewModel.NormalizeSelectedFolders(["/", "/Content/Materials"]);

        _ = AssetsLayoutViewModel.IsInSelectedFolders(
            "/Content/Materials/Red.omat.json",
            "/Content/Materials/Red.omat.json",
            folders,
            hasActiveProject: true).Should().BeTrue();
        _ = AssetsLayoutViewModel.IsInSelectedFolders(
            "/Content/Scenes/Default.oscene.json",
            "/Content/Scenes/Default.oscene.json",
            folders,
            hasActiveProject: true).Should().BeFalse();
    }

    [TestMethod]
    public void IsInSelectedFolders_WhenCookedRootIsSelected_ShouldMatchCookedProjectionOnly()
    {
        var folders = AssetsLayoutViewModel.NormalizeSelectedFolders(["/Cooked"]);

        _ = AssetsLayoutViewModel.IsInSelectedFolders(
            "/Content/Materials/Red.omat.json",
            "/Content/Materials/Red.omat.json",
            folders,
            hasActiveProject: true,
            cookedAbsolutePath: "/Content/Materials/Red.omat",
            hasCookedProjection: true,
            cookedFolders: ["/Cooked"]).Should().BeTrue();
        _ = AssetsLayoutViewModel.IsInSelectedFolders(
            "/Content/Materials/Uncooked.omat.json",
            "/Content/Materials/Uncooked.omat.json",
            folders,
            hasActiveProject: true,
            cookedAbsolutePath: "/Content/Materials/Uncooked.omat",
            hasCookedProjection: false,
            cookedFolders: ["/Cooked"]).Should().BeFalse();
    }

    [TestMethod]
    public void IsInSelectedFolders_WhenCookedSubfolderIsSelected_ShouldMatchRuntimeCookedPath()
    {
        var folders = AssetsLayoutViewModel.NormalizeSelectedFolders(["/Cooked/Content/Materials"]);

        _ = AssetsLayoutViewModel.IsInSelectedFolders(
            "/Content/Materials/Red.omat.json",
            "/Content/Materials/Red.omat.json",
            folders,
            hasActiveProject: true,
            cookedAbsolutePath: "/Content/Materials/Red.omat",
            hasCookedProjection: true,
            cookedFolders: ["/Cooked"]).Should().BeTrue();
        _ = AssetsLayoutViewModel.IsInSelectedFolders(
            "/Content/Scenes/Main.oscene.json",
            "/Content/Scenes/Main.oscene.json",
            folders,
            hasActiveProject: true,
            cookedAbsolutePath: "/Content/Scenes/Main.oscene",
            hasCookedProjection: true,
            cookedFolders: ["/Cooked"]).Should().BeFalse();
    }
}
