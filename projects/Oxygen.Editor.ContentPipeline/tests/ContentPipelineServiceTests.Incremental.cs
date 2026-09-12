// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Security.Cryptography;
using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Slots;
using Oxygen.Managed.Assets.Model;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Compatibility;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline.Tests;

/// <summary>Checks real native output reuse across requests, source changes, and project reopen.</summary>
public sealed partial class ContentPipelineServiceTests
{
    /// <summary>An unchanged project reuses all products and preserves output bytes and timestamps across service recreation.</summary>
    /// <returns>The asynchronous native regression.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task CurrentProjectCookReusesVerifiedProductsWithoutNativeWorkers()
    {
        using var workspace = new TempWorkspace();
        await PrepareIncrementalSceneAsync(workspace).ConfigureAwait(false);
        using var compatibility = Oxygen.Testing.TemporaryNativeArtifacts.ForInstalledEngine();
        var api = CreateRecordingApi(compatibility);
        var first = await CreateIncrementalService(workspace, api, compatibility).CookProjectAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        AssertCookSucceeded(first);
        var files = ReadOutputIdentities(workspace.Root);
        var workers = api.Imported.Count + api.CatalogRequests;

        var second = await CreateIncrementalService(workspace, api, compatibility).CookProjectAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        AssertCookSucceeded(second);
        _ = second.IsUpToDate.Should().BeTrue();
        _ = second.CookedAssets.Should().BeEmpty();
        _ = second.ReusedAssets.Should().BeEquivalentTo(first.CookedAssets);
        _ = second.Diagnostics.Select(static diagnostic => diagnostic.Code).Should().BeEquivalentTo(first.Diagnostics.Select(static diagnostic => diagnostic.Code));
        _ = (api.Imported.Count + api.CatalogRequests).Should().Be(workers);
        _ = ReadOutputIdentities(workspace.Root).Should().BeEquivalentTo(files);
    }

    /// <summary>A material content change rebuilds that material while preserving scene/geometry descriptors and native catalog reuse.</summary>
    /// <returns>The asynchronous dependency regression.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task ChangedMaterialRebuildsOnlyItsEmittedProduct()
    {
        using var workspace = new TempWorkspace();
        await PrepareIncrementalSceneAsync(workspace).ConfigureAwait(false);
        using var compatibility = Oxygen.Testing.TemporaryNativeArtifacts.ForInstalledEngine();
        var api = CreateRecordingApi(compatibility);
        var pipeline = CreateIncrementalService(workspace, api, compatibility);
        var first = await pipeline.CookProjectAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        AssertCookSucceeded(first);
        var descriptors = first.Inspection!.Assets.Where(static asset => asset.Kind is ContentCookAssetKind.Geometry or ContentCookAssetKind.Scene)
            .ToDictionary(static asset => asset.VirtualPath, asset => File.ReadAllBytes(Path.Combine(first.Inspection.CookedRoot, asset.DescriptorRelativePath!)), StringComparer.Ordinal);
        var beforeCatalog = api.CatalogRequests;
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
        foreach (var (path, bytes) in descriptors)
        {
            var entry = first.Inspection.Assets.Single(asset => string.Equals(asset.VirtualPath, path, StringComparison.Ordinal));
            _ = (await File.ReadAllBytesAsync(Path.Combine(first.Inspection.CookedRoot, entry.DescriptorRelativePath!), this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Equal(bytes);
        }

        _ = (await pipeline.CookProjectAsync(this.TestContext.CancellationToken).ConfigureAwait(false)).IsUpToDate.Should().BeTrue();
    }

    /// <summary>A corrupt generated descriptor is regenerated despite unchanged length and timestamp.</summary>
    /// <returns>The asynchronous corruption regression.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task CorruptGeometryDescriptorInvalidatesOnlyThatProduct()
    {
        using var workspace = new TempWorkspace();
        await PrepareIncrementalSceneAsync(workspace).ConfigureAwait(false);
        using var compatibility = Oxygen.Testing.TemporaryNativeArtifacts.ForInstalledEngine();
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
        _ = (await File.ReadAllBytesAsync(path, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Equal(bytes);
        _ = (await pipeline.CookProjectAsync(this.TestContext.CancellationToken).ConfigureAwait(false)).IsUpToDate.Should().BeTrue();
    }

    /// <summary>A failed import cannot advance the saved product evidence used by a later retry.</summary>
    /// <returns>The asynchronous failed-run regression.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task FailedCookDoesNotReplaceProductProvenance()
    {
        using var workspace = new TempWorkspace();
        await PrepareIncrementalSceneAsync(workspace).ConfigureAwait(false);
        using var compatibility = Oxygen.Testing.TemporaryNativeArtifacts.ForInstalledEngine();
        var api = CreateRecordingApi(compatibility);
        var pipeline = CreateIncrementalService(workspace, api, compatibility);
        AssertCookSucceeded(await pipeline.CookProjectAsync(this.TestContext.CancellationToken).ConfigureAwait(false));
        var path = Path.Combine(workspace.Root, ".build", "cook", "provenance.json");
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
        using var workspace = new TempWorkspace();
        await PrepareIncrementalSceneAsync(workspace).ConfigureAwait(false);
        using var compatibility = Oxygen.Testing.TemporaryNativeArtifacts.ForInstalledEngine();
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

    private static RecordingNativeApi CreateRecordingApi(INativeCompatibilityService compatibility)
        => new(new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), new ContentPipelineProcessRunner(), NullLogger<ImportToolContentPipelineApi>.Instance, compatibility));

    private static ContentPipelineService CreateIncrementalService(TempWorkspace workspace, RecordingNativeApi api, INativeCompatibilityService compatibility)
        => CreateService(workspace, new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(api)), api, compatibility);

    private static void AssertCookSucceeded(ContentCookResult result)
        => _ = result.Status.Should().BeOneOf([OperationStatus.Succeeded, OperationStatus.SucceededWithWarnings], string.Join(Environment.NewLine, result.Diagnostics.Select(static diagnostic => diagnostic.TechnicalMessage ?? diagnostic.Message)));

    private static Dictionary<string, (string hash, DateTime write)> ReadOutputIdentities(string projectRoot)
        => Directory.EnumerateFiles(Path.Combine(projectRoot, ".cooked"), "*", SearchOption.AllDirectories)
            .ToDictionary(static path => path, static path => (Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(path))), File.GetLastWriteTimeUtc(path)), StringComparer.Ordinal);

