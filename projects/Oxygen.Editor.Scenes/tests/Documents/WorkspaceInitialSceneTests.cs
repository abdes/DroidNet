// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Moq;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Editor.Scenes.Documents;
using Oxygen.Editor.Scenes.Testing;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Core;
using Testably.Abstractions.Testing;

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
        var context = scenes.Project.Context;

        _ = (await SceneFiles.ResolveSceneByNameOrIdAsync(CatalogOf(scenes), context, "Alpha").ConfigureAwait(false))!.File.Should().Be(scenes.FileOf("Alpha").Path);
        _ = (await SceneFiles.ResolveSceneByNameOrIdAsync(CatalogOf(scenes), context, "Beta.oscene.json").ConfigureAwait(false))!.File.Should().Be(scenes.FileOf("Beta").Path);
        _ = (await SceneFiles.ResolveSceneByNameOrIdAsync(CatalogOf(scenes), context, GammaId.Value.ToString("D")).ConfigureAwait(false))!.File.Should().Be(scenes.FileOf("Levels/Gamma").Path);
    }

    [TestMethod]
    public async Task ResolveSceneByNameOrId_ReturnsNullForBlankOrUnknown()
    {
        using var scenes = CreateScenes();
        var context = scenes.Project.Context;

        _ = (await SceneFiles.ResolveSceneByNameOrIdAsync(CatalogOf(scenes), context, "   ").ConfigureAwait(false)).Should().BeNull();
        _ = (await SceneFiles.ResolveSceneByNameOrIdAsync(CatalogOf(scenes), context, "Not a scene").ConfigureAwait(false)).Should().BeNull();
        _ = (await SceneFiles.ResolveSceneByNameOrIdAsync(CatalogOf(scenes), context, Guid.NewGuid().ToString("D")).ConfigureAwait(false)).Should().BeNull();
    }

    [TestMethod]
    public async Task ResolveInitialScene_ConfiguredStartSceneIsFoundByIdentityInAnyFolder()
    {
        using var scenes = CreateScenes();

        var initial = await SceneFiles.ResolveInitialSceneAsync(CatalogOf(scenes), scenes.Project.Context with { DefaultSceneId = GammaId }).ConfigureAwait(false);

        _ = initial.Should().Be(scenes.FileOf("Levels/Gamma").Asset);
    }

    [TestMethod]
    public async Task ResolveInitialScene_RequestedSceneWinsOverTheStartScene()
    {
        using var scenes = CreateScenes();
        var requested = scenes.FileOf("Beta").Asset;

        var initial = await SceneFiles.ResolveInitialSceneAsync(CatalogOf(scenes), scenes.Project.Context with { DefaultSceneId = AlphaId, InitialScene = requested }).ConfigureAwait(false);

        _ = initial.Should().Be(requested);
    }

    [TestMethod]
    public async Task ResolveInitialScene_MissingStartSceneDoesNotFallBackToAnotherScene()
    {
        using var scenes = CreateScenes();

        _ = (await SceneFiles.ResolveInitialSceneAsync(CatalogOf(scenes), scenes.Project.Context with { DefaultSceneId = SceneId.New() }).ConfigureAwait(false)).Should().BeNull();
        _ = (await SceneFiles.ResolveInitialSceneAsync(CatalogOf(scenes), scenes.Project.Context).ConfigureAwait(false)).Should().BeNull();
    }

    private static TestSceneDocuments CreateScenes()
    {
        var scenes = new TestSceneDocuments(TestProject.Create(new MockFileSystem()));
        _ = scenes.Write(new SceneData { Id = AlphaId, Name = "Alpha" });
        _ = scenes.Write(new SceneData { Id = BetaId, Name = "Beta" });
        scenes.Write(scenes.FileOf("Levels/Gamma"), new SceneData { Id = GammaId, Name = "Gamma" });
        return scenes;
    }

    // The catalog lists the scene files and one file that is not a scene.
    private static IAssetCatalog CatalogOf(TestSceneDocuments scenes)
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
