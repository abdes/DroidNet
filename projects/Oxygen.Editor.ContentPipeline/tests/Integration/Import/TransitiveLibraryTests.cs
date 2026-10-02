// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Nodes;
using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Editor.ContentPipeline.TestSupport;
using Oxygen.Managed.Core.Compatibility;
using static Oxygen.Editor.ContentPipeline.TestSupport.CookScenario;
using static Oxygen.Editor.ContentPipeline.TestSupport.DependencyScenario;

namespace Oxygen.Editor.ContentPipeline.Integration.Tests.Import;

[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Naming", "CA1707:Identifiers should not contain underscores", Justification = "Scenario-based MSTest method names separate the operation and expected behavior.")]
public sealed class TransitiveLibraryTests
{
    public TestContext TestContext { get; set; } = null!;
    /// <summary>A library geometry's material resolves through the project's saved source priority.</summary>
    /// <param name="projectWins">Whether project output has higher priority than the material library.</param>
    /// <param name="materialLibraryMounted">Whether a library index can name the dependent material.</param>
    /// <param name="prewarm">Whether background metadata refresh runs before the first cook.</param>
    /// <returns>The asynchronous native priority regression.</returns>
    [TestMethod]
    [DataRow(true, true, false)]
    [DataRow(false, true, false)]
    [DataRow(true, false, false)]
    [DataRow(true, false, true)]
    [TestCategory("NativeContent")]
    public async Task CookedGeometryDiscoversProjectMaterialOverride(bool projectWins, bool materialLibraryMounted, bool prewarm)
    {
        using var library = new CookWorkspace([new("Content", "Content"), new("Art", "Art")]);
        using var consumer = new CookWorkspace([new("Content", "Content"), new("Art", "Art")]);
        const string material = "Art/Materials/Shared.omat.json";
        library.WriteMaterial(material, "Shared");
        consumer.WriteMaterial(material, "Project shared");
        WriteAuthoredGeometry(library, withBuffer: false);
        var geometry = new Uri("asset:///Content/Geometry/AuthoredCube.ogeo.json");
        var descriptor = JsonNode.Parse(library.ReadText("Content/Geometry/AuthoredCube.ogeo.json"))!;
        descriptor["lods"]![0]!["submeshes"]![0]!["material_ref"] = "/" + material[..^5];
        library.WriteText("Content/Geometry/AuthoredCube.ogeo.json", descriptor.ToJsonString());
        using var compatibility = EditorNativeCompatibilityService.ForCooking();
        var runner = new CountingSourceRunner();
        var api = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), runner, NullLogger<ImportToolContentPipelineApi>.Instance, compatibility);
        var producer = CreateService(library, new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(api)), api, compatibility);
        _ = (await producer.CookAssetAsync(geometry, this.TestContext.CancellationToken).ConfigureAwait(false)).IsPublished.Should().BeTrue();
        var context = consumer.ProjectContext with
        {
            LocalFolderMounts = materialLibraryMounted
                ? [new("Materials", await library.ExportRootAsync("Art", this.TestContext.CancellationToken).ConfigureAwait(false)), new("Meshes", library.CookedRoot("Content"))]
                : [new("Meshes", library.CookedRoot("Content"))],
            CookedContentOrder = projectWins ? [] : [new(Oxygen.Editor.World.CookedContentSourceKind.ProjectOutput), new(Oxygen.Editor.World.CookedContentSourceKind.LocalFolder, "Materials"), new(Oxygen.Editor.World.CookedContentSourceKind.LocalFolder, "Meshes")],
        };
        consumer.Activate(context);
        AddGeometryNode(consumer, new("asset:///Content/Geometry/AuthoredCube.ogeo"), "Library mesh");
        await consumer.WriteSceneAsync("Content/Scenes/Main.oscene.json").ConfigureAwait(false);
        var service = CreateService(consumer, new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(api)), api, compatibility);
        var scene = new Uri("asset:///Content/Scenes/Main.oscene.json");
        if (prewarm)
        {
            await this.VerifyBackgroundKeyLookupAsync(service, consumer, scene, runner).ConfigureAwait(false);
        }

        var cooked = await service.CookCurrentSceneAsync(scene, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = cooked.IsPublished.Should().BeTrue(string.Join(Environment.NewLine, cooked.Diagnostics.Select(static issue => issue.TechnicalMessage ?? issue.Message)));
        var materialUri = new Uri("asset:///" + material[..^5]);
        if (projectWins)
        {
            _ = cooked.CookedAssets.Should().Contain(asset => asset.SourceAssetUri == new Uri("asset:///" + material));
            _ = cooked.InputSnapshot!.CookedDependencies.Should().NotContain(input => input.AssetUri == materialUri);
        }
        else
        {
            _ = cooked.InputSnapshot!.CookedDependencies.Should().Contain(input => input.AssetUri == materialUri && input.SourceName == "Materials");
            _ = cooked.CookedAssets.Should().NotContain(asset => asset.SourceAssetUri == new Uri("asset:///" + material));
        }

        _ = (await service.ReadAsync(context, [scene], this.TestContext.CancellationToken).ConfigureAwait(false)).Single().Freshness.Should().Be(AssetCookFreshness.Current);
        WriteMaterialRoughness(library, material, 0.93);
        _ = (await producer.CookAssetAsync(new("asset:///" + material), this.TestContext.CancellationToken).ConfigureAwait(false)).IsPublished.Should().BeTrue();
        _ = await library.ExportRootAsync("Art", this.TestContext.CancellationToken).ConfigureAwait(false);
        var status = (await service.ReadAsync(context, [scene], this.TestContext.CancellationToken).ConfigureAwait(false)).Single();
        _ = status.Freshness.Should().Be(projectWins ? AssetCookFreshness.Current : AssetCookFreshness.OutOfDate);
        _ = (await service.CookCurrentSceneAsync(scene, this.TestContext.CancellationToken).ConfigureAwait(false)).Status.Should().Be(Oxygen.Managed.Core.Diagnostics.OperationStatus.Succeeded);
        WriteMaterialRoughness(consumer, material, 0.13);
        status = (await service.ReadAsync(context, [scene], this.TestContext.CancellationToken).ConfigureAwait(false)).Single();
        _ = status.Freshness.Should().Be(projectWins ? AssetCookFreshness.OutOfDate : AssetCookFreshness.Current);
    }

    /// <summary>A consuming scene tracks the material library even though its authored reference names only geometry.</summary>
    /// <returns>The asynchronous transitive-library regression.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task CookedGeometryTracksItsSeparateMaterialLibrary()
    {
        using var library = new CookWorkspace([new("Content", "Content"), new("Art", "Art")]);
        using var consumer = new CookWorkspace();
        const string material = "Art/Materials/Shared.omat.json";
        library.WriteMaterial(material, "Shared");
        WriteAuthoredGeometry(library, withBuffer: false);
        var geometry = new Uri("asset:///Content/Geometry/AuthoredCube.ogeo.json");
        var descriptor = JsonNode.Parse(library.ReadText("Content/Geometry/AuthoredCube.ogeo.json"))!;
        descriptor["lods"]![0]!["submeshes"]![0]!["material_ref"] = "/" + material[..^5];
        library.WriteText("Content/Geometry/AuthoredCube.ogeo.json", descriptor.ToJsonString());
        using var compatibility = EditorNativeCompatibilityService.ForCooking();
        var runner = new CountingSourceRunner();
        var api = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), runner, NullLogger<ImportToolContentPipelineApi>.Instance, compatibility);
        var producer = CreateService(library, new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(api)), api, compatibility);
        var exported = await producer.CookAssetAsync(geometry, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = exported.IsPublished.Should().BeTrue();
        var context = consumer.ProjectContext with
        {
            LocalFolderMounts = [new("Materials", await library.ExportRootAsync("Art", this.TestContext.CancellationToken).ConfigureAwait(false)), new("Meshes", library.CookedRoot("Content"))],
        };
        consumer.Activate(context);
        AddGeometryNode(consumer, new("asset:///Content/Geometry/AuthoredCube.ogeo"), "Library mesh");
        await consumer.WriteSceneAsync("Content/Scenes/Main.oscene.json").ConfigureAwait(false);
        var service = CreateService(consumer, new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(api)), api, compatibility);
        var scene = new Uri("asset:///Content/Scenes/Main.oscene.json");
        var cooked = await service.CookCurrentSceneAsync(scene, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = cooked.IsPublished.Should().BeTrue(string.Join(Environment.NewLine, cooked.Diagnostics.Select(static issue => issue.TechnicalMessage ?? issue.Message)));

        var source = JsonNode.Parse(library.ReadText(material))!;
        source["parameters"]!["roughness"] = 0.9;
        library.WriteText(material, source.ToJsonString());
        _ = (await producer.CookAssetAsync(new("asset:///" + material), this.TestContext.CancellationToken).ConfigureAwait(false)).IsPublished.Should().BeTrue();
        _ = await library.ExportRootAsync("Art", this.TestContext.CancellationToken).ConfigureAwait(false);
        var workers = runner.Count;
        var status = await service.ReadAsync(context, [scene], this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = runner.Count.Should().Be(workers, "ordinary status reads must not start native inspection");
        _ = status.Single().Freshness.Should().NotBe(AssetCookFreshness.Current);
        _ = cooked.InputSnapshot!.CookedDependencies.Select(static input => input.SourceName).Should().Contain("Materials");
        var runs = consumer.CookCoordinator.Runs.Count;
        _ = (await service.RefreshLibraryMetadataAsync(context, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().BeTrue();
        _ = consumer.CookCoordinator.Runs.Count.Should().Be(runs, "background inspection is not a cooking operation");
        workers = runner.Count;
        _ = (await service.RefreshLibraryMetadataAsync(context, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().BeFalse();
        _ = runner.Count.Should().Be(workers, "the verified library hash should reuse the cached report");
        _ = (await service.CookCurrentSceneAsync(scene, this.TestContext.CancellationToken).ConfigureAwait(false)).IsPublished.Should().BeTrue();
        _ = (await service.ReadAsync(context, [scene], this.TestContext.CancellationToken).ConfigureAwait(false)).Single().Freshness.Should().Be(AssetCookFreshness.Current);
    }

    private async Task VerifyBackgroundKeyLookupAsync(ContentPipelineService service, CookWorkspace consumer, Uri scene, CountingSourceRunner runner)
    {
        var project = consumer.ContextService.ActiveProject!;
        var workers = runner.Count;
        var runs = consumer.CookCoordinator.Runs.Count;
        _ = await service.ReadAsync(project, [scene], this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = runner.Count.Should().Be(workers);
        _ = (await service.RefreshLibraryMetadataAsync(project, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().BeTrue();
        _ = consumer.CookCoordinator.Runs.Count.Should().Be(runs);
        workers = runner.Count;
        _ = await service.ReadAsync(project, [scene], this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = (await service.RefreshLibraryMetadataAsync(project, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().BeFalse();
        _ = runner.Count.Should().Be(workers, "unchanged candidates and status reads must reuse native metadata");
    }
}
