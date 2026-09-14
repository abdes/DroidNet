// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Nodes;
using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentPipeline.Status;

namespace Oxygen.Editor.ContentPipeline.Tests;

/// <summary>Exercises material dependencies reached through geometry supplied by another cooked library.</summary>
public sealed partial class ContentPipelineServiceTests
{
    /// <summary>A consuming scene tracks the material library even though its authored reference names only geometry.</summary>
    /// <returns>The asynchronous transitive-library regression.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task CookedGeometryTracksItsSeparateMaterialLibrary()
    {
        using var library = new TempWorkspace([new("Content", "Content"), new("Art", "Art")]);
        using var consumer = new TempWorkspace();
        const string material = "Art/Materials/Shared.omat.json";
        library.WriteMaterial(material, "Shared");
        WriteAuthoredGeometry(library, withBuffer: false);
        var geometry = new Uri("asset:///Content/Geometry/AuthoredCube.ogeo.json");
        var descriptor = JsonNode.Parse(library.ReadText("Content/Geometry/AuthoredCube.ogeo.json"))!;
        descriptor["lods"]![0]!["submeshes"]![0]!["material_ref"] = "/" + material[..^5];
        library.WriteText("Content/Geometry/AuthoredCube.ogeo.json", descriptor.ToJsonString());
        using var compatibility = Oxygen.Testing.TemporaryNativeArtifacts.ForInstalledEngine();
        var runner = new CountingSourceRunner();
        var api = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), runner, NullLogger<ImportToolContentPipelineApi>.Instance, compatibility);
        var producer = CreateService(library, new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(api)), api, compatibility);
        var exported = await producer.CookAssetAsync(geometry, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = exported.IsPublished.Should().BeTrue();
        var context = consumer.ProjectContext with
        {
            LocalFolderMounts = [new("Materials", Path.Combine(library.Root, ".cooked/Art")), new("Meshes", Path.Combine(library.Root, ".cooked/Content"))],
        };
        consumer.ContextService.Activate(context);
        AddGeometryNode(consumer, new("asset:///Content/Geometry/AuthoredCube.ogeo"), "Library mesh");
        await consumer.WriteSceneAsync("Content/Scenes/Main.oscene.json").ConfigureAwait(false);
        var service = CreateService(consumer, new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(api)), api, compatibility);
        var scene = new Uri("asset:///Content/Scenes/Main.oscene.json");
        var cooked = await service.CookCurrentSceneAsync(scene, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = cooked.IsPublished.Should().BeTrue(string.Join(Environment.NewLine, cooked.Diagnostics.Select(static issue => issue.TechnicalMessage ?? issue.Message)));

        var source = JsonNode.Parse(library.ReadText(material))!;
        source["PbrMetallicRoughness"]!["RoughnessFactor"] = 0.9;
        library.WriteText(material, source.ToJsonString());
        _ = (await producer.CookAssetAsync(new("asset:///" + material), this.TestContext.CancellationToken).ConfigureAwait(false)).IsPublished.Should().BeTrue();
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
}
