// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.ContentPipeline.TestSupport;
using Oxygen.Editor.World;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Compatibility;
using static Oxygen.Editor.ContentPipeline.TestSupport.CookScenario;

namespace Oxygen.Editor.ContentPipeline.Integration.Tests.Inspection;

/// <summary>
/// A source's structure is read from its current cooked scene through the installed Inspector (scene instances 8.2):
/// none before the cook, one copy while the cook stands, and a fresh one once the source is republished.
/// </summary>
[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed class SceneStructureProviderTests
{
    private const string SceneFile = "Content/Scenes/Main.oscene.json";

    private static readonly AssetPath SceneUri = AssetPath.Parse("/Content/Scenes/Main.oscene");

    public TestContext TestContext { get; set; } = null!;

    /// <summary>The structure follows the source's publications.</summary>
    /// <returns>The asynchronous cooks and reads.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task TheStructureFollowsTheSourcesPublications()
    {
        using var workspace = new CookWorkspace(engineContent: EngineContentPak.Installed());
        var tower = new SceneNode(workspace.Scene, "Tower");
        workspace.Scene.RootNodes.Add(tower);
        await workspace.WriteSceneAsync(SceneFile).ConfigureAwait(false);
        var compatibility = EditorNativeCompatibilityService.ForCooking();
        using var lifetime = compatibility;
        var api = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), new ContentPipelineProcessRunner(), NullLogger<ImportToolContentPipelineApi>.Instance, compatibility);
        var service = CreateService(workspace, api, compatibility: compatibility);
        ISceneStructureProvider provider = service;
        var project = workspace.ProjectContext;

        _ = (await provider.ReadAsync(project, SceneUri, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().BeNull("an uncooked source has no structure");

        await this.CookAsync(service).ConfigureAwait(false);
        var cooked = await provider.ReadAsync(project, SceneUri, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = cooked.Should().NotBeNull();
        var node = cooked!.Find(NodePath.Of(tower.Id))!;
        _ = node.Name.Should().Be("Tower");
        _ = node.Parent.Should().BeNull();
        _ = node.Components.Should().Contain(SceneSchema.Transform.Kind);
        _ = node.Properties.Should().Contain(property => property.Property == SceneSchema.Transform.PositionX.Id && property.Supplier.Key == cooked.Scene.Key);
        _ = (await provider.ReadAsync(project, SceneUri, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().BeSameAs(cooked, "every view reads one copy");

        tower.Metadata.Name = "Watchtower";
        await workspace.WriteSceneAsync(SceneFile).ConfigureAwait(false);
        await this.CookAsync(service).ConfigureAwait(false);
        var republished = await provider.ReadAsync(project, SceneUri, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = republished.Should().NotBeSameAs(cooked);
        _ = republished!.Find(NodePath.Of(tower.Id))!.Name.Should().Be("Watchtower");
    }

    private async Task CookAsync(ContentPipelineService service)
    {
        var result = await service.CookCurrentSceneAsync(SceneUri, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = result.IsPublished.Should().BeTrue(string.Join(Environment.NewLine, result.Diagnostics.Select(static issue => issue.Code + ": " + (issue.TechnicalMessage ?? issue.Message))));
    }
}
