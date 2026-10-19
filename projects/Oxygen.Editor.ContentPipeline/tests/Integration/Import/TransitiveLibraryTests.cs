// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Nodes;
using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Editor.ContentPipeline.TestSupport;
using Oxygen.Managed.Core;
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
    /// <returns>The asynchronous native priority regression.</returns>
    [TestMethod]
    [DataRow(true, true)]
    [DataRow(false, true)]
    [DataRow(true, false)]
    [TestCategory("NativeContent")]
    public async Task CookedGeometryDiscoversProjectMaterialOverride(bool projectWins, bool materialLibraryMounted)
    {
        using var library = new CookWorkspace([new("Content", "Content"), new("Art", "Art")], EngineContentPak.Installed());
        using var consumer = new CookWorkspace([new("Content", "Content"), new("Art", "Art")], EngineContentPak.Installed());
        const string material = "Art/Materials/Shared.omat.json";
        library.WriteMaterial(material, "Shared");
        consumer.WriteMaterial(material, "Project shared");
        WriteAuthoredGeometry(library, withBuffer: false);
        var geometry = AssetPath.Parse("/Content/Geometry/AuthoredCube.ogeo");
        var descriptor = JsonNode.Parse(library.ReadText("Content/Geometry/AuthoredCube.ogeo.json"))!;
        descriptor["lods"]![0]!["submeshes"]![0]!["material_ref"] = "/" + material[..^5];
        library.WriteText("Content/Geometry/AuthoredCube.ogeo.json", descriptor.ToJsonString());
        using var compatibility = EditorNativeCompatibilityService.ForCooking();
        var runner = new CountingSourceRunner();
        var api = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), runner, NullLogger<ImportToolContentPipelineApi>.Instance, compatibility);
        var producer = CreateService(library, new SceneDescriptorGenerator(), api, compatibility);
        _ = (await producer.CookAssetAsync(geometry, this.TestContext.CancellationToken).ConfigureAwait(false)).IsPublished.Should().BeTrue();
        var context = consumer.ProjectContext with
        {
            LocalFolderMounts = materialLibraryMounted
                ? [new("Materials", FileSystemPath.From(await library.ExportRootAsync("Art", this.TestContext.CancellationToken).ConfigureAwait(false))), new("Meshes", FileSystemPath.From(library.CookedRoot("Content")))]
                : [new("Meshes", FileSystemPath.From(library.CookedRoot("Content")))],
            CookedContentOrder = projectWins ? [] : [new(Oxygen.Editor.Projects.CookedContentSourceKind.ProjectOutput), new(Oxygen.Editor.Projects.CookedContentSourceKind.LocalFolder, "Materials"), new(Oxygen.Editor.Projects.CookedContentSourceKind.LocalFolder, "Meshes")],
        };
        consumer.Activate(context);
        AddGeometryNode(consumer, AssetPath.Parse("/Content/Geometry/AuthoredCube.ogeo"), "Library mesh");
        await consumer.WriteSceneAsync("Content/Scenes/Main.oscene.json").ConfigureAwait(false);
        var service = CreateService(consumer, new SceneDescriptorGenerator(), api, compatibility);
        var scene = AssetPath.Parse("/Content/Scenes/Main.oscene");
        var cooked = await service.CookCurrentSceneAsync(scene, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = cooked.IsPublished.Should().BeTrue(string.Join(Environment.NewLine, cooked.Diagnostics.Select(static issue => issue.TechnicalMessage ?? issue.Message)));
        var materialUri = AssetPath.Parse("/" + material[..^5]);
        if (projectWins)
        {
            _ = cooked.CookedAssets.Should().Contain(asset => asset.SourceAssetUri == AssetPath.Parse("/" + material));
            _ = cooked.InputSnapshot!.CookedDependencies.Should().NotContain(input => input.AssetUri == materialUri);
        }
        else
        {
            _ = cooked.InputSnapshot!.CookedDependencies.Should().Contain(input => input.AssetUri == materialUri && input.SourceName == "Materials");
            _ = cooked.CookedAssets.Should().NotContain(asset => asset.SourceAssetUri == AssetPath.Parse("/" + material));
        }

        _ = (await service.ReadAsync(context, [scene], this.TestContext.CancellationToken).ConfigureAwait(false)).Single().Freshness.Should().Be(AssetCookFreshness.Current);
        WriteMaterialRoughness(library, material, 0.93);
        _ = (await producer.CookAssetAsync(AssetPath.Parse("/" + material[..^5]), this.TestContext.CancellationToken).ConfigureAwait(false)).IsPublished.Should().BeTrue();
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
        using var library = new CookWorkspace([new("Content", "Content"), new("Art", "Art")], EngineContentPak.Installed());
        using var consumer = new CookWorkspace(engineContent: EngineContentPak.Installed());
        const string material = "Art/Materials/Shared.omat.json";
        library.WriteMaterial(material, "Shared");
        WriteAuthoredGeometry(library, withBuffer: false);
        var geometry = AssetPath.Parse("/Content/Geometry/AuthoredCube.ogeo");
        var descriptor = JsonNode.Parse(library.ReadText("Content/Geometry/AuthoredCube.ogeo.json"))!;
        descriptor["lods"]![0]!["submeshes"]![0]!["material_ref"] = "/" + material[..^5];
        library.WriteText("Content/Geometry/AuthoredCube.ogeo.json", descriptor.ToJsonString());
        using var compatibility = EditorNativeCompatibilityService.ForCooking();
        var runner = new CountingSourceRunner();
        var api = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), runner, NullLogger<ImportToolContentPipelineApi>.Instance, compatibility);
        var producer = CreateService(library, new SceneDescriptorGenerator(), api, compatibility);
        var exported = await producer.CookAssetAsync(geometry, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = exported.IsPublished.Should().BeTrue();
        var context = consumer.ProjectContext with
        {
            LocalFolderMounts = [new("Materials", FileSystemPath.From(await library.ExportRootAsync("Art", this.TestContext.CancellationToken).ConfigureAwait(false))), new("Meshes", FileSystemPath.From(library.CookedRoot("Content")))],
        };
        consumer.Activate(context);
        AddGeometryNode(consumer, AssetPath.Parse("/Content/Geometry/AuthoredCube.ogeo"), "Library mesh");
        await consumer.WriteSceneAsync("Content/Scenes/Main.oscene.json").ConfigureAwait(false);
        var service = CreateService(consumer, new SceneDescriptorGenerator(), api, compatibility);
        var scene = AssetPath.Parse("/Content/Scenes/Main.oscene");
        var cooked = await service.CookCurrentSceneAsync(scene, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = cooked.IsPublished.Should().BeTrue(string.Join(Environment.NewLine, cooked.Diagnostics.Select(static issue => issue.TechnicalMessage ?? issue.Message)));

        var source = JsonNode.Parse(library.ReadText(material))!;
        source["parameters"]!["roughness"] = 0.9;
        library.WriteText(material, source.ToJsonString());
        _ = (await producer.CookAssetAsync(AssetPath.Parse("/" + material[..^5]), this.TestContext.CancellationToken).ConfigureAwait(false)).IsPublished.Should().BeTrue();
        _ = await library.ExportRootAsync("Art", this.TestContext.CancellationToken).ConfigureAwait(false);
        var workers = runner.Count;
        var status = await service.ReadAsync(context, [scene], this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = runner.Count.Should().Be(workers, "ordinary status reads must not start native inspection");
        _ = status.Single().Freshness.Should().NotBe(AssetCookFreshness.Current);
        _ = cooked.InputSnapshot!.CookedDependencies.Select(static input => input.SourceName).Should().Contain("Materials");
        _ = (await service.CookCurrentSceneAsync(scene, this.TestContext.CancellationToken).ConfigureAwait(false)).IsPublished.Should().BeTrue();
        _ = (await service.ReadAsync(context, [scene], this.TestContext.CancellationToken).ConfigureAwait(false)).Single().Freshness.Should().Be(AssetCookFreshness.Current);
    }
}
