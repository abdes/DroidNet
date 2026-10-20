// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Moq;
using Oxygen.Editor.World.Workspace;
using Oxygen.Editor.WorldEditor.Unit.Tests.Documents;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.Workspace;

/// <summary>Finds the scene a workspace opens first among the project's scene files, in any folder.</summary>
[TestClass]
public sealed class WorkspaceInitialSceneTests
{
    private static readonly SceneId AlphaId = SceneId.New();
    private static readonly SceneId BetaId = SceneId.New();
    private static readonly SceneId GammaId = SceneId.New();

    [TestMethod]
    public async Task ResolveSceneByNameOrId_MatchesByNameFileNameOrId()
    {
        using var scenes = CreateScenes();

        _ = (await WorkspaceViewModel.ResolveSceneByNameOrIdAsync(CatalogOf(scenes), scenes.Context, "Alpha").ConfigureAwait(false))!.File.Should().Be(scenes.PathOf("Alpha"));
        _ = (await WorkspaceViewModel.ResolveSceneByNameOrIdAsync(CatalogOf(scenes), scenes.Context, "Beta.oscene.json").ConfigureAwait(false))!.File.Should().Be(scenes.PathOf("Beta"));
        _ = (await WorkspaceViewModel.ResolveSceneByNameOrIdAsync(CatalogOf(scenes), scenes.Context, GammaId.Value.ToString("D")).ConfigureAwait(false))!.File.Should().Be(scenes.PathOf("Levels/Gamma"));
    }

    [TestMethod]
    public async Task ResolveSceneByNameOrId_ReturnsNullForBlankOrUnknown()
    {
        using var scenes = CreateScenes();

        _ = (await WorkspaceViewModel.ResolveSceneByNameOrIdAsync(CatalogOf(scenes), scenes.Context, "   ").ConfigureAwait(false)).Should().BeNull();
        _ = (await WorkspaceViewModel.ResolveSceneByNameOrIdAsync(CatalogOf(scenes), scenes.Context, "Not a scene").ConfigureAwait(false)).Should().BeNull();
        _ = (await WorkspaceViewModel.ResolveSceneByNameOrIdAsync(CatalogOf(scenes), scenes.Context, Guid.NewGuid().ToString("D")).ConfigureAwait(false)).Should().BeNull();
    }

    [TestMethod]
    public async Task ResolveInitialScene_ConfiguredStartSceneIsFoundByIdentityInAnyFolder()
    {
        using var scenes = CreateScenes();

        var initial = await WorkspaceViewModel.ResolveInitialSceneAsync(CatalogOf(scenes), scenes.Context with { DefaultSceneId = GammaId }).ConfigureAwait(false);

        _ = initial.Should().Be(scenes.FileOf("Levels/Gamma").Asset);
    }

    [TestMethod]
    public async Task ResolveInitialScene_RequestedSceneWinsOverTheStartScene()
    {
        using var scenes = CreateScenes();
        var requested = scenes.FileOf("Beta").Asset;

        var initial = await WorkspaceViewModel.ResolveInitialSceneAsync(CatalogOf(scenes), scenes.Context with { DefaultSceneId = AlphaId, InitialScene = requested }).ConfigureAwait(false);

        _ = initial.Should().Be(requested);
    }

    [TestMethod]
    public async Task ResolveInitialScene_MissingStartSceneDoesNotFallBackToAnotherScene()
    {
        using var scenes = CreateScenes();

        _ = (await WorkspaceViewModel.ResolveInitialSceneAsync(CatalogOf(scenes), scenes.Context with { DefaultSceneId = SceneId.New() }).ConfigureAwait(false)).Should().BeNull();
        _ = (await WorkspaceViewModel.ResolveInitialSceneAsync(CatalogOf(scenes), scenes.Context).ConfigureAwait(false)).Should().BeNull();
    }

    private static SceneDocumentFiles CreateScenes()
    {
        var scenes = new SceneDocumentFiles();
        scenes.Write("Alpha", SceneDocumentFiles.Data(AlphaId, "Alpha"));
        scenes.Write("Beta", SceneDocumentFiles.Data(BetaId, "Beta"));
        scenes.Write("Levels/Gamma", SceneDocumentFiles.Data(GammaId, "Gamma"));
        return scenes;
    }

    // The catalog lists the scene files and one file that is not a scene.
    private static IAssetCatalog CatalogOf(SceneDocumentFiles scenes)
    {
        IReadOnlyList<AssetRecord> records =
        [
            new(scenes.FileOf("Alpha").Asset.WithAuthoringSuffix()),
            new(scenes.FileOf("Beta").Asset.WithAuthoringSuffix()),
            new(scenes.FileOf("Levels/Gamma").Asset.WithAuthoringSuffix()),
            new(AssetPath.Parse("/Content/Wood.omat.json")),
        ];
        var catalog = new Mock<IAssetCatalog>();
        _ = catalog.Setup(value => value.QueryAsync(It.IsAny<AssetQuery>(), It.IsAny<CancellationToken>())).ReturnsAsync(records);
        return catalog.Object;
    }
}
