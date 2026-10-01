// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Security.Cryptography;
using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentPipeline.TestSupport;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Slots;
using Oxygen.Editor.World;
using Oxygen.Managed.Assets.Model;
using Oxygen.Managed.Core.Compatibility;
using Oxygen.Managed.Core.Diagnostics;
using Oxygen.Managed.Core;
using static Oxygen.Editor.ContentPipeline.TestSupport.IncrementalCookScenario;

namespace Oxygen.Editor.ContentPipeline.Integration.Tests.Cooking;

[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Naming", "CA1707:Identifiers should not contain underscores", Justification = "Scenario-based MSTest method names separate the operation and expected behavior.")]
public sealed class IncrementalCookTests
{
    public TestContext TestContext { get; set; } = null!;
    /// <summary>A saved scene produces a quiet automatic run, its assets, and reusable native output.</summary>
    /// <returns>The asynchronous native background-cook regression.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task SavedSceneCookUsesSharedRunHistoryAndIncrementalOutput()
    {
        using var workspace = new CookWorkspace();
        await PrepareIncrementalSceneAsync(workspace).ConfigureAwait(false);
        using var compatibility = EditorNativeCompatibilityService.ForCooking();
        var api = CreateRecordingApi(compatibility);
        var pipeline = CreateIncrementalService(workspace, api, compatibility);
        var revealed = false;
        workspace.CookCoordinator.RunChanged += (_, args) => revealed |= args.Reveal;
        var sceneUri = new Uri("asset:///Content/Scenes/Main.oscene.json");

        var result = await pipeline.CookSavedAssetAsync(sceneUri, workspace.ProjectContext, this.TestContext.CancellationToken).ConfigureAwait(false);

        AssertCookSucceeded(result);
        var run = workspace.CookCoordinator.Runs.Single();
        _ = run.Request.IsAutomatic.Should().BeTrue();
        _ = run.Request.ScopeUri.Should().Be(sceneUri);
        _ = run.Assets.Should().NotBeEmpty();
        _ = run.IsCompleted.Should().BeTrue();
        _ = revealed.Should().BeFalse();
        _ = (await pipeline.CookSavedAssetAsync(sceneUri, workspace.ProjectContext, this.TestContext.CancellationToken).ConfigureAwait(false)).IsUpToDate.Should().BeTrue();
    }

    /// <summary>An explicit cook joins a paused Save through the real snapshot and native publication pipeline.</summary>
    /// <returns>The asynchronous shared native workflow regression.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task ExplicitCookSharesPausedSavedScopeThroughNativePublication()
    {
        using var workspace = new CookWorkspace();
        await PrepareIncrementalSceneAsync(workspace).ConfigureAwait(false);
        using var compatibility = EditorNativeCompatibilityService.ForCooking();
        var pipeline = CreateIncrementalService(workspace, CreateRecordingApi(compatibility), compatibility);
        var sceneUri = new Uri("asset:///Content/Scenes/Main.oscene.json");
        workspace.CookCoordinator.IsAutomaticCookingPaused = true;
        var saved = pipeline.CookSavedAssetAsync(sceneUri, workspace.ProjectContext, this.TestContext.CancellationToken);
        _ = saved.IsCompleted.Should().BeFalse();
        var explicitCook = pipeline.CookAssetAsync(sceneUri, this.TestContext.CancellationToken);
        var savedResult = await saved.ConfigureAwait(false);
        var explicitResult = await explicitCook.ConfigureAwait(false);
        AssertCookSucceeded(savedResult);
        _ = explicitResult.Should().BeSameAs(savedResult);
        var run = workspace.CookCoordinator.Runs.Should().ContainSingle().Subject;
        _ = run.OperationId.Should().Be(savedResult.OperationId);
        _ = run.Request.IsAutomatic.Should().BeFalse();
        _ = run.IsCompleted.Should().BeTrue();
    }

    /// <summary>An unchanged project reuses all products and preserves output bytes and timestamps across service recreation.</summary>
    /// <returns>The asynchronous native regression.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task CurrentProjectCookReusesVerifiedProductsWithoutNativeWorkers()
    {
        using var workspace = new CookWorkspace();
        await PrepareIncrementalSceneAsync(workspace).ConfigureAwait(false);
        using var compatibility = EditorNativeCompatibilityService.ForCooking();
        var api = CreateRecordingApi(compatibility);
        var first = await CreateIncrementalService(workspace, api, compatibility).CookProjectAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        AssertCookSucceeded(first);
        var files = ReadOutputIdentities(workspace.Root);
        var workers = api.Imported.Count + api.CatalogRequests + api.Analyzed.Count;

        var second = await CreateIncrementalService(workspace, api, compatibility).CookProjectAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        AssertCookSucceeded(second);
        _ = second.IsUpToDate.Should().BeTrue();
        _ = second.CookedAssets.Should().BeEmpty();
        _ = second.ReusedAssets.Should().BeEquivalentTo(first.CookedAssets);
        _ = second.Diagnostics.Select(static diagnostic => diagnostic.Code).Should().BeEquivalentTo(first.Diagnostics.Select(static diagnostic => diagnostic.Code));
        _ = (api.Imported.Count + api.CatalogRequests + api.Analyzed.Count).Should().Be(workers);
        _ = ReadOutputIdentities(workspace.Root).Should().BeEquivalentTo(files);
    }

    /// <summary>A material content change rebuilds that material while preserving scene/geometry descriptors and native catalog reuse.</summary>
    /// <returns>The asynchronous dependency regression.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task ChangedMaterialRebuildsOnlyItsEmittedProduct()
    {
        using var workspace = new CookWorkspace();
        await PrepareIncrementalSceneAsync(workspace).ConfigureAwait(false);
        using var compatibility = EditorNativeCompatibilityService.ForCooking();
        var api = CreateRecordingApi(compatibility);
        var pipeline = CreateIncrementalService(workspace, api, compatibility);
        var first = await pipeline.CookProjectAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        AssertCookSucceeded(first);
        var descriptors = first.Inspection!.Assets.Where(static asset => asset.Kind is ContentCookAssetKind.Geometry or ContentCookAssetKind.Scene)
            .ToDictionary(static asset => asset.VirtualPath, asset => File.ReadAllBytes(Path.Combine(first.Inspection.CookedRoot, asset.DescriptorRelativePath!)), StringComparer.Ordinal);
        var beforeCatalog = api.CatalogRequests;
        var beforeAnalysis = api.Analyzed.Count;
        var source = Path.Combine(workspace.Root, "Content", "Materials", "Blue.omat.json");
        var timestamp = File.GetLastWriteTimeUtc(source);
        workspace.WriteText("Content/Materials/Blue.omat.json", workspace.ReadText("Content/Materials/Blue.omat.json").Replace("0.5", "0.7", StringComparison.Ordinal));
        File.SetLastWriteTimeUtc(source, timestamp);
        api.Imported.Clear();

        var changed = await pipeline.CookProjectAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        AssertCookSucceeded(changed);
        _ = changed.CookedAssets.Should().ContainSingle(asset => asset.SourceAssetUri == new Uri("asset:///Content/Materials/Blue.omat.json"));
        _ = api.Imported.Should().ContainSingle();
        _ = api.Imported[0].Manifest.Jobs.Should().ContainSingle(job => job.Type == "material-descriptor");
        _ = api.CatalogRequests.Should().Be(beforeCatalog);
        _ = api.Analyzed.Skip(beforeAnalysis).SelectMany(static execution => execution.Jobs).Should().ContainSingle(job => job.Type == "material-descriptor");
        foreach (var (path, bytes) in descriptors)
        {
            var entry = first.Inspection.Assets.Single(asset => string.Equals(asset.VirtualPath, path, StringComparison.Ordinal));
            _ = (await File.ReadAllBytesAsync(Path.Combine(workspace.CookedRoot("Content"), entry.DescriptorRelativePath!), this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Equal(bytes);
        }

        _ = (await pipeline.CookProjectAsync(this.TestContext.CancellationToken).ConfigureAwait(false)).IsUpToDate.Should().BeTrue();
    }

    /// <summary>A corrupt generated descriptor is regenerated despite unchanged length and timestamp.</summary>
    /// <returns>The asynchronous corruption regression.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task CorruptGeometryDescriptorInvalidatesOnlyThatProduct()
    {
        using var workspace = new CookWorkspace();
        await PrepareIncrementalSceneAsync(workspace).ConfigureAwait(false);
        using var compatibility = EditorNativeCompatibilityService.ForCooking();
        var api = CreateRecordingApi(compatibility);
        var pipeline = CreateIncrementalService(workspace, api, compatibility);
        var first = await pipeline.CookProjectAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        AssertCookSucceeded(first);
        var geometry = first.Inspection!.Assets.Single(static asset => asset.Kind == ContentCookAssetKind.Geometry);
        var path = Path.Combine(first.Inspection.CookedRoot, geometry.DescriptorRelativePath!);
        var bytes = await File.ReadAllBytesAsync(path, this.TestContext.CancellationToken).ConfigureAwait(false);
        var timestamp = File.GetLastWriteTimeUtc(path);
        var corrupted = bytes.ToArray();
        corrupted[^1] ^= 1;
        await File.WriteAllBytesAsync(path, corrupted, this.TestContext.CancellationToken).ConfigureAwait(false);
        File.SetLastWriteTimeUtc(path, timestamp);
        api.Imported.Clear();

        var repaired = await pipeline.CookProjectAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        AssertCookSucceeded(repaired);
        _ = repaired.IsUpToDate.Should().BeFalse();
        _ = api.Imported.Should().ContainSingle();
        _ = api.Imported[0].Manifest.Jobs.Should().ContainSingle(job => job.Type == "geometry-descriptor");
        _ = (await File.ReadAllBytesAsync(Path.Combine(workspace.CookedRoot("Content"), geometry.DescriptorRelativePath!), this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Equal(bytes);
        _ = (await pipeline.CookProjectAsync(this.TestContext.CancellationToken).ConfigureAwait(false)).IsUpToDate.Should().BeTrue();
    }

    /// <summary>A failed import cannot advance the saved product evidence used by a later retry.</summary>
    /// <returns>The asynchronous failed-run regression.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task FailedCookDoesNotReplaceProductProvenance()
    {
        using var workspace = new CookWorkspace();
        await PrepareIncrementalSceneAsync(workspace).ConfigureAwait(false);
        using var compatibility = EditorNativeCompatibilityService.ForCooking();
        var api = CreateRecordingApi(compatibility);
        var pipeline = CreateIncrementalService(workspace, api, compatibility);
        AssertCookSucceeded(await pipeline.CookProjectAsync(this.TestContext.CancellationToken).ConfigureAwait(false));
        var path = global::Oxygen.Editor.ContentPipeline.Publication.CookPublicationPaths.Head(workspace.Root);
        var before = await File.ReadAllBytesAsync(path, this.TestContext.CancellationToken).ConfigureAwait(false);
        workspace.WriteText("Content/Materials/Blue.omat.json", workspace.ReadText("Content/Materials/Blue.omat.json").Replace("0.5", "0.8", StringComparison.Ordinal));
        api.FailNextImport = true;

        var failed = await pipeline.CookProjectAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = failed.Status.Should().Be(OperationStatus.Failed);
        _ = (await File.ReadAllBytesAsync(path, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Equal(before);
        AssertCookSucceeded(await pipeline.CookProjectAsync(this.TestContext.CancellationToken).ConfigureAwait(false));

        _ = (await pipeline.CookProjectAsync(this.TestContext.CancellationToken).ConfigureAwait(false)).IsUpToDate.Should().BeTrue();
    }

    /// <summary>Reused scene warnings remain visible and do not leak into a material-only request.</summary>
    /// <returns>The asynchronous diagnostic regression.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task ReusedWarningsStayWithTheirAffectedProduct()
    {
        using var workspace = new CookWorkspace();
        await PrepareIncrementalSceneAsync(workspace).ConfigureAwait(false);
        using var compatibility = EditorNativeCompatibilityService.ForCooking();
        var api = CreateRecordingApi(compatibility);
        api.AddSceneWarning = true;
        var pipeline = CreateIncrementalService(workspace, api, compatibility);
        AssertCookSucceeded(await pipeline.CookProjectAsync(this.TestContext.CancellationToken).ConfigureAwait(false));

        var reused = await pipeline.CookProjectAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = reused.IsUpToDate.Should().BeTrue();
        _ = reused.Status.Should().Be(OperationStatus.SucceededWithWarnings);
        _ = reused.Diagnostics.Should().Contain(diagnostic => diagnostic.Code == "TEST.SCENE_WARNING" && diagnostic.OperationId == reused.OperationId);
        var material = await pipeline.CookAssetAsync(new Uri("asset:///Content/Materials/Blue.omat.json"), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = material.IsUpToDate.Should().BeTrue();
        _ = material.Diagnostics.Should().NotContain(static diagnostic => diagnostic.Code == "TEST.SCENE_WARNING");
    }
}
