// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

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
            ExposureMode = ExposureMode.Auto,
            PostProcess = new PostProcessEnvironmentData { ExposureMode = ExposureMode.Auto },
        });
        await workspace.WriteSceneAsync("Content/Scenes/Main.oscene.json").ConfigureAwait(false);
        _ = Directory.EnumerateFiles(Path.Combine(workspace.Root, ".cooked"), "*", SearchOption.AllDirectories).Should().BeEmpty();
        using var qualification = Oxygen.Testing.TemporaryArtifactQualification.ForInstalledEngine();
        var api = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), new ContentPipelineProcessRunner(), NullLogger<ImportToolContentPipelineApi>.Instance, qualification);
        var pipeline = CreateService(workspace, new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(api)), api);
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
        using var qualification = Oxygen.Testing.TemporaryArtifactQualification.ForInstalledEngine();
        var api = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), new ContentPipelineProcessRunner(), NullLogger<ImportToolContentPipelineApi>.Instance, qualification);
        var catalog = await api.GetBuiltinGeometryCatalogAsync(workspace.Root, "Content", this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = catalog.Geometries.Should().HaveCount(11);
        foreach (var definition in catalog.Geometries)
        {
            var node = new SceneNode(workspace.Scene) { Name = definition.Name };
            _ = node.AddComponent(new GeometryComponent { Name = "Geometry", Geometry = new AssetReference<GeometryAsset>(definition.AssetUri) });
            workspace.Scene.RootNodes.Add(node);
        }

        workspace.Scene.SetEnvironment(new SceneEnvironmentData
        {
            ExposureMode = ExposureMode.Auto,
            PostProcess = new PostProcessEnvironmentData { ExposureMode = ExposureMode.Auto },
        });
        await workspace.WriteSceneAsync("Content/Scenes/Main.oscene.json").ConfigureAwait(false);
        var pipeline = CreateService(workspace, new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(api)), api);
        var result = projectCook
            ? await pipeline.CookProjectAsync(this.TestContext.CancellationToken).ConfigureAwait(false)
            : await pipeline.CookCurrentSceneAsync(new Uri("asset:///Content/Scenes/Main.oscene.json"), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = result.Status.Should().BeOneOf(OperationStatus.Succeeded, OperationStatus.SucceededWithWarnings);
        _ = result.Validation.Should().NotBeNull();
        _ = result.Validation!.Succeeded.Should().BeTrue();
        foreach (var definition in catalog.Geometries)
        {
            _ = result.CookedAssets.Should().Contain(asset => asset.SourceAssetUri == definition.AssetUri);
            var path = Path.Combine(workspace.Root, ".cooked", definition.Contribution.VirtualPath.TrimStart('/').Replace('/', Path.DirectorySeparatorChar));
            _ = File.Exists(path).Should().BeTrue(definition.Name);
        }

        _ = Directory.GetFiles(Path.Combine(workspace.Root, ".pipeline", "Catalogs")).Should().BeEmpty();
    }
}
