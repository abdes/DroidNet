// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Nodes;
using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Slots;
using Oxygen.Managed.Assets.Model;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Tests;

/// <summary>Exercises nested authored locations through native emission and consumer resolution.</summary>
public sealed partial class ContentPipelineServiceTests
{
    /// <summary>Same-named materials in different folders remain distinct and resolve through nested geometry and scene paths.</summary>
    /// <returns>The asynchronous nested-namespace regression.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task NestedDescriptorsPreservePathsAndDistinctMaterialIdentities()
    {
        using var workspace = new TempWorkspace();
        const string first = "Content/Materials/A/Shared.omat.json";
        const string second = "Content/Materials/B/Shared.omat.json";
        const string geometry = "Content/Meshes/Props/AuthoredCube.ogeo.json";
        const string scene = "Content/Scenes/Levels/Main.oscene.json";
        workspace.WriteMaterial(first, "Shared");
        workspace.WriteMaterial(second, "Shared");
        WriteAuthoredGeometry(workspace, withBuffer: false);
        var original = Path.Combine(workspace.Root, "Content/Geometry/AuthoredCube.ogeo.json");
        var descriptor = JsonNode.Parse(await File.ReadAllTextAsync(original, this.TestContext.CancellationToken).ConfigureAwait(false))!;
        descriptor["lods"]![0]!["submeshes"]![0]!["material_ref"] = "/" + first[..^5];
        workspace.WriteText(geometry, descriptor.ToJsonString());
        File.Delete(original);
        AddGeometryNode(workspace, new("asset:///" + geometry), "Mesh");
        workspace.Scene.RootNodes[^1].Components.OfType<GeometryComponent>().Single().OverrideSlots.Add(
            new MaterialsSlot { Material = new AssetReference<MaterialAsset>(new Uri("asset:///" + second)) });
        await workspace.WriteSceneAsync(scene).ConfigureAwait(false);
        using var compatibility = Oxygen.Testing.TemporaryNativeArtifacts.ForInstalledEngine();
        var api = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), new ContentPipelineProcessRunner(), NullLogger<ImportToolContentPipelineApi>.Instance, compatibility);
        var service = CreateService(workspace, new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(api)), api, compatibility);
        var result = await service.CookCurrentSceneAsync(new("asset:///" + scene), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = result.IsPublished.Should().BeTrue(string.Join(Environment.NewLine, result.Diagnostics.Select(static issue => issue.TechnicalMessage ?? issue.Message)));
        var inputs = new[] { first, second, geometry, scene };
        _ = result.CookedAssets.Select(static asset => asset.CookedAssetUri).Should().BeEquivalentTo(inputs.Select(static path => new Uri("asset:///" + path[..^5])));
        foreach (var path in inputs)
        {
            _ = File.Exists(Path.Combine(workspace.Root, ".cooked", path[..^5])).Should().BeTrue(path);
        }

        var inspected = await api.InspectLooseCookedRootAsync(Path.Combine(workspace.Root, ".cooked/Content"), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = inspected.Succeeded.Should().BeTrue();
        _ = inspected.Assets.Where(static asset => asset.Kind == ContentCookAssetKind.Material).Select(static asset => asset.AssetKey)
            .Should().HaveCount(2).And.OnlyHaveUniqueItems().And.NotContainNulls();

        _ = (await service.CookCurrentSceneAsync(new("asset:///" + scene), this.TestContext.CancellationToken).ConfigureAwait(false)).IsUpToDate.Should().BeTrue();
        _ = (await service.ReadAsync(workspace.ProjectContext, inputs.Select(static path => new Uri("asset:///" + path)).ToArray(), this.TestContext.CancellationToken).ConfigureAwait(false))
            .Should().OnlyContain(static status => status.Freshness == AssetCookFreshness.Current);
    }
}
