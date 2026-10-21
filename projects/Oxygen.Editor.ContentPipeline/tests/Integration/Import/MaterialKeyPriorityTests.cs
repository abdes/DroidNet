// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Nodes;
using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Editor.ContentPipeline.TestSupport;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Compatibility;
using static Oxygen.Editor.ContentPipeline.TestSupport.CookScenario;
using static Oxygen.Editor.ContentPipeline.TestSupport.DependencyScenario;
using static Oxygen.Editor.ContentPipeline.TestSupport.IncrementalCookScenario;

namespace Oxygen.Editor.ContentPipeline.Integration.Tests.Import;

[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Naming", "CA1707:Identifiers should not contain underscores", Justification = "Scenario-based MSTest method names separate the operation and expected behavior.")]
public sealed class MaterialKeyPriorityTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Project priority resolves the same native material key for library and project geometry.</summary>
    /// <returns>The asynchronous native identity regression.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task LibraryGeometryUsesProjectPriorityForTheSameNativeMaterialKey()
    {
        using var materials = new CookWorkspace([new("Art", "Art")], EngineContentPak.Installed());
        using var meshes = new CookWorkspace(engineContent: EngineContentPak.Installed());
        using var consumer = new CookWorkspace([new("Content", "Content"), new("Art", "Art")], EngineContentPak.Installed());
        const string material = "Art/Materials/Shared.omat.json";
        materials.WriteMaterial(material, "Library material");
        consumer.WriteMaterial(material, "Project material");
        using var compatibility = EditorNativeCompatibilityService.ForCooking();
        var api = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), new ContentPipelineProcessRunner(), NullLogger<ImportToolContentPipelineApi>.Instance, compatibility);
        var materialProducer = CreateService(materials, api, compatibility: compatibility);
        var imported = await materialProducer.CookAssetAsync(AssetPath.Parse("/" + material[..^5]), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = imported.IsPublished.Should().BeTrue(string.Join(Environment.NewLine, imported.Diagnostics.Select(static issue => issue.TechnicalMessage ?? issue.Message)));
        var materialRoot = await materials.ExportRootAsync("Art", this.TestContext.CancellationToken).ConfigureAwait(false);
        meshes.Activate(meshes.ProjectContext with { LocalFolderMounts = [new("Materials", FileSystemPath.From(materialRoot))] });
        WriteAuthoredGeometry(meshes, withBuffer: false);
        var descriptor = JsonNode.Parse(meshes.ReadText("Content/Geometry/AuthoredCube.ogeo.json"))!;
        descriptor["lods"]![0]!["submeshes"]![0]!["material_ref"] = "/Art/Materials/Shared.omat";
        meshes.WriteText("Content/Geometry/AuthoredCube.ogeo.json", descriptor.ToJsonString());
        var producer = CreateService(meshes, api, compatibility: compatibility);
        var geometry = await producer.CookAssetAsync(AssetPath.Parse("/Content/Geometry/AuthoredCube.ogeo"), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = geometry.IsPublished.Should().BeTrue(string.Join(Environment.NewLine, geometry.Diagnostics.Select(static issue => issue.TechnicalMessage ?? issue.Message)));
        consumer.Activate(consumer.ProjectContext with
        {
            LocalFolderMounts = [new("Materials", FileSystemPath.From(materialRoot)), new("Meshes", FileSystemPath.From(meshes.CookedRoot("Content")))],
        });
        AddGeometryNode(consumer, AssetPath.Parse("/Content/Geometry/AuthoredCube.ogeo"), "Library mesh");
        await consumer.WriteSceneAsync("Content/Scenes/Main.oscene.json").ConfigureAwait(false);
        var service = CreateService(consumer, api, compatibility: compatibility);
        var result = await service.CookCurrentSceneAsync(AssetPath.Parse("/Content/Scenes/Main.oscene"), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = result.IsPublished.Should().BeTrue(string.Join(Environment.NewLine, result.Diagnostics.Select(static issue => issue.TechnicalMessage ?? issue.Message)));
        _ = result.InputSnapshot!.CookedDependencies.Should().NotContain(input => input.SourceName == "Materials");
        var materialUri = AssetPath.Parse("/" + material[..^5]);
        _ = result.CookedAssets.Should().Contain(asset => asset.SourceAssetUri == materialUri);
        await this.VerifySharedProjectConsumersAsync(consumer, materials, service, materialProducer, api).ConfigureAwait(false);
    }

    private async Task VerifySharedProjectConsumersAsync(CookWorkspace consumer, CookWorkspace materials, ContentPipelineService service, ContentPipelineService materialProducer, ImportToolContentPipelineApi api)
    {
        var second = new Scene { Name = "Project material" };
        var node = new SceneNode(second) { Name = "Project cube" };
        var geometry = new GeometryComponent { Name = "Geometry", Geometry = EngineAssets.BasicShape("Cube") };
        geometry.OverrideSlots.Add(await CreateEngineMaterialSlotAsync(
            consumer, api, geometry.Geometry, AssetPath.Parse("/Art/Materials/Shared.omat"), this.TestContext.CancellationToken).ConfigureAwait(false));
        _ = node.AddComponent(geometry);
        second.RootNodes.Add(node);
        var stream = File.Create(Path.Combine(consumer.Root, "Content/Scenes/Project.oscene.json"));
        await using (stream.ConfigureAwait(false))
        {
            await SceneSerializer.SerializeAsync(stream, second).ConfigureAwait(false);
        }

        var folder = AssetPath.Parse("/Content/Scenes");
        var uris = new[] { AssetPath.Parse("/Content/Scenes/Main.oscene"), AssetPath.Parse("/Content/Scenes/Project.oscene") };
        _ = (await service.CookFolderAsync(folder, this.TestContext.CancellationToken).ConfigureAwait(false)).IsPublished.Should().BeTrue();
        var project = consumer.Session.Context;
        var states = await service.ReadAsync(project, uris, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = states.Should().OnlyContain(state => state.Freshness == AssetCookFreshness.Current);
        WriteMaterialRoughness(consumer, "Art/Materials/Shared.omat.json", 0.77);
        states = await service.ReadAsync(project, uris, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = states.Should().OnlyContain(state => state.Freshness == AssetCookFreshness.OutOfDate);
        _ = (await service.CookFolderAsync(folder, this.TestContext.CancellationToken).ConfigureAwait(false)).IsPublished.Should().BeTrue();
        WriteMaterialRoughness(materials, "Art/Materials/Shared.omat.json", 0.2);
        _ = (await materialProducer.CookAssetAsync(AssetPath.Parse("/Art/Materials/Shared.omat"), this.TestContext.CancellationToken).ConfigureAwait(false)).IsPublished.Should().BeTrue();
        _ = await materials.ExportRootAsync("Art", this.TestContext.CancellationToken).ConfigureAwait(false);
        states = await service.ReadAsync(project, uris, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = states.Should().OnlyContain(state => state.Freshness == AssetCookFreshness.Current);
    }
}
