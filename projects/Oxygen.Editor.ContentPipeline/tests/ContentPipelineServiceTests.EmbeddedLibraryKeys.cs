// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Nodes;
using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Slots;
using Oxygen.Managed.Assets.Model;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Tests;

/// <summary>Preserves native dependency provenance across coincident virtual paths.</summary>
public sealed partial class ContentPipelineServiceTests
{
    /// <summary>Project priority resolves the same native material key for library and project geometry.</summary>
    /// <returns>The asynchronous native identity regression.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task LibraryGeometryUsesProjectPriorityForTheSameNativeMaterialKey()
    {
        using var materials = new TempWorkspace([new("Art", "Art")]);
        using var meshes = new TempWorkspace();
        using var consumer = new TempWorkspace([new("Content", "Content"), new("Art", "Art")]);
        const string material = "Art/Materials/Shared.omat.json";
        materials.WriteMaterial(material, "Library material");
        consumer.WriteMaterial(material, "Project material");
        using var compatibility = Oxygen.Testing.TemporaryNativeArtifacts.ForInstalledEngine();
        var api = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), new ContentPipelineProcessRunner(), NullLogger<ImportToolContentPipelineApi>.Instance, compatibility);
        var materialProducer = CreateService(materials, new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(api)), api, compatibility);
        var imported = await materialProducer.CookAssetAsync(new("asset:///" + material), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = imported.IsPublished.Should().BeTrue(string.Join(Environment.NewLine, imported.Diagnostics.Select(static issue => issue.TechnicalMessage ?? issue.Message)));
        var materialRoot = Path.Combine(materials.Root, ".cooked/Art");
        meshes.ContextService.Activate(meshes.ProjectContext with { LocalFolderMounts = [new("Materials", materialRoot)] });
        WriteAuthoredGeometry(meshes, withBuffer: false);
        var descriptor = JsonNode.Parse(meshes.ReadText("Content/Geometry/AuthoredCube.ogeo.json"))!;
        descriptor["lods"]![0]!["submeshes"]![0]!["material_ref"] = "/Art/Materials/Shared.omat";
        meshes.WriteText("Content/Geometry/AuthoredCube.ogeo.json", descriptor.ToJsonString());
        var producer = CreateService(meshes, new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(api)), api, compatibility);
        var geometry = await producer.CookAssetAsync(new("asset:///Content/Geometry/AuthoredCube.ogeo.json"), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = geometry.IsPublished.Should().BeTrue(string.Join(Environment.NewLine, geometry.Diagnostics.Select(static issue => issue.TechnicalMessage ?? issue.Message)));
        consumer.ContextService.Activate(consumer.ProjectContext with
        {
            LocalFolderMounts = [new("Materials", materialRoot), new("Meshes", Path.Combine(meshes.Root, ".cooked/Content"))],
        });
        AddGeometryNode(consumer, new("asset:///Content/Geometry/AuthoredCube.ogeo"), "Library mesh");
        await consumer.WriteSceneAsync("Content/Scenes/Main.oscene.json").ConfigureAwait(false);
        var service = CreateService(consumer, new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(api)), api, compatibility);
        var result = await service.CookCurrentSceneAsync(new("asset:///Content/Scenes/Main.oscene.json"), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = result.IsPublished.Should().BeTrue(string.Join(Environment.NewLine, result.Diagnostics.Select(static issue => issue.TechnicalMessage ?? issue.Message)));
        _ = result.InputSnapshot!.CookedDependencies.Should().NotContain(input => input.SourceName == "Materials");
        _ = result.CookedAssets.Should().Contain(asset => asset.SourceAssetUri == new Uri("asset:///" + material));
        await this.VerifySharedProjectConsumersAsync(consumer, materials, service, materialProducer).ConfigureAwait(false);
    }

    private async Task VerifySharedProjectConsumersAsync(TempWorkspace consumer, TempWorkspace materials, ContentPipelineService service, ContentPipelineService materialProducer)
    {
        var second = new Scene(consumer.Project) { Name = "Project material" };
        var node = new SceneNode(second) { Name = "Project cube" };
        var geometry = new GeometryComponent { Name = "Geometry", Geometry = new AssetReference<GeometryAsset>(AssetUris.BuildGeneratedUri("BasicShapes/Cube")) };
        geometry.OverrideSlots.Add(await CreateBuiltinMaterialSlotAsync(
            consumer, geometry, new("asset:///Art/Materials/Shared.omat.json"), this.TestContext.CancellationToken).ConfigureAwait(false));
        _ = node.AddComponent(geometry);
        second.RootNodes.Add(node);
        var stream = File.Create(Path.Combine(consumer.Root, "Content/Scenes/Project.oscene.json"));
        await using (stream.ConfigureAwait(false))
        {
            await new SceneSerializer(consumer.Project).SerializeAsync(stream, second).ConfigureAwait(false);
        }

        var folder = new Uri("asset:///Content/Scenes");
        var uris = new[] { new Uri("asset:///Content/Scenes/Main.oscene.json"), new Uri("asset:///Content/Scenes/Project.oscene.json") };
        _ = (await service.CookFolderAsync(folder, this.TestContext.CancellationToken).ConfigureAwait(false)).IsPublished.Should().BeTrue();
        var project = consumer.ContextService.ActiveProject!;
        var states = await service.ReadAsync(project, uris, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = states.Should().OnlyContain(state => state.Freshness == AssetCookFreshness.Current);
        WriteMaterialRoughness(consumer, "Art/Materials/Shared.omat.json", 0.77);
        states = await service.ReadAsync(project, uris, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = states.Should().OnlyContain(state => state.Freshness == AssetCookFreshness.OutOfDate);
        _ = (await service.CookFolderAsync(folder, this.TestContext.CancellationToken).ConfigureAwait(false)).IsPublished.Should().BeTrue();
        WriteMaterialRoughness(materials, "Art/Materials/Shared.omat.json", 0.2);
        _ = (await materialProducer.CookAssetAsync(new("asset:///Art/Materials/Shared.omat.json"), this.TestContext.CancellationToken).ConfigureAwait(false)).IsPublished.Should().BeTrue();
        states = await service.ReadAsync(project, uris, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = states.Should().OnlyContain(state => state.Freshness == AssetCookFreshness.Current);
    }
}
