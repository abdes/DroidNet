// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Snapshots;

namespace Oxygen.Editor.ContentPipeline.Tests;

/// <summary>Qualifies active-preview requests through saved-input capture and native publication.</summary>
public sealed partial class ContentPipelineServiceTests
{
    /// <summary>A material demand joins its paused Save without saving or cooking its dirty consuming scene.</summary>
    /// <returns>The asynchronous native preview-demand regression.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task PreviewMaterialSharesSavedScopeWithoutIncludingDirtyConsumingScene()
    {
        using var workspace = new TempWorkspace();
        await PrepareIncrementalSceneAsync(workspace).ConfigureAwait(false);
        using var compatibility = Oxygen.Testing.TemporaryNativeArtifacts.ForInstalledEngine();
        var pipeline = CreateIncrementalService(workspace, CreateRecordingApi(compatibility), compatibility);
        var materialUri = new Uri("asset:///Content/Materials/Blue.omat.json");
        var scenePath = Path.Combine(workspace.Root, "Content", "Scenes", "Main.oscene.json");
        var scene = SavedState(scenePath) with { Revision = 2, IsDirty = true };
        using var registration = workspace.Documents.Register(scenePath, _ => Task.FromResult<CookDocumentReadLease?>(new(scene, static () => { })));
        workspace.CookCoordinator.IsAutomaticCookingPaused = true;
        var saved = pipeline.CookSavedAssetAsync(materialUri, workspace.ProjectContext, this.TestContext.CancellationToken);
        var preview = pipeline.CookPreviewAssetAsync(materialUri, workspace.ProjectContext, this.TestContext.CancellationToken);
        var queued = workspace.CookCoordinator.Runs.Should().ContainSingle().Subject;
        _ = queued.Request.IsDemand.Should().BeTrue();
        _ = queued.Request.IsAutomatic.Should().BeTrue();
        _ = preview.IsCompleted.Should().BeFalse();
        workspace.CookCoordinator.IsAutomaticCookingPaused = false;
        var result = await preview.ConfigureAwait(false);
        AssertCookSucceeded(result);
        _ = (await saved.ConfigureAwait(false)).Should().BeSameAs(result);
        _ = result.InputSnapshot.Should().NotBeNull();
        var snapshot = result.InputSnapshot!;
        _ = snapshot.Inputs.Should().NotContain(input => input.AssetUri != null && input.AssetUri.AbsolutePath.EndsWith(".oscene.json", StringComparison.Ordinal));
        _ = snapshot.Documents.Should().NotContain(document => document.DocumentId == scene.DocumentId);
        _ = scene.IsDirty.Should().BeTrue();
    }

    /// <summary>Geometry preview demand includes the saved material it needs before either asset has been cooked.</summary>
    /// <returns>The asynchronous native dependency regression.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task PreviewGeometryCooksItsUncookedMaterialDependency()
    {
        using var workspace = new TempWorkspace();
        WriteAuthoredGeometry(workspace, withBuffer: false);
        workspace.WriteMaterial("Content/Materials/Red.omat.json", "Red");
        using var compatibility = Oxygen.Testing.TemporaryNativeArtifacts.ForInstalledEngine();
        var pipeline = CreateIncrementalService(workspace, CreateRecordingApi(compatibility), compatibility);
        var geometryUri = new Uri("asset:///Content/Geometry/AuthoredCube.ogeo.json");
        var result = await pipeline.CookPreviewAssetAsync(geometryUri, workspace.ProjectContext, this.TestContext.CancellationToken).ConfigureAwait(false);
        AssertCookSucceeded(result);
        _ = result.CookedAssets.Should().HaveCount(2);
        _ = result.CookedAssets.Should().Contain(asset => asset.SourceAssetUri == geometryUri);
        _ = result.CookedAssets.Should().Contain(asset => asset.SourceAssetUri == new Uri("asset:///Content/Materials/Red.omat.json"));
        _ = workspace.CookCoordinator.Runs.Should().ContainSingle().Which.Request.IsDemand.Should().BeTrue();
    }

    /// <summary>Preview demand cannot silently become a whole-scene cook or cook a built-in.</summary>
    /// <param name="identity">The non-authorable preview scope.</param>
    /// <returns>The asynchronous scope regression.</returns>
    [TestMethod]
    [DataRow("asset:///Content/Scenes/Main.oscene.json")]
    [DataRow("asset://Engine/Generated/BasicShapes/Cylinder")]
    [DataRow("asset://Engine/Generated/Materials/Default")]
    [DataRow("asset:///Content/Models/Source.fbx")]
    public async Task PreviewDemandRejectsNonAssetScopes(string identity)
    {
        using var workspace = new TempWorkspace();
        var pipeline = CreateService(workspace, new CapturingSceneDescriptorGenerator(workspace, []), CreateSuccessfulApi(workspace));
        Func<Task> request = () => pipeline.CookPreviewAssetAsync(new(identity), workspace.ProjectContext, this.TestContext.CancellationToken);
        _ = await request.Should().ThrowAsync<ArgumentException>().ConfigureAwait(false);
        _ = workspace.CookCoordinator.Runs.Should().BeEmpty();
    }
}
