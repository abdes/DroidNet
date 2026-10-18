// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.ContentBrowser.Panes.Assets.Layouts;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentBrowser.Tests;

[TestClass]
public sealed class AssetsLayoutViewModelTests
{
    /// <summary>A renamed derived mount filters cooked assets while an ordinary Cooked folder remains authored.</summary>
    [TestMethod]
    public void RenamedCookedMountUsesItsSavedAlias()
    {
        _ = AssetsLayoutViewModel.IsInSelectedFolders(
            "/Content/Materials/Red.omat",
            AssetPath.Parse("/Content/Materials/Red.omat"),
            [AssetPath.Parse("/Published/Content/Materials")],
            hasActiveProject: true,
            hasCookedProjection: true,
            cookedFolders: [AssetPath.Parse("/Published")]).Should().BeTrue();
        _ = AssetsLayoutViewModel.IsInSelectedFolders(
            "/Content/Materials/Red.omat",
            AssetPath.Parse("/Content/Materials/Red.omat"),
            [AssetPath.Parse("/Published/Content/Materials")],
            hasActiveProject: true,
            hasCookedProjection: false,
            cookedFolders: [AssetPath.Parse("/Published")]).Should().BeFalse();
        _ = AssetsLayoutViewModel.IsInSelectedFolders(
            "/Cooked/Red.omat",
            AssetPath.Parse("/Cooked/Red.omat"),
            [AssetPath.Parse("/Cooked")],
            hasActiveProject: true,
            cookedFolders: [AssetPath.Parse("/Published")]).Should().BeTrue();
    }

    [TestMethod]
    public void IsInSelectedFolders_WhenNoFolderIsSelected_ShouldMatchTheWholeProject()
    {
        _ = AssetsLayoutViewModel.IsInSelectedFolders(
            "/Content/Scenes/Default.oscene",
            AssetPath.Parse("/Content/Scenes/Default.oscene"),
            [],
            hasActiveProject: true).Should().BeTrue();
    }

    [TestMethod]
    public void IsInSelectedFolders_WhenConcreteFolderIsSelected_ShouldOnlyMatchThatFolder()
    {
        AssetPath[] folders = [AssetPath.Parse("/Content/Materials")];

        _ = AssetsLayoutViewModel.IsInSelectedFolders(
            "/Content/Materials/Red.omat",
            AssetPath.Parse("/Content/Materials/Red.omat"),
            folders,
            hasActiveProject: true).Should().BeTrue();
        _ = AssetsLayoutViewModel.IsInSelectedFolders(
            "/Content/Scenes/Default.oscene",
            AssetPath.Parse("/Content/Scenes/Default.oscene"),
            folders,
            hasActiveProject: true).Should().BeFalse();
    }

    [TestMethod]
    public void IsInSelectedFolders_WhenCookedRootIsSelected_ShouldMatchCookedProjectionOnly()
    {
        AssetPath[] folders = [AssetPath.Parse("/Cooked")];

        _ = AssetsLayoutViewModel.IsInSelectedFolders(
            "/Content/Materials/Red.omat",
            AssetPath.Parse("/Content/Materials/Red.omat"),
            folders,
            hasActiveProject: true,
            hasCookedProjection: true,
            cookedFolders: [AssetPath.Parse("/Cooked")]).Should().BeTrue();
        _ = AssetsLayoutViewModel.IsInSelectedFolders(
            "/Content/Materials/Uncooked.omat",
            AssetPath.Parse("/Content/Materials/Uncooked.omat"),
            folders,
            hasActiveProject: true,
            hasCookedProjection: false,
            cookedFolders: [AssetPath.Parse("/Cooked")]).Should().BeFalse();
    }

    [TestMethod]
    public void IsInSelectedFolders_WhenCookedSubfolderIsSelected_ShouldMatchRuntimeCookedPath()
    {
        AssetPath[] folders = [AssetPath.Parse("/Cooked/Content/Materials")];

        _ = AssetsLayoutViewModel.IsInSelectedFolders(
            "/Content/Materials/Red.omat",
            AssetPath.Parse("/Content/Materials/Red.omat"),
            folders,
            hasActiveProject: true,
            hasCookedProjection: true,
            cookedFolders: [AssetPath.Parse("/Cooked")]).Should().BeTrue();
        _ = AssetsLayoutViewModel.IsInSelectedFolders(
            "/Content/Scenes/Main.oscene",
            AssetPath.Parse("/Content/Scenes/Main.oscene"),
            folders,
            hasActiveProject: true,
            hasCookedProjection: true,
            cookedFolders: [AssetPath.Parse("/Cooked")]).Should().BeFalse();
    }
}
