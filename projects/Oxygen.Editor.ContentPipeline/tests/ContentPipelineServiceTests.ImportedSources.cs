// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Security.Cryptography;
using System.Text.Json.Nodes;
using AwesomeAssertions;
using DroidNet.Storage.Native;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Managed.Core.Diagnostics;
using Testably.Abstractions;

namespace Oxygen.Editor.ContentPipeline.Tests;

/// <summary>Exercises retained native sources through ordinary cooking and publication.</summary>
public sealed partial class ContentPipelineServiceTests
{
    /// <summary>A retained native source publishes all outputs once and reuses them after a service restart.</summary>
    /// <param name="extension">The retained model format.</param>
    /// <returns>The asynchronous native import/cook regression.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    [DataRow("gltf")]
    [DataRow("fbx")]
    public async Task RetainedSourceCookPublishesAndReusesNativeOutputs(string extension)
    {
        using var workspace = new TempWorkspace();
        var source = await WriteRetainedModelAsync(workspace, "Model", extension, this.TestContext.CancellationToken).ConfigureAwait(false);
        using var compatibility = Oxygen.Testing.TemporaryNativeArtifacts.ForInstalledEngine();
        var runner = new CountingSourceRunner();
        var api = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), runner, NullLogger<ImportToolContentPipelineApi>.Instance, compatibility);
        var service = CreateService(workspace, new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(api)), api, compatibility);

        var result = await service.CookAssetAsync(source, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = result.Status.Should().BeOneOf([OperationStatus.Succeeded, OperationStatus.SucceededWithWarnings], string.Join(Environment.NewLine, result.Diagnostics.Select(static issue => issue.TechnicalMessage ?? issue.Message)));
        _ = result.IsPublished.Should().BeTrue();
        _ = result.CookedAssets.Should().Contain(asset => asset.Kind == ContentCookAssetKind.Geometry);
        _ = result.CookedAssets.Should().Contain(asset => asset.Kind == ContentCookAssetKind.Scene);
        _ = result.CookedAssets.Should().OnlyContain(asset => asset.SourceAssetUri == source && asset.VirtualPath.StartsWith("/Content/Models/Model/", StringComparison.Ordinal));
        var count = runner.Count;
        var reopened = CreateService(workspace, new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(api)), api, compatibility);
        var repeat = await reopened.CookAssetAsync(source, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = repeat.IsUpToDate.Should().BeTrue();
        _ = runner.Count.Should().Be(count, "unchanged imports must reuse native output without another worker");
        _ = repeat.ReusedAssets.Select(static asset => asset.CookedAssetUri).Should().BeEquivalentTo(result.CookedAssets.Select(static asset => asset.CookedAssetUri));
    }

    /// <summary>Project scope cooks independent model sources and authored materials into one publication.</summary>
    /// <returns>The asynchronous mixed project regression.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task ProjectCookIncludesRetainedSourcesAndAuthoredMaterials()
    {
        using var workspace = new TempWorkspace();
        var first = await WriteRetainedModelAsync(workspace, "First", "gltf", this.TestContext.CancellationToken).ConfigureAwait(false);
        var second = await WriteRetainedModelAsync(workspace, "Second", "fbx", this.TestContext.CancellationToken).ConfigureAwait(false);
        workspace.WriteMaterial("Content/Materials/Authored.omat.json", "Authored");
        using var compatibility = Oxygen.Testing.TemporaryNativeArtifacts.ForInstalledEngine();
        var api = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), new ContentPipelineProcessRunner(), NullLogger<ImportToolContentPipelineApi>.Instance, compatibility);
        var service = CreateService(workspace, new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(api)), api, compatibility);
        var result = await service.CookProjectAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = result.Status.Should().BeOneOf([OperationStatus.Succeeded, OperationStatus.SucceededWithWarnings], string.Join(Environment.NewLine, result.Diagnostics.Select(static issue => issue.TechnicalMessage ?? issue.Message)));
        _ = result.IsPublished.Should().BeTrue();
        _ = result.CookedAssets.Should().Contain(asset => asset.SourceAssetUri == first);
        _ = result.CookedAssets.Should().Contain(asset => asset.SourceAssetUri == second);
        _ = result.CookedAssets.Should().Contain(asset => asset.SourceAssetUri == new Uri("asset:///Content/Materials/Authored.omat.json"));
    }

    /// <summary>Changing source bytes updates native output and persists discovery so the next cook is a no-op.</summary>
    /// <returns>The asynchronous changed-source regression.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task ChangedRetainedSourceIsRediscoveredOnce()
    {
        using var workspace = new TempWorkspace();
        var source = await WriteRetainedModelAsync(workspace, "Model", "gltf", this.TestContext.CancellationToken).ConfigureAwait(false);
        using var compatibility = Oxygen.Testing.TemporaryNativeArtifacts.ForInstalledEngine();
        var runner = new CountingSourceRunner();
        var api = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), runner, NullLogger<ImportToolContentPipelineApi>.Instance, compatibility);
        var service = CreateService(workspace, new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(api)), api, compatibility);
        _ = (await service.CookAssetAsync(source, this.TestContext.CancellationToken).ConfigureAwait(false)).IsPublished.Should().BeTrue();
        var path = Path.Combine(workspace.Root, "Content/SourceMedia/DCC/Model/model.gltf");
        var content = JsonNode.Parse(await File.ReadAllTextAsync(path, this.TestContext.CancellationToken).ConfigureAwait(false))!;
        content["nodes"]![0]!["translation"]![0] = 9;
        await File.WriteAllTextAsync(path, content.ToJsonString(), this.TestContext.CancellationToken).ConfigureAwait(false);
        var changed = await service.CookAssetAsync(source, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = changed.IsPublished.Should().BeTrue(string.Join(Environment.NewLine, changed.Diagnostics.Select(static issue => issue.TechnicalMessage ?? issue.Message)));
        var afterChange = runner.Count;
        _ = (await service.CookAssetAsync(source, this.TestContext.CancellationToken).ConfigureAwait(false)).IsUpToDate.Should().BeTrue();
        _ = runner.Count.Should().Be(afterChange);
    }

    /// <summary>A renamed native output cannot replace published identities hidden by old staged files.</summary>
    /// <returns>The asynchronous identity-protection test.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task ReimportWithChangedAssetNamesPreservesPublishedOutput()
    {
        using var workspace = new TempWorkspace();
        var source = await WriteRetainedModelAsync(workspace, "Model", "gltf", this.TestContext.CancellationToken).ConfigureAwait(false);
        using var compatibility = Oxygen.Testing.TemporaryNativeArtifacts.ForInstalledEngine();
        var api = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), new ContentPipelineProcessRunner(), NullLogger<ImportToolContentPipelineApi>.Instance, compatibility);
        var service = CreateService(workspace, new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(api)), api, compatibility);
        _ = (await service.CookAssetAsync(source, this.TestContext.CancellationToken).ConfigureAwait(false)).IsPublished.Should().BeTrue();
        var index = Path.Combine(workspace.Root, ".cooked/Content/container.index.bin");
        var before = await File.ReadAllBytesAsync(index, this.TestContext.CancellationToken).ConfigureAwait(false);
        var path = Path.Combine(workspace.Root, "Content/SourceMedia/DCC/Model/model.gltf");
        var content = JsonNode.Parse(await File.ReadAllTextAsync(path, this.TestContext.CancellationToken).ConfigureAwait(false))!;
        content["meshes"]![0]!["name"] = "Renamed";
        await File.WriteAllTextAsync(path, content.ToJsonString(), this.TestContext.CancellationToken).ConfigureAwait(false);
        var result = await service.CookAssetAsync(source, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = result.Status.Should().Be(OperationStatus.Failed);
        _ = result.IsPublished.Should().BeFalse();
        _ = result.Diagnostics.Should().Contain(issue => issue.Message.Contains("remove or rename", StringComparison.Ordinal));
        _ = (await File.ReadAllBytesAsync(index, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Equal(before);
    }

    /// <summary>Another source cannot take ownership of an already published import destination.</summary>
    /// <returns>The asynchronous destination-collision test.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task ImportDestinationCollisionPreservesExistingSourceOutputs()
    {
        using var workspace = new TempWorkspace();
        var first = await WriteRetainedModelAsync(workspace, "First", "gltf", this.TestContext.CancellationToken).ConfigureAwait(false);
        var second = await WriteRetainedModelAsync(workspace, "Second", "gltf", this.TestContext.CancellationToken).ConfigureAwait(false);
        using var compatibility = Oxygen.Testing.TemporaryNativeArtifacts.ForInstalledEngine();
        var runner = new CountingSourceRunner();
        var api = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), runner, NullLogger<ImportToolContentPipelineApi>.Instance, compatibility);
        var service = CreateService(workspace, new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(api)), api, compatibility);
        _ = (await service.CookAssetAsync(first, this.TestContext.CancellationToken).ConfigureAwait(false)).IsPublished.Should().BeTrue();
        var settingsPath = Path.Combine(workspace.Root, "Content/SourceMedia/DCC/Second/model.gltf.import.json");
        var settings = NativeSceneImportSettings.Parse(await File.ReadAllBytesAsync(settingsPath, this.TestContext.CancellationToken).ConfigureAwait(false));
        await File.WriteAllBytesAsync(settingsPath, (settings with { OutputDirectory = "Models/First" }).ToBytes(), this.TestContext.CancellationToken).ConfigureAwait(false);
        var before = runner.Count;
        var result = await service.CookAssetAsync(second, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = result.Status.Should().Be(OperationStatus.Failed);
        _ = result.IsPublished.Should().BeFalse();
        _ = runner.Count.Should().Be(before);
        _ = result.Diagnostics.Should().Contain(issue => issue.Message.Contains("overlaps", StringComparison.Ordinal));
    }

    /// <summary>Portable source/settings reproduce the same native logical identities in a clean project root.</summary>
    /// <returns>The asynchronous clean-copy identity test.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task RetainedSourceReproducesLogicalIdentitiesInCleanProject()
    {
        using var first = new TempWorkspace();
        using var second = new TempWorkspace();
        var source = await WriteRetainedModelAsync(first, "Model", "gltf", this.TestContext.CancellationToken).ConfigureAwait(false);
        var sourceRoot = Path.Combine(first.Root, "Content/SourceMedia/DCC/Model");
        var copyRoot = Path.Combine(second.Root, "Content/SourceMedia/DCC/Model");
        _ = Directory.CreateDirectory(copyRoot);
        foreach (var file in Directory.EnumerateFiles(sourceRoot))
        {
            File.Copy(file, Path.Combine(copyRoot, Path.GetFileName(file)));
        }

        using var compatibility = Oxygen.Testing.TemporaryNativeArtifacts.ForInstalledEngine();
        var api = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), new ContentPipelineProcessRunner(), NullLogger<ImportToolContentPipelineApi>.Instance, compatibility);
        var one = await CreateService(first, new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(api)), api, compatibility).CookAssetAsync(source, this.TestContext.CancellationToken).ConfigureAwait(false);
        var two = await CreateService(second, new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(api)), api, compatibility).CookAssetAsync(source, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = one.IsPublished.Should().BeTrue();
        _ = two.IsPublished.Should().BeTrue();
        _ = two.Inspection!.Assets.Select(static asset => (asset.VirtualPath, asset.AssetKey)).Should().BeEquivalentTo(one.Inspection!.Assets.Select(static asset => (asset.VirtualPath, asset.AssetKey)));
    }

    /// <summary>A changed dependency layout is captured from native discovery and reused on later cooks.</summary>
    /// <returns>The asynchronous dependency-closure test.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task ChangedImportedBufferLayoutBecomesThePersistedDependencySet()
    {
        using var workspace = new TempWorkspace();
        var source = await WriteRetainedModelAsync(workspace, "Model", "gltf", this.TestContext.CancellationToken).ConfigureAwait(false);
        using var compatibility = Oxygen.Testing.TemporaryNativeArtifacts.ForInstalledEngine();
        var runner = new CountingSourceRunner();
        var api = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), runner, NullLogger<ImportToolContentPipelineApi>.Instance, compatibility);
        var service = CreateService(workspace, new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(api)), api, compatibility);
        _ = (await service.CookAssetAsync(source, this.TestContext.CancellationToken).ConfigureAwait(false)).IsPublished.Should().BeTrue();
        var directory = Path.Combine(workspace.Root, "Content/SourceMedia/DCC/Model");
        var primary = Path.Combine(directory, "model.gltf");
        var content = JsonNode.Parse(await File.ReadAllTextAsync(primary, this.TestContext.CancellationToken).ConfigureAwait(false))!;
        var buffer = content["buffers"]![0]!;
        var dataUri = buffer["uri"]!.GetValue<string>();
        var bytes = Convert.FromBase64String(dataUri[(dataUri.IndexOf(',', StringComparison.Ordinal) + 1)..]);
        await File.WriteAllBytesAsync(Path.Combine(directory, "mesh data.bin"), bytes, this.TestContext.CancellationToken).ConfigureAwait(false);
        buffer["uri"] = "mesh%20data.bin";
        await File.WriteAllTextAsync(primary, content.ToJsonString(), this.TestContext.CancellationToken).ConfigureAwait(false);
        var changed = await service.CookAssetAsync(source, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = changed.IsPublished.Should().BeTrue(string.Join(Environment.NewLine, changed.Diagnostics.Select(static issue => issue.Message)));
        _ = changed.InputSnapshot!.Inputs.Should().Contain(file => file.RelativePath.EndsWith("mesh data.bin", StringComparison.Ordinal));
        var count = runner.Count;
        _ = (await service.CookAssetAsync(source, this.TestContext.CancellationToken).ConfigureAwait(false)).IsUpToDate.Should().BeTrue();
        _ = runner.Count.Should().Be(count);
    }

    private static async Task<Uri> WriteRetainedModelAsync(TempWorkspace workspace, string name, string extension, CancellationToken cancellationToken)
    {
        var relative = "Content/SourceMedia/DCC/" + name;
        var directory = Path.Combine(workspace.Root, relative);
        _ = Directory.CreateDirectory(directory);
        var filename = "model." + extension;
        var bytes = await File.ReadAllBytesAsync(Path.Combine(AppContext.BaseDirectory, "Fixtures", "static_scalar_triangle." + extension), cancellationToken).ConfigureAwait(false);
        await File.WriteAllBytesAsync(Path.Combine(directory, filename), bytes, cancellationToken).ConfigureAwait(false);
        var retained = new RetainedImportSource(relative, filename, [new(filename, Convert.ToHexString(SHA256.HashData(bytes)))]);

        // Existing model-folder imports must retain their saved paths after the type-folder layout ships.
        var settings = NativeSceneImportSettings.Create(retained, "Content", name, "Models/" + name) with { SchemaVersion = 2 };
        _ = await settings.SaveNewAsync(workspace.Root, new NativeAtomicFileStore(new RealFileSystem()), cancellationToken).ConfigureAwait(false);
        return new Uri("asset:///" + relative + "/" + filename);
    }

    private sealed class CountingSourceRunner : IContentPipelineProcessRunner
    {
        private readonly ContentPipelineProcessRunner inner = new();

        public int Count { get; private set; }

        public Task<ContentPipelineProcessResult> RunAsync(ContentPipelineProcessRequest request, CancellationToken cancellationToken)
        {
            this.Count++;
            return this.inner.RunAsync(request, cancellationToken);
        }
    }
}
