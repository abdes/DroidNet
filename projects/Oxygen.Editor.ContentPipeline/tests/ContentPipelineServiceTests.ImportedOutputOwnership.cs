// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Nodes;
using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline.Tests;

/// <summary>Exercises source ownership when cooking actual imported output identities.</summary>
public sealed partial class ContentPipelineServiceTests
{
    /// <summary>An authored descriptor cannot silently take over an import-owned native identity.</summary>
    /// <param name="authoredRequest">Whether the request selects the descriptor instead of the cooked identity.</param>
    /// <returns>The asynchronous namespace-conflict regression.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    [DataRow(false)]
    [DataRow(true)]
    public async Task ImportedOutputConflictsWithAuthoredDescriptor(bool authoredRequest)
    {
        using var workspace = new TempWorkspace();
        var source = await WriteRetainedModelAsync(workspace, "Model", "gltf", this.TestContext.CancellationToken).ConfigureAwait(false);
        using var compatibility = Oxygen.Testing.TemporaryNativeArtifacts.ForInstalledEngine();
        var runner = new CountingSourceRunner();
        var api = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), runner, NullLogger<ImportToolContentPipelineApi>.Instance, compatibility);
        var service = CreateService(workspace, new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(api)), api, compatibility);
        var first = await service.CookAssetAsync(source, this.TestContext.CancellationToken).ConfigureAwait(false);
        var material = first.CookedAssets.First(static asset => asset.Kind == ContentCookAssetKind.Material).CookedAssetUri;
        workspace.WriteMaterial(material.AbsolutePath.TrimStart('/') + ".json", "Authored conflict");
        var count = runner.Count;
        var result = await service.CookAssetAsync(authoredRequest ? new Uri(material + ".json") : material, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = result.Status.Should().Be(OperationStatus.Failed);
        _ = result.Diagnostics.Should().Contain(issue => issue.Message.Contains("conflicts with authored descriptor", StringComparison.Ordinal));
        _ = runner.Count.Should().Be(count);
        var statuses = await service.ReadAsync(workspace.ProjectContext, [source, material], this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = statuses.Single(status => status.AssetUri == material).Diagnostics.Should().Contain(issue => issue.Code == "asset_status.import_conflict");
        _ = statuses.Single(status => status.AssetUri == source).Freshness.Should().Be(AssetCookFreshness.Current);
    }

    /// <summary>Two retained source namespaces cannot claim the same requested output.</summary>
    /// <returns>The asynchronous ambiguous-owner regression.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task ImportedOutputRejectsAmbiguousSourceOwners()
    {
        using var workspace = new TempWorkspace();
        var source = await WriteRetainedModelAsync(workspace, "Model", "gltf", this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = await WriteRetainedModelAsync(workspace, "Other", "gltf", this.TestContext.CancellationToken).ConfigureAwait(false);
        using var compatibility = Oxygen.Testing.TemporaryNativeArtifacts.ForInstalledEngine();
        var runner = new CountingSourceRunner();
        var api = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), runner, NullLogger<ImportToolContentPipelineApi>.Instance, compatibility);
        var service = CreateService(workspace, new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(api)), api, compatibility);
        var first = await service.CookAssetAsync(source, this.TestContext.CancellationToken).ConfigureAwait(false);
        var geometry = first.CookedAssets.First(static asset => asset.Kind == ContentCookAssetKind.Geometry).CookedAssetUri;
        var settingsPath = Path.Combine(workspace.Root, "Content/SourceMedia/DCC/Other/model.gltf.import.json");
        var settings = NativeSceneImportSettings.Parse(await File.ReadAllBytesAsync(settingsPath, this.TestContext.CancellationToken).ConfigureAwait(false));
        await File.WriteAllBytesAsync(settingsPath, (settings with { OutputDirectory = "Models/Model" }).ToBytes(), this.TestContext.CancellationToken).ConfigureAwait(false);
        var count = runner.Count;
        var result = await service.CookAssetAsync(geometry, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = result.Status.Should().Be(OperationStatus.Failed);
        _ = result.Diagnostics.Should().Contain(issue => issue.Message.Contains("overlaps", StringComparison.Ordinal));
        _ = runner.Count.Should().Be(count);
    }

    /// <summary>A changed producer can regenerate an unchanged saved model automatically.</summary>
    /// <returns>The asynchronous producer-only refresh regression.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task ImportedOutputProducerChangeDoesNotRequireSourceReimport()
    {
        using var workspace = new TempWorkspace();
        var source = await WriteRetainedModelAsync(workspace, "Model", "gltf", this.TestContext.CancellationToken).ConfigureAwait(false);
        var policy = Path.Combine(workspace.Root, "test-producer-policy");
        await File.WriteAllTextAsync(policy, "v1", this.TestContext.CancellationToken).ConfigureAwait(false);
        using var firstCompatibility = Oxygen.Testing.TemporaryNativeArtifacts.ForInstalledEngine([new("test/policy", policy)]);
        var firstApi = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), new ContentPipelineProcessRunner(), NullLogger<ImportToolContentPipelineApi>.Instance, firstCompatibility);
        var first = await CreateService(workspace, new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(firstApi)), firstApi, firstCompatibility).CookAssetAsync(source, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = first.IsPublished.Should().BeTrue();
        await File.WriteAllTextAsync(policy, "v2", this.TestContext.CancellationToken).ConfigureAwait(false);
        using var changedCompatibility = Oxygen.Testing.TemporaryNativeArtifacts.ForInstalledEngine([new("test/policy", policy)]);
        var changedApi = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), new ContentPipelineProcessRunner(), NullLogger<ImportToolContentPipelineApi>.Instance, changedCompatibility);
        var service = CreateService(workspace, new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(changedApi)), changedApi, changedCompatibility);
        var geometry = first.CookedAssets.First(static asset => asset.Kind == ContentCookAssetKind.Geometry).CookedAssetUri;
        var result = await service.CookPreviewAssetAsync(geometry, workspace.ProjectContext, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = result.IsPublished.Should().BeTrue(string.Join(Environment.NewLine, result.Diagnostics.Select(static issue => issue.Message)));
    }

    /// <summary>Every named native output resolves back to one retained source and reuses its verified publication.</summary>
    /// <returns>The asynchronous imported-output request test.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task ImportedOutputCookReusesItsRetainedSource()
    {
        using var workspace = new TempWorkspace();
        var source = await WriteRetainedModelAsync(workspace, "Model", "gltf", this.TestContext.CancellationToken).ConfigureAwait(false);
        using var compatibility = Oxygen.Testing.TemporaryNativeArtifacts.ForInstalledEngine();
        var runner = new CountingSourceRunner();
        var api = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), runner, NullLogger<ImportToolContentPipelineApi>.Instance, compatibility);
        var service = CreateService(workspace, new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(api)), api, compatibility);
        var first = await service.CookAssetAsync(source, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = first.IsPublished.Should().BeTrue();
        var count = runner.Count;
        foreach (var output in first.CookedAssets)
        {
            var result = await service.CookAssetAsync(output.CookedAssetUri, this.TestContext.CancellationToken).ConfigureAwait(false);
            _ = result.IsUpToDate.Should().BeTrue(string.Join(Environment.NewLine, result.Diagnostics.Select(static issue => issue.Message)));
            _ = result.ReusedAssets.Should().Contain(asset => asset.CookedAssetUri == output.CookedAssetUri && asset.SourceAssetUri == source);
        }

        _ = runner.Count.Should().Be(count);
    }

    /// <summary>Saved source settings resolve named outputs and scene dependencies in a project with no derived metadata.</summary>
    /// <param name="scope">The requested scope.</param>
    /// <returns>The asynchronous clean-project imported-output regression.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    [DataRow("asset")]
    [DataRow("folder")]
    [DataRow("scene")]
    public async Task ImportedOutputResolvesInCleanProject(string scope)
    {
        using var original = new TempWorkspace();
        using var clean = new TempWorkspace();
        var source = await WriteRetainedModelAsync(original, "Model", "gltf", this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = await WriteRetainedModelAsync(clean, "Model", "gltf", this.TestContext.CancellationToken).ConfigureAwait(false);
        using var compatibility = Oxygen.Testing.TemporaryNativeArtifacts.ForInstalledEngine();
        var api = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), new ContentPipelineProcessRunner(), NullLogger<ImportToolContentPipelineApi>.Instance, compatibility);
        var first = await CreateService(original, new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(api)), api, compatibility).CookAssetAsync(source, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = first.IsPublished.Should().BeTrue();
        var geometry = first.CookedAssets.First(static output => output.Kind == ContentCookAssetKind.Geometry);
        AddGeometryNode(clean, geometry.CookedAssetUri, "Imported mesh");
        await clean.WriteSceneAsync("Content/Scenes/Main.oscene.json").ConfigureAwait(false);
        var service = CreateService(clean, new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(api)), api, compatibility);
        var result = await (scope switch
        {
            "asset" => service.CookAssetAsync(geometry.CookedAssetUri, this.TestContext.CancellationToken),
            "folder" => service.CookFolderAsync(new("asset:///Content/Models"), this.TestContext.CancellationToken),
            _ => service.CookCurrentSceneAsync(new("asset:///Content/Scenes/Main.oscene.json"), this.TestContext.CancellationToken),
        }).ConfigureAwait(false);
        _ = result.IsPublished.Should().BeTrue(string.Join(Environment.NewLine, result.Diagnostics.Select(static issue => issue.TechnicalMessage ?? issue.Message)));
        _ = result.CookedAssets.Should().Contain(asset => asset.CookedAssetUri == geometry.CookedAssetUri && asset.SourceAssetUri == source);
        _ = result.InputsAreCurrent.Should().BeTrue();
        _ = result.InputSnapshot!.Inputs.Should().Contain(input => input.RelativePath.EndsWith("model.gltf.import.json", StringComparison.Ordinal));
    }

    /// <summary>A requested name must actually exist in native output; owning its prefix is insufficient.</summary>
    /// <param name="alreadyCooked">Whether the model already has reusable output.</param>
    /// <returns>The asynchronous missing imported-output regression.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    [DataRow(false)]
    [DataRow(true)]
    public async Task ImportedOutputMissingNameCannotPublish(bool alreadyCooked)
    {
        using var workspace = new TempWorkspace();
        var source = await WriteRetainedModelAsync(workspace, "Model", "gltf", this.TestContext.CancellationToken).ConfigureAwait(false);
        using var compatibility = Oxygen.Testing.TemporaryNativeArtifacts.ForInstalledEngine();
        var api = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), new ContentPipelineProcessRunner(), NullLogger<ImportToolContentPipelineApi>.Instance, compatibility);
        var service = CreateService(workspace, new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(api)), api, compatibility);
        var index = Path.Combine(workspace.Root, ".cooked/Content/container.index.bin");
        byte[]? before = null;
        if (alreadyCooked)
        {
            _ = (await service.CookAssetAsync(source, this.TestContext.CancellationToken).ConfigureAwait(false)).IsPublished.Should().BeTrue();
            before = await File.ReadAllBytesAsync(index, this.TestContext.CancellationToken).ConfigureAwait(false);
        }

        var result = await service.CookAssetAsync(new("asset:///Content/Models/Model/Geometry/model/Missing.ogeo"), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = result.Status.Should().Be(OperationStatus.Failed);
        _ = result.IsPublished.Should().BeFalse();
        _ = result.Diagnostics.Should().Contain(issue => issue.Message.Contains("does not produce", StringComparison.Ordinal));
        if (before is not null)
        {
            _ = (await File.ReadAllBytesAsync(index, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Equal(before);
        }
        else
        {
            _ = File.Exists(index).Should().BeFalse();
        }
    }

    /// <summary>Output status follows retained-source edits without native discovery or a false invalid-source error.</summary>
    /// <returns>The asynchronous source/output status regression.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task ImportedOutputStatusTracksSourceWithoutStartingWorker()
    {
        using var workspace = new TempWorkspace();
        var source = await WriteRetainedModelAsync(workspace, "Model", "gltf", this.TestContext.CancellationToken).ConfigureAwait(false);
        using var compatibility = Oxygen.Testing.TemporaryNativeArtifacts.ForInstalledEngine();
        var runner = new CountingSourceRunner();
        var api = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), runner, NullLogger<ImportToolContentPipelineApi>.Instance, compatibility);
        var service = CreateService(workspace, new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(api)), api, compatibility);
        var first = await service.CookAssetAsync(source, this.TestContext.CancellationToken).ConfigureAwait(false);
        var uris = first.CookedAssets.Select(static output => output.CookedAssetUri).Prepend(source).ToArray();
        var count = runner.Count;
        var statuses = await service.ReadAsync(workspace.ProjectContext, uris, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = statuses.Select(static status => status.AssetUri).Should().BeEquivalentTo(uris);
        _ = statuses.Should().OnlyContain(static status => status.Freshness == AssetCookFreshness.Current && status.HasVerifiedOutput);
        await File.AppendAllTextAsync(Path.Combine(workspace.Root, "Content/SourceMedia/DCC/Model/model.gltf"), " ", this.TestContext.CancellationToken).ConfigureAwait(false);
        statuses = await service.ReadAsync(workspace.ProjectContext, uris, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = statuses.Should().OnlyContain(static status => status.Freshness == AssetCookFreshness.OutOfDate && status.HasPublishedOutput);
        _ = statuses.SelectMany(static status => status.Diagnostics).Should().NotContain(static issue => issue.Severity == DiagnosticSeverity.Error);
        var incomplete = await new Snapshots.CookDependencyDiscovery(workspace.Documents).DiscoverAsync(
            workspace.ProjectContext,
            [CookInputResolver.Resolve(workspace.ProjectContext, source, ContentCookInputRole.Primary)],
            this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = incomplete.Diagnostics.Should().Contain(static issue => issue.Severity == DiagnosticSeverity.Error);
        _ = runner.Count.Should().Be(count);
    }

    /// <summary>Saving a consumer or requesting preview cannot silently replace externally changed model content.</summary>
    /// <param name="externalBuffer">Whether the edit changes an external dependency instead of the primary file.</param>
    /// <param name="preview">Whether preview demand rather than scene save initiates the request.</param>
    /// <returns>The asynchronous explicit-reimport boundary regression.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    [DataRow(false, false)]
    [DataRow(false, true)]
    [DataRow(true, false)]
    [DataRow(true, true)]
    public async Task ImportedSourceChangesRequireExplicitReimport(bool externalBuffer, bool preview)
    {
        using var workspace = new TempWorkspace();
        var source = await WriteRetainedModelAsync(workspace, "Model", "gltf", this.TestContext.CancellationToken).ConfigureAwait(false);
        var path = Path.Combine(workspace.Root, "Content/SourceMedia/DCC/Model/model.gltf");
        var bufferPath = Path.Combine(Path.GetDirectoryName(path)!, "mesh.bin");
        if (externalBuffer)
        {
            var content = JsonNode.Parse(await File.ReadAllTextAsync(path, this.TestContext.CancellationToken).ConfigureAwait(false))!;
            var buffer = content["buffers"]![0]!;
            var uri = buffer["uri"]!.GetValue<string>();
            await File.WriteAllBytesAsync(bufferPath, Convert.FromBase64String(uri[(uri.IndexOf(',', StringComparison.Ordinal) + 1)..]), this.TestContext.CancellationToken).ConfigureAwait(false);
            buffer["uri"] = "mesh.bin";
            await File.WriteAllTextAsync(path, content.ToJsonString(), this.TestContext.CancellationToken).ConfigureAwait(false);
        }

        using var compatibility = Oxygen.Testing.TemporaryNativeArtifacts.ForInstalledEngine();
        var api = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), new ContentPipelineProcessRunner(), NullLogger<ImportToolContentPipelineApi>.Instance, compatibility);
        var service = CreateService(workspace, new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(api)), api, compatibility);
        var first = await service.CookAssetAsync(source, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = first.IsPublished.Should().BeTrue();
        var geometry = first.CookedAssets.First(static output => output.Kind == ContentCookAssetKind.Geometry).CookedAssetUri;
        AddGeometryNode(workspace, geometry, "Imported mesh");
        await workspace.WriteSceneAsync("Content/Scenes/Main.oscene.json").ConfigureAwait(false);
        var scene = new Uri("asset:///Content/Scenes/Main.oscene.json");
        var index = Path.Combine(workspace.Root, ".cooked/Content/container.index.bin");
        var before = await File.ReadAllBytesAsync(index, this.TestContext.CancellationToken).ConfigureAwait(false);
        if (externalBuffer)
        {
            var bytes = await File.ReadAllBytesAsync(bufferPath, this.TestContext.CancellationToken).ConfigureAwait(false);
            BitConverter.GetBytes(0.25f).CopyTo(bytes, 0);
            await File.WriteAllBytesAsync(bufferPath, bytes, this.TestContext.CancellationToken).ConfigureAwait(false);
        }
        else
        {
            await File.AppendAllTextAsync(path, " ", this.TestContext.CancellationToken).ConfigureAwait(false);
        }

        Task<ContentCookResult> AutomaticAsync() => preview
            ? service.CookPreviewAssetAsync(geometry, workspace.ProjectContext, this.TestContext.CancellationToken)
            : service.CookSavedAssetAsync(scene, workspace.ProjectContext, this.TestContext.CancellationToken);
        var blocked = await AutomaticAsync().ConfigureAwait(false);
        _ = blocked.Status.Should().Be(OperationStatus.Failed);
        _ = blocked.Diagnostics.Should().Contain(issue => issue.Code == "asset_import.reimport_required");
        _ = (await File.ReadAllBytesAsync(index, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Equal(before);
        _ = (await service.ReimportSourceAsync(source, workspace.ProjectContext, this.TestContext.CancellationToken).ConfigureAwait(false)).IsPublished.Should().BeTrue();
        var resumed = await AutomaticAsync().ConfigureAwait(false);
        _ = (resumed.IsPublished || resumed.IsUpToDate).Should().BeTrue(string.Join(Environment.NewLine, resumed.Diagnostics.Select(static issue => issue.Message)));
    }
}
