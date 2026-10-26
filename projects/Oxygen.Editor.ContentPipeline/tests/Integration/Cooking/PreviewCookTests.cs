// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.TestSupport;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Compatibility;
using static Oxygen.Editor.ContentPipeline.TestSupport.DependencyScenario;
using static Oxygen.Editor.ContentPipeline.TestSupport.IncrementalCookScenario;

namespace Oxygen.Editor.ContentPipeline.Integration.Tests.Cooking;

[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Naming", "CA1707:Identifiers should not contain underscores", Justification = "Scenario-based MSTest method names separate the operation and expected behavior.")]
public sealed class PreviewCookTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>A material demand joins its paused Save without cooking its consuming scene.</summary>
    /// <returns>The asynchronous native preview-demand regression.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task PreviewMaterialSharesSavedScopeWithoutIncludingConsumingScene()
    {
        using var workspace = new CookWorkspace(engineContent: EngineContentPak.Installed());
        var compatibility = EditorNativeCompatibilityService.ForCooking();
        var api = CreateRecordingApi(compatibility);
        await PrepareIncrementalSceneAsync(workspace, api).ConfigureAwait(false);
        var pipeline = CreateIncrementalService(workspace, api, compatibility);
        var materialUri = AssetPath.Parse("/Content/Materials/Blue.omat");
        var hold = WriterHold.Take(workspace.CookCoordinator, this.TestContext.CancellationToken);
        var saved = pipeline.CookSavedAssetAsync(materialUri, workspace.ProjectContext, this.TestContext.CancellationToken);
        var preview = pipeline.CookPreviewAssetAsync(materialUri, workspace.ProjectContext, this.TestContext.CancellationToken);
        var queued = workspace.CookCoordinator.Runs.Should().ContainSingle().Subject;
        _ = queued.Request.IsDemand.Should().BeTrue();
        _ = queued.Request.IsAutomatic.Should().BeTrue();
        _ = preview.IsCompleted.Should().BeFalse();
        await hold.ReleaseAsync().ConfigureAwait(false);
        var result = await preview.ConfigureAwait(false);
        AssertCookSucceeded(result);
        _ = (await saved.ConfigureAwait(false)).Should().BeSameAs(result);
        _ = result.InputSnapshot.Should().NotBeNull();
        var snapshot = result.InputSnapshot!;
        _ = snapshot.Inputs.Should().NotContain(input => input.AssetUri != null && input.AssetUri.VirtualPath.EndsWith(".oscene", StringComparison.Ordinal));
    }

    /// <summary>Geometry preview demand includes the saved material it needs before either asset has been cooked.</summary>
    /// <returns>The asynchronous native dependency regression.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task PreviewGeometryCooksItsUncookedMaterialDependency()
    {
        using var workspace = new CookWorkspace(engineContent: EngineContentPak.Installed());
        WriteAuthoredGeometry(workspace, withBuffer: false);
        workspace.WriteMaterial("Content/Materials/Red.omat.json", "Red");
        var compatibility = EditorNativeCompatibilityService.ForCooking();
        var pipeline = CreateIncrementalService(workspace, CreateRecordingApi(compatibility), compatibility);
        var geometryUri = AssetPath.Parse("/Content/Geometry/AuthoredCube.ogeo");
        var result = await pipeline.CookPreviewAssetAsync(geometryUri, workspace.ProjectContext, this.TestContext.CancellationToken).ConfigureAwait(false);
        AssertCookSucceeded(result);
        _ = result.CookedAssets.Should().HaveCount(2);
        _ = result.CookedAssets.Should().Contain(asset => asset.SourceAssetUri == geometryUri);
        _ = result.CookedAssets.Should().Contain(asset => asset.SourceAssetUri == AssetPath.Parse("/Content/Materials/Red.omat"));
        _ = workspace.CookCoordinator.Runs.Should().ContainSingle().Which.Request.IsDemand.Should().BeTrue();
    }
}