    private static async Task PrepareIncrementalSceneAsync(TempWorkspace workspace)
    {
        workspace.WriteMaterial("Content/Materials/Blue.omat.json", "Blue");
        var node = new SceneNode(workspace.Scene) { Name = "Cube" };
        var geometry = new GeometryComponent { Name = "Geometry", Geometry = new AssetReference<GeometryAsset>(AssetUris.BuildGeneratedUri("BasicShapes/Cube")) };
        geometry.OverrideSlots.Add(new MaterialsSlot { Material = new AssetReference<MaterialAsset>(new Uri("asset:///Content/Materials/Blue.omat.json")) });
        _ = node.AddComponent(geometry);
        workspace.Scene.RootNodes.Add(node);
        workspace.Scene.SetEnvironment(new SceneEnvironmentData { ExposureMode = ExposureMode.Auto, PostProcess = new PostProcessEnvironmentData { ExposureMode = ExposureMode.Auto } });
        await workspace.WriteSceneAsync("Content/Scenes/Main.oscene.json").ConfigureAwait(false);
    }

    private sealed class RecordingNativeApi(ImportToolContentPipelineApi native) : IEngineContentPipelineApi, IBuiltinGeometryCatalogProvider
    {
        public List<ContentImportExecution> Imported { get; } = [];

        public int CatalogRequests { get; private set; }

        public bool FailNextImport { get; set; }

        public bool AddSceneWarning { get; set; }

        public async Task<NativeImportResult> ImportAsync(ContentImportExecution execution, CancellationToken cancellationToken)
        {
            this.Imported.Add(execution);
            if (this.FailNextImport)
            {
                this.FailNextImport = false;
                return new NativeImportResult(Succeeded: false, [new DiagnosticRecord { OperationId = execution.OperationId, Domain = FailureDomain.ContentPipeline, Severity = DiagnosticSeverity.Error, Code = "TEST.IMPORT_FAILED", Message = "Injected import failure." }]);
            }

            var result = await native.ImportAsync(execution, cancellationToken).ConfigureAwait(false);
            return result.Succeeded && this.AddSceneWarning
                ? result with { Diagnostics = [.. result.Diagnostics, new DiagnosticRecord { OperationId = execution.OperationId, Domain = FailureDomain.ContentPipeline, Severity = DiagnosticSeverity.Warning, Code = "TEST.SCENE_WARNING", Message = "Scene warning.", AffectedVirtualPath = "/Content/Scenes/Main.oscene" }] }
                : result;
        }

        public Task<CookInspectionResult> InspectLooseCookedRootAsync(string cookedRoot, CancellationToken cancellationToken) => native.InspectLooseCookedRootAsync(cookedRoot, cancellationToken);

        public Task<CookValidationResult> ValidateLooseCookedRootAsync(string cookedRoot, CancellationToken cancellationToken) => native.ValidateLooseCookedRootAsync(cookedRoot, cancellationToken);

        public Task<BuiltinGeometryCatalog> GetBuiltinGeometryCatalogAsync(string projectRoot, string mountName, CancellationToken cancellationToken, NativeArtifactLease? artifacts = null)
        {
            this.CatalogRequests++;
            return native.GetBuiltinGeometryCatalogAsync(projectRoot, mountName, cancellationToken, artifacts);
        }
    }
}
