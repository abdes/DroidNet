// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Nodes;
using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Editor.ContentPipeline.TestSupport;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Compatibility;
using Oxygen.Managed.Core.Diagnostics;
using static Oxygen.Editor.ContentPipeline.TestSupport.CookScenario;
using static Oxygen.Editor.ContentPipeline.TestSupport.DependencyScenario;
using static Oxygen.Editor.ContentPipeline.TestSupport.RetainedModelScenario;

namespace Oxygen.Editor.ContentPipeline.Integration.Tests.Import;

[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed class ImportedOutputOwnershipTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>
    /// A model owns exactly the identities named under it: every product is flat under the model's identity, and the
    /// scene is named after the model file.
    /// </summary>
    /// <returns>The asynchronous identity-ownership test.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task ImportedOutputsAreNamedUnderTheirModel()
    {
        using var workspace = new CookWorkspace(engineContent: EngineContentPak.Installed());
        var source = await WriteRetainedModelAsync(workspace, "Model", "gltf", this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = await WriteRetainedModelAsync(workspace, "Other", "gltf", this.TestContext.CancellationToken).ConfigureAwait(false);
        var cook = NativeCook.Create(workspace);
        var service = cook.Service;

        var result = await service.CookAssetAsync(source, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = result.IsPublished.Should().BeTrue(string.Join(Environment.NewLine, result.Diagnostics.Select(static issue => issue.TechnicalMessage ?? issue.Message)));
        _ = result.CookedAssets.Should().NotBeEmpty();
        _ = result.CookedAssets.Should().OnlyContain(asset => asset.CookedAssetUri.Parent == source && asset.SourceAssetUri == source);
        _ = result.CookedAssets.Should().Contain(asset => asset.CookedAssetUri == source.Combine("model.oscene"));
    }

    /// <summary>Rewriting a producer binary keeps the cooked output reused: reuse depends on the inputs alone.</summary>
    /// <returns>The asynchronous producer-rewrite regression.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task RewritingAProducerBinaryKeepsTheProductReused()
    {
        using var workspace = new CookWorkspace(engineContent: EngineContentPak.Installed());
        var source = await WriteRetainedModelAsync(workspace, "Model", "gltf", this.TestContext.CancellationToken).ConfigureAwait(false);
        var policy = Path.Combine(workspace.Root, "test-producer-policy");
        await File.WriteAllTextAsync(policy, "v1", this.TestContext.CancellationToken).ConfigureAwait(false);
        using var firstCompatibility = Oxygen.Managed.Core.Testing.TemporaryNativeArtifacts.ForInstalledEngine([new("test/policy", policy)]);
        var firstRunner = new CountingSourceRunner();
        var firstApi = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), firstRunner, NullLogger<ImportToolContentPipelineApi>.Instance, firstCompatibility);
        var first = await CreateService(workspace, firstApi, compatibility: firstCompatibility).CookAssetAsync(source, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = first.IsPublished.Should().BeTrue();
        await File.WriteAllTextAsync(policy, "v2", this.TestContext.CancellationToken).ConfigureAwait(false);
        using var changedCompatibility = Oxygen.Managed.Core.Testing.TemporaryNativeArtifacts.ForInstalledEngine([new("test/policy", policy)]);
        var changedRunner = new CountingSourceRunner();
        var changedApi = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), changedRunner, NullLogger<ImportToolContentPipelineApi>.Instance, changedCompatibility);
        var service = CreateService(workspace, changedApi, compatibility: changedCompatibility);
        var geometry = first.CookedAssets.First(static asset => asset.Kind == ContentCookAssetKind.Geometry).CookedAssetUri;
        var result = await service.CookAssetAsync(geometry, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = result.IsUpToDate.Should().BeTrue(string.Join(Environment.NewLine, result.Diagnostics.Select(static issue => issue.Message)));
        _ = changedRunner.ImportCount.Should().Be(0);
    }

    /// <summary>Every named native output resolves back to one retained source and reuses its verified publication.</summary>
    /// <returns>The asynchronous imported-output request test.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task ImportedOutputCookReusesItsRetainedSource()
    {
        using var workspace = new CookWorkspace(engineContent: EngineContentPak.Installed());
        var source = await WriteRetainedModelAsync(workspace, "Model", "gltf", this.TestContext.CancellationToken).ConfigureAwait(false);
        var runner = new CountingSourceRunner();
        var cook = NativeCook.Create(workspace, runner: runner);
        var service = cook.Service;
        var first = await service.CookAssetAsync(source, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = first.IsPublished.Should().BeTrue();
        var count = runner.ImportCount;
        foreach (var output in first.CookedAssets)
        {
            var result = await service.CookAssetAsync(output.CookedAssetUri, this.TestContext.CancellationToken).ConfigureAwait(false);
            _ = result.IsUpToDate.Should().BeTrue(string.Join(Environment.NewLine, result.Diagnostics.Select(static issue => issue.Message)));
            _ = result.ReusedAssets.Should().Contain(asset => asset.CookedAssetUri == output.CookedAssetUri && asset.SourceAssetUri == source);
        }

        _ = runner.ImportCount.Should().Be(count);
    }

    /// <summary>Saved model import records resolve named outputs and scene dependencies in a project with no derived metadata.</summary>
    /// <param name="scope">The requested scope.</param>
    /// <returns>The asynchronous clean-project imported-output regression.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    [DataRow("asset")]
    [DataRow("folder")]
    [DataRow("scene")]
    public async Task ImportedOutputResolvesInCleanProject(string scope)
    {
        using var original = new CookWorkspace(engineContent: EngineContentPak.Installed());
        using var clean = new CookWorkspace(engineContent: EngineContentPak.Installed());
        var source = await WriteRetainedModelAsync(original, "Model", "gltf", this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = await WriteRetainedModelAsync(clean, "Model", "gltf", this.TestContext.CancellationToken).ConfigureAwait(false);
        var compatibility = EditorNativeCompatibilityService.ForCooking();
        var api = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), new ContentPipelineProcessRunner(), NullLogger<ImportToolContentPipelineApi>.Instance, compatibility);
        var first = await CreateService(original, api, compatibility: compatibility).CookAssetAsync(source, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = first.IsPublished.Should().BeTrue();
        var geometry = first.CookedAssets.First(static output => output.Kind == ContentCookAssetKind.Geometry);
        AddGeometryNode(clean, geometry.CookedAssetUri, "Imported mesh");
        await clean.WriteSceneAsync("Content/Scenes/Main.oscene.json").ConfigureAwait(false);
        var service = CreateService(clean, api, compatibility: compatibility);
        var result = await (scope switch
        {
            "asset" => service.CookAssetAsync(geometry.CookedAssetUri, this.TestContext.CancellationToken),
            "folder" => service.CookFolderAsync(source.Parent!, this.TestContext.CancellationToken),
            _ => service.CookCurrentSceneAsync(AssetPath.Parse("/Content/Scenes/Main.oscene"), this.TestContext.CancellationToken),
        }).ConfigureAwait(false);
        _ = result.IsPublished.Should().BeTrue(string.Join(Environment.NewLine, result.Diagnostics.Select(static issue => issue.TechnicalMessage ?? issue.Message)));
        _ = result.CookedAssets.Should().Contain(asset => asset.CookedAssetUri == geometry.CookedAssetUri && asset.SourceAssetUri == source);
        _ = result.InputsAreCurrent.Should().BeTrue();
        _ = result.InputSnapshot!.Inputs.Should().Contain(input => input.RelativePath.StartsWith("imports/", StringComparison.Ordinal) && input.RelativePath.EndsWith(".import.json", StringComparison.Ordinal));
    }

    /// <summary>A requested name must actually exist in native output; being named under the model is insufficient.</summary>
    /// <param name="alreadyCooked">Whether the model already has reusable output.</param>
    /// <returns>The asynchronous missing imported-output regression.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    [DataRow(false)]
    [DataRow(true)]
    public async Task ImportedOutputMissingNameCannotPublish(bool alreadyCooked)
    {
        using var workspace = new CookWorkspace(engineContent: EngineContentPak.Installed());
        var source = await WriteRetainedModelAsync(workspace, "Model", "gltf", this.TestContext.CancellationToken).ConfigureAwait(false);
        var cook = NativeCook.Create(workspace);
        var service = cook.Service;
        var index = CookPublicationPaths.Head(workspace.Root);
        byte[]? before = null;
        if (alreadyCooked)
        {
            _ = (await service.CookAssetAsync(source, this.TestContext.CancellationToken).ConfigureAwait(false)).IsPublished.Should().BeTrue();
            before = await File.ReadAllBytesAsync(index, this.TestContext.CancellationToken).ConfigureAwait(false);
        }

        var missing = source.Combine("Missing.ogeo");
        var result = await service.CookAssetAsync(missing, this.TestContext.CancellationToken).ConfigureAwait(false);
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
        using var workspace = new CookWorkspace(engineContent: EngineContentPak.Installed());
        var source = await WriteRetainedModelAsync(workspace, "Model", "gltf", this.TestContext.CancellationToken).ConfigureAwait(false);
        var runner = new CountingSourceRunner();
        var cook = NativeCook.Create(workspace, runner: runner);
        var service = cook.Service;
        var first = await service.CookAssetAsync(source, this.TestContext.CancellationToken).ConfigureAwait(false);
        var uris = first.CookedAssets.Select(static output => output.CookedAssetUri).Prepend(source).ToArray();
        var count = runner.Count;
        var statuses = await service.ReadAsync(workspace.ProjectContext, uris, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = statuses.Select(static status => status.AssetUri).Should().BeEquivalentTo(uris);
        _ = statuses.Should().OnlyContain(static status => status.Freshness == AssetCookFreshness.Current && status.HasAvailableOutput);
        await File.AppendAllTextAsync(Path.Combine(workspace.Root, source.VirtualPath.TrimStart('/')), " ", this.TestContext.CancellationToken).ConfigureAwait(false);
        statuses = await service.ReadAsync(workspace.ProjectContext, uris, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = statuses.Should().OnlyContain(static status => status.Freshness == AssetCookFreshness.OutOfDate && status.HasPublishedOutput);
        _ = statuses.SelectMany(static status => status.Diagnostics).Should().NotContain(static issue => issue.Severity == DiagnosticSeverity.Error);
        using var selected = await workspace.Publication.AcquireReadAsync(workspace.ProjectContext, this.TestContext.CancellationToken).ConfigureAwait(false);
        var incomplete = await new global::Oxygen.Editor.ContentPipeline.Snapshots.CookDependencyDiscovery(new AcceptedCookSourceFacts(workspace.ProjectContext, selected.ProductState, CookSavedSourceReader.HashFileAsync)).DiscoverAsync(workspace.ProjectContext, [CookInputResolver.Resolve(workspace.ProjectContext, source, ContentCookInputRole.Primary)], this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = incomplete.SourcesNeedingAnalysis.Should().Contain(source);
        _ = incomplete.Diagnostics.Should().BeEmpty();
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
        using var workspace = new CookWorkspace(engineContent: EngineContentPak.Installed());
        var source = await WriteRetainedModelAsync(workspace, "Model", "gltf", this.TestContext.CancellationToken).ConfigureAwait(false);
        var path = Path.Combine(workspace.Root, source.VirtualPath.TrimStart('/'));
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

        var cook = NativeCook.Create(workspace);
        var service = cook.Service;
        var first = await service.CookAssetAsync(source, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = first.IsPublished.Should().BeTrue();
        var geometry = first.CookedAssets.First(static output => output.Kind == ContentCookAssetKind.Geometry).CookedAssetUri;
        AddGeometryNode(workspace, geometry, "Imported mesh");
        await workspace.WriteSceneAsync("Content/Scenes/Main.oscene.json").ConfigureAwait(false);
        var scene = AssetPath.Parse("/Content/Scenes/Main.oscene");
        var index = Path.Combine(workspace.CookedRoot("Content"), "container.index.bin");
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
