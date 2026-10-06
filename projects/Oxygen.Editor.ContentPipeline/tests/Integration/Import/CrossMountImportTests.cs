// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Nodes;
using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentPipeline.TestSupport;
using Oxygen.Managed.Core.Compatibility;
using static Oxygen.Editor.ContentPipeline.TestSupport.CookScenario;
using static Oxygen.Editor.ContentPipeline.TestSupport.DependencyScenario;

namespace Oxygen.Editor.ContentPipeline.Integration.Tests.Import;

[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Naming", "CA1707:Identifiers should not contain underscores", Justification = "Scenario-based MSTest method names separate the operation and expected behavior.")]
public sealed class CrossMountImportTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>A scalar material in another mount is cooked before its authored geometry and scene consumers.</summary>
    /// <param name="interleaved">Whether the material and scene share a mount separated by the geometry's mount.</param>
    /// <returns>The asynchronous transitive cross-mount regression.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    [DataRow(false)]
    [DataRow(true)]
    public async Task SceneCookOrdersCrossMountMaterialGeometryAndScene(bool interleaved)
    {
        using var workspace = new CookWorkspace([new("Content", "Content"), new("Art", "Art")]);
        WriteAuthoredGeometry(workspace, withBuffer: false);
        var geometryPath = Path.Combine(workspace.Root, "Content/Geometry/AuthoredCube.ogeo.json");
        var descriptor = JsonNode.Parse(await File.ReadAllTextAsync(geometryPath, this.TestContext.CancellationToken).ConfigureAwait(false))!;
        var materialPath = (interleaved ? "Content" : "Art") + "/Materials/Red.omat.json";
        var geometrySource = (interleaved ? "Art" : "Content") + "/Geometry/AuthoredCube.ogeo.json";
        if (interleaved)
        {
            File.Delete(geometryPath);
            geometryPath = Path.Combine(workspace.Root, geometrySource);
            _ = Directory.CreateDirectory(Path.GetDirectoryName(geometryPath)!);
        }

        descriptor["lods"]![0]!["submeshes"]![0]!["material_ref"] = "/" + materialPath[..^5];
        await File.WriteAllTextAsync(geometryPath, descriptor.ToJsonString(), this.TestContext.CancellationToken).ConfigureAwait(false);
        workspace.WriteMaterial(materialPath, "Red");
        AddGeometryNode(workspace, new("asset:///" + geometrySource), "Mesh");
        await workspace.WriteSceneAsync("Content/Scenes/Main.oscene.json").ConfigureAwait(false);
        using var compatibility = EditorNativeCompatibilityService.ForCooking();
        var api = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), new ContentPipelineProcessRunner(), NullLogger<ImportToolContentPipelineApi>.Instance, compatibility);
        var service = CreateService(workspace, new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(api)), api, compatibility);
        var result = await service.CookCurrentSceneAsync(new("asset:///Content/Scenes/Main.oscene.json"), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = result.IsPublished.Should().BeTrue(string.Join(Environment.NewLine, result.Diagnostics.Select(static issue => issue.TechnicalMessage ?? issue.Message)));
        _ = result.CookedAssets.Should().HaveCount(3);
        _ = result.CookedAssets.Should().OnlyHaveUniqueItems(static asset => asset.CookedAssetUri);
        var materialUri = new Uri("asset:///" + materialPath[..^5]);
        _ = result.CookedAssets.Should().Contain(asset => asset.CookedAssetUri == materialUri);
        _ = (await service.CookCurrentSceneAsync(new("asset:///Content/Scenes/Main.oscene.json"), this.TestContext.CancellationToken).ConfigureAwait(false)).IsUpToDate.Should().BeTrue();
    }

    /// <summary>A scene can consume imported geometry in another mount in both new and existing publications.</summary>
    /// <param name="existingOutput">Whether the source's output already exists in this project.</param>
    /// <returns>The asynchronous cross-mount scene cook regression.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    [DataRow(false)]
    [DataRow(true)]
    public async Task SceneCookResolvesImportedGeometryAcrossMounts(bool existingOutput)
    {
        using var original = new CookWorkspace([new("Content", "Content"), new("Art", "Art")]);
        using var clean = new CookWorkspace([new("Content", "Content"), new("Art", "Art")]);
        var source = await WriteCrossMountModelAsync(original, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = await WriteCrossMountModelAsync(clean, this.TestContext.CancellationToken).ConfigureAwait(false);
        using var compatibility = EditorNativeCompatibilityService.ForCooking();
        var api = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), new ContentPipelineProcessRunner(), NullLogger<ImportToolContentPipelineApi>.Instance, compatibility);
        var sourceService = CreateService(original, new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(api)), api, compatibility);
        var imported = await sourceService.CookAssetAsync(source, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = imported.IsPublished.Should().BeTrue();
        var workspace = existingOutput ? original : clean;
        var geometry = imported.CookedAssets.First(static asset => asset.Kind == ContentCookAssetKind.Geometry).CookedAssetUri;
        AddGeometryNode(workspace, geometry, "Imported mesh");
        await workspace.WriteSceneAsync("Content/Scenes/Main.oscene.json").ConfigureAwait(false);
        var service = CreateService(workspace, new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(api)), api, compatibility);
        var result = await service.CookCurrentSceneAsync(new("asset:///Content/Scenes/Main.oscene.json"), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = result.IsPublished.Should().BeTrue(string.Join(Environment.NewLine, result.Diagnostics.Select(static issue => issue.TechnicalMessage ?? issue.Message)));
        _ = result.CookedAssets.Concat(result.ReusedAssets).Should().Contain(asset => asset.CookedAssetUri == geometry);
        _ = result.InputsAreCurrent.Should().BeTrue();
    }
}
