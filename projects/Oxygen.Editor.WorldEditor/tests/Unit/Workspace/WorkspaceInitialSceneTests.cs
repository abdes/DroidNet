// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Moq;
using Oxygen.Editor.World.Workspace;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Oxygen.Editor.WorldEditor.Unit.Tests.Documents;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.Workspace;

/// <summary>Finds the scene a workspace opens first among the project's scene files, in any folder.</summary>
[TestClass]
public sealed class WorkspaceInitialSceneTests
{
    private static readonly Guid AlphaId = Guid.NewGuid();
    private static readonly Guid BetaId = Guid.NewGuid();
    private static readonly Guid GammaId = Guid.NewGuid();

    [TestMethod]
    public async Task ResolveSceneByNameOrId_MatchesByNameFileNameOrId()
    {
        using var scenes = CreateScenes();

        _ = (await WorkspaceViewModel.ResolveSceneByNameOrIdAsync(scenes.Context, "Alpha").ConfigureAwait(false))!.FilePath.Should().Be(scenes.PathOf("Alpha"));
        _ = (await WorkspaceViewModel.ResolveSceneByNameOrIdAsync(scenes.Context, "Beta.oscene.json").ConfigureAwait(false))!.FilePath.Should().Be(scenes.PathOf("Beta"));
        _ = (await WorkspaceViewModel.ResolveSceneByNameOrIdAsync(scenes.Context, GammaId.ToString("D")).ConfigureAwait(false))!.FilePath.Should().Be(scenes.PathOf("Levels/Gamma"));
    }

    [TestMethod]
    public async Task ResolveSceneByNameOrId_ReturnsNullForBlankOrUnknown()
    {
        using var scenes = CreateScenes();

        _ = (await WorkspaceViewModel.ResolveSceneByNameOrIdAsync(scenes.Context, "   ").ConfigureAwait(false)).Should().BeNull();
        _ = (await WorkspaceViewModel.ResolveSceneByNameOrIdAsync(scenes.Context, "Not a scene").ConfigureAwait(false)).Should().BeNull();
        _ = (await WorkspaceViewModel.ResolveSceneByNameOrIdAsync(scenes.Context, Guid.NewGuid().ToString("D")).ConfigureAwait(false)).Should().BeNull();
    }

    [TestMethod]
    public async Task ResolveInitialScene_ConfiguredStartSceneIsFoundByIdentityInAnyFolder()
    {
        using var scenes = CreateScenes();

        var initial = await WorkspaceViewModel.ResolveInitialSceneAsync(scenes.Context with { DefaultSceneId = GammaId }).ConfigureAwait(false);

        _ = initial.Should().Be(scenes.FileOf("Levels/Gamma").AssetUri);
    }

    [TestMethod]
    public async Task ResolveInitialScene_RequestedSceneWinsOverTheStartScene()
    {
        using var scenes = CreateScenes();
        var requested = scenes.FileOf("Beta").AssetUri;

        var initial = await WorkspaceViewModel.ResolveInitialSceneAsync(scenes.Context with { DefaultSceneId = AlphaId, InitialSceneAssetUri = requested }).ConfigureAwait(false);

        _ = initial.Should().Be(requested);
    }

    [TestMethod]
    public async Task ResolveInitialScene_MissingStartSceneDoesNotFallBackToAnotherScene()
    {
        using var scenes = CreateScenes();

        _ = (await WorkspaceViewModel.ResolveInitialSceneAsync(scenes.Context with { DefaultSceneId = Guid.NewGuid() }).ConfigureAwait(false)).Should().BeNull();
        _ = (await WorkspaceViewModel.ResolveInitialSceneAsync(scenes.Context).ConfigureAwait(false)).Should().BeNull();
    }

    private static SceneDocumentFiles CreateScenes()
    {
        var scenes = new SceneDocumentFiles(Mock.Of<ISceneDocumentCommandService>());
        scenes.Write("Alpha", SceneDocumentFiles.Data(AlphaId, "Alpha"));
        scenes.Write("Beta", SceneDocumentFiles.Data(BetaId, "Beta"));
        scenes.Write("Levels/Gamma", SceneDocumentFiles.Data(GammaId, "Gamma"));
        return scenes;
    }
}
