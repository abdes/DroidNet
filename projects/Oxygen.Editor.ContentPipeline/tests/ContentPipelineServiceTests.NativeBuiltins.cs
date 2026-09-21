// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using System.Runtime.ExceptionServices;
using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Slots;
using Oxygen.Managed.Assets.Model;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline.Tests;

/// <summary>Exercises all built-ins through the real descriptor/catalog and native cook entry points.</summary>
public sealed partial class ContentPipelineServiceTests
{
    /// <summary>Gets or sets cancellation for the current test.</summary>
    public TestContext TestContext { get; set; } = null!;

    /// <summary>A first project cook resolves assigned materials without a preceding Materials-folder cook.</summary>
    /// <returns>The asynchronous native test.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task FirstProjectCookIncludesAssignedMaterialDependencies()
    {
        using var workspace = new TempWorkspace();
        workspace.WriteMaterial("Content/Materials/Blue.omat.json", "Blue");
        var materialUri = new Uri("asset:///Content/Materials/Blue.omat.json");
        var node = new SceneNode(workspace.Scene) { Name = "Assigned material" };
        var geometry = new GeometryComponent
        {
            Name = "Geometry",
            Geometry = new AssetReference<GeometryAsset>(AssetUris.BuildGeneratedUri("BasicShapes/Cube")),
        };
        geometry.OverrideSlots.Add(new MaterialsSlot { Material = new AssetReference<MaterialAsset>(materialUri) });
        _ = node.AddComponent(geometry);
        workspace.Scene.RootNodes.Add(node);
        workspace.Scene.SetEnvironment(new SceneEnvironmentData
        {
            PostProcess = new PostProcessEnvironmentData { ExposureMode = ExposureMode.Auto },
        });
        await workspace.WriteSceneAsync("Content/Scenes/Main.oscene.json").ConfigureAwait(false);
        _ = Directory.EnumerateFiles(Path.Combine(workspace.Root, ".cooked"), "*", SearchOption.AllDirectories).Should().BeEmpty();
        using var compatibility = Oxygen.Testing.TemporaryNativeArtifacts.ForInstalledEngine();
        var api = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), new ContentPipelineProcessRunner(), NullLogger<ImportToolContentPipelineApi>.Instance, compatibility);
        var pipeline = CreateService(workspace, new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(api)), api, compatibility);
        var result = await pipeline.CookProjectAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = result.Status.Should().BeOneOf([OperationStatus.Succeeded, OperationStatus.SucceededWithWarnings], string.Join(Environment.NewLine, result.Diagnostics.Select(static diagnostic => diagnostic.TechnicalMessage ?? diagnostic.Message)));
        _ = result.CookedAssets.Should().Contain(asset => asset.SourceAssetUri == materialUri);
        _ = workspace.CookCoordinator.Runs.Single().Assets.Values.Should().Contain(asset => asset.AssetUri == materialUri && asset.State == CookAssetState.Updated);
    }

    /// <summary>Both scene and project cooking accept every supported engine shape and preserve authored identities.</summary>
    /// <param name="projectCook">Whether to exercise the complete project entry point.</param>
    /// <returns>The test task.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    [DataRow(false)]
    [DataRow(true)]
    public async Task EveryBuiltinCooksThroughNativeSceneAndProjectWorkflows(bool projectCook)
    {
        using var workspace = new TempWorkspace();
        using var compatibility = Oxygen.Testing.TemporaryNativeArtifacts.ForInstalledEngine();
        var api = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), new ContentPipelineProcessRunner(), NullLogger<ImportToolContentPipelineApi>.Instance, compatibility);
        var catalog = await api.GetBuiltinGeometryCatalogAsync(workspace.Root, "Content", this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = catalog.AuthoringGeometries.Should().HaveCount(10);
        foreach (var definition in catalog.AuthoringGeometries)
        {
            var node = new SceneNode(workspace.Scene) { Name = definition.Name };
            _ = node.AddComponent(new GeometryComponent { Name = "Geometry", Geometry = new AssetReference<GeometryAsset>(definition.AssetUri) });
            workspace.Scene.RootNodes.Add(node);
        }

        workspace.Scene.SetEnvironment(new SceneEnvironmentData
        {
            PostProcess = new PostProcessEnvironmentData { ExposureMode = ExposureMode.Auto },
        });
        await workspace.WriteSceneAsync("Content/Scenes/Main.oscene.json").ConfigureAwait(false);
        var pipeline = CreateService(workspace, new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(api)), api, compatibility);
        var result = projectCook
            ? await pipeline.CookProjectAsync(this.TestContext.CancellationToken).ConfigureAwait(false)
            : await pipeline.CookCurrentSceneAsync(new Uri("asset:///Content/Scenes/Main.oscene.json"), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = result.Status.Should().BeOneOf(OperationStatus.Succeeded, OperationStatus.SucceededWithWarnings);
        _ = result.Validation.Should().NotBeNull();
        _ = result.Validation!.Succeeded.Should().BeTrue();
        foreach (var definition in catalog.AuthoringGeometries)
        {
            _ = result.CookedAssets.Should().Contain(asset => asset.SourceAssetUri == definition.AssetUri);
            var path = Path.Combine(workspace.Root, ".cooked", definition.Contribution.VirtualPath.TrimStart('/').Replace('/', Path.DirectorySeparatorChar));
            _ = File.Exists(path).Should().BeTrue(definition.Name);
        }

        _ = Directory.GetFiles(Path.Combine(workspace.Root, ".pipeline", "Catalogs")).Should().BeEmpty();
        var repeated = projectCook
            ? await pipeline.CookProjectAsync(this.TestContext.CancellationToken).ConfigureAwait(false)
            : await pipeline.CookCurrentSceneAsync(new Uri("asset:///Content/Scenes/Main.oscene.json"), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = repeated.IsUpToDate.Should().BeTrue();
        foreach (var definition in catalog.AuthoringGeometries)
        {
            _ = repeated.ReusedAssets.Should().Contain(asset => asset.SourceAssetUri == definition.AssetUri);
        }

        await this.VerifyBuiltinPublicationOriginsAsync(pipeline, workspace, catalog).ConfigureAwait(false);
    }

    private async Task VerifyBuiltinPublicationOriginsAsync(ContentPipelineService pipeline, TempWorkspace workspace, BuiltinGeometryCatalog catalog)
    {
        var identities = catalog.CreateCatalogRecords().Select(static record => record.Uri).ToArray();
        var runCount = workspace.CookCoordinator.Runs.Count;
        var statuses = await pipeline.ReadAsync(workspace.ProjectContext, identities, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = statuses.Should().HaveCount(11).And.OnlyContain(status => status.HasVerifiedOutput && status.HasPublishedOutput);
        _ = statuses.Should().OnlyContain(status => status.SourcePaths.IsEmpty && status.Diagnostics.IsEmpty && !status.Outputs.IsEmpty);
        _ = statuses.Should().OnlyContain(status => status.Outputs.All(output => output.SourceAssetUri == status.AssetUri));
        _ = workspace.CookCoordinator.Runs.Count.Should().Be(runCount, "origin discovery never cooks built-ins");
        var requested = identities.Concat(statuses.SelectMany(static status => status.Outputs.Select(static output => output.CookedAssetUri))).Distinct().ToArray();
        var missingSources = new ConcurrentQueue<string>();
        void Observe(object? sender, FirstChanceExceptionEventArgs args)
        {
            if (args.Exception is FileNotFoundException { FileName: { } path } && path.StartsWith(workspace.Root, StringComparison.OrdinalIgnoreCase))
            {
                missingSources.Enqueue(path);
            }
        }

        AppDomain.CurrentDomain.FirstChanceException += Observe;
        try
        {
            var combined = await pipeline.ReadAsync(workspace.ProjectContext, requested, this.TestContext.CancellationToken).ConfigureAwait(false);
            _ = missingSources.Should().BeEmpty("cooked built-in companions have engine-owned recipes, not authored JSON descriptors");
            _ = combined.Select(static status => status.AssetUri).Should().BeEquivalentTo(requested);
            _ = combined.Should().OnlyContain(status => status.HasVerifiedOutput && status.HasPublishedOutput && status.SourcePaths.IsEmpty && status.Diagnostics.IsEmpty);
        }
        finally
        {
            AppDomain.CurrentDomain.FirstChanceException -= Observe;
        }

        var shape = catalog.Geometries[0];
        var path = Path.Combine(workspace.Root, ".cooked", shape.Contribution.VirtualPath.TrimStart('/').Replace('/', Path.DirectorySeparatorChar));
        await File.WriteAllTextAsync(path, "unrelated replacement", this.TestContext.CancellationToken).ConfigureAwait(false);
        var changed = await pipeline.ReadAsync(workspace.ProjectContext, [shape.AssetUri, new Uri("asset://" + shape.Contribution.VirtualPath)], this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = changed.Should().HaveCount(2).And.OnlyContain(
            static status => status.HasPublishedOutput && !status.HasVerifiedOutput && status.SourcePaths.IsEmpty,
            "both identities retain the same owner, but corrupt output is never verified");

        workspace.WriteMaterial("Content/Materials/OxygenEditor_Default.omat.json", "Authored replacement");
        Uri authoredDefault = new("asset:///Content/Materials/OxygenEditor_Default.omat.json");
        var authored = await pipeline.ReadAsync(workspace.ProjectContext, [authoredDefault, new Uri("asset:///Content/Materials/OxygenEditor_Default.omat")], this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = authored.Should().HaveCount(2).And.OnlyContain(status => status.SourcePaths.Contains(Path.Combine(workspace.Root, "Content", "Materials", "OxygenEditor_Default.omat.json")));
        _ = authored.Select(static status => status.AssetUri).Should().Contain(authoredDefault);
    }
}
