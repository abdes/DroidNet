// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Editor.World;

namespace Oxygen.Editor.ContentPipeline.Tests;

/// <summary>Exercises published library inputs without copying them into project authoring.</summary>
public sealed partial class ContentPipelineServiceTests
{
    /// <summary>A scene resolves and reuses a native geometry supplied only by a declared library.</summary>
    /// <returns>The asynchronous cooked-library regression.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task SceneCookUsesDeclaredCookedLibraryGeometry()
    {
        using var library = new TempWorkspace([new("Content", "Content"), new("Art", "Art")]);
        using var consumer = new TempWorkspace();
        var source = await WriteCrossMountModelAsync(library, this.TestContext.CancellationToken).ConfigureAwait(false);
        using var compatibility = Oxygen.Testing.TemporaryNativeArtifacts.ForInstalledEngine();
        var api = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), new ContentPipelineProcessRunner(), NullLogger<ImportToolContentPipelineApi>.Instance, compatibility);
        var imported = await CreateService(library, new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(api)), api, compatibility).CookAssetAsync(source, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = imported.IsPublished.Should().BeTrue();
        var geometry = imported.CookedAssets.First(static output => output.Kind == ContentCookAssetKind.Geometry).CookedAssetUri;
        var root = Path.Combine(library.Root, ".cooked/Art");
        var context = consumer.ProjectContext with { LocalFolderMounts = [new("Art library", root)] };
        consumer.ContextService.Activate(context);
        AddGeometryNode(consumer, geometry, "Library mesh");
        await consumer.WriteSceneAsync("Content/Scenes/Main.oscene.json").ConfigureAwait(false);
        var service = CreateService(consumer, new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(api)), api, compatibility);
        var scene = new Uri("asset:///Content/Scenes/Main.oscene.json");
        var result = await service.CookCurrentSceneAsync(scene, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = result.IsPublished.Should().BeTrue(string.Join(Environment.NewLine, result.Diagnostics.Select(static issue => issue.TechnicalMessage ?? issue.Message)));
        _ = result.CookedAssets.Should().ContainSingle().Which.SourceAssetUri.Should().Be(scene);
        _ = (await service.CookCurrentSceneAsync(scene, this.TestContext.CancellationToken).ConfigureAwait(false)).IsUpToDate.Should().BeTrue();
        _ = (await service.ReadAsync(context, [scene], this.TestContext.CancellationToken).ConfigureAwait(false)).Single().Freshness.Should().Be(AssetCookFreshness.Current);

        var modelPath = Path.Combine(library.Root, "Content/SourceMedia/DCC/Model/model.gltf");
        var model = System.Text.Json.Nodes.JsonNode.Parse(await File.ReadAllTextAsync(modelPath, this.TestContext.CancellationToken).ConfigureAwait(false))!;
        model["materials"]![0]!["pbrMetallicRoughness"]!["roughnessFactor"] = 0.1;
        await File.WriteAllTextAsync(modelPath, model.ToJsonString(), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = (await CreateService(library, new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(api)), api, compatibility).CookAssetAsync(source, this.TestContext.CancellationToken).ConfigureAwait(false)).IsPublished.Should().BeTrue();
        _ = (await service.ReadAsync(context, [scene], this.TestContext.CancellationToken).ConfigureAwait(false)).Single().Freshness.Should().Be(AssetCookFreshness.OutOfDate);
        var updated = await service.CookCurrentSceneAsync(scene, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = updated.IsPublished.Should().BeTrue();
        _ = updated.InputSnapshot!.CookedDependencies.Select(static dependency => dependency.ContentFingerprint).Distinct(StringComparer.Ordinal).Should().ContainSingle().Which
            .Should().NotBe(result.InputSnapshot!.CookedDependencies.Select(static dependency => dependency.ContentFingerprint).Distinct(StringComparer.Ordinal).Single());
        _ = (await service.ReadAsync(context, [scene], this.TestContext.CancellationToken).ConfigureAwait(false)).Single().Freshness.Should().Be(AssetCookFreshness.Current);

        await this.AssertLibraryFailuresPreserveOutputAsync(consumer, context, library.Root, root, service, scene).ConfigureAwait(false);
    }

    /// <summary>The saved library order selects the captured native dependency and invalidates its consumer.</summary>
    /// <returns>The asynchronous library-priority regression.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task SceneCookFollowsSavedLibraryPriority()
    {
        using var older = new TempWorkspace([new("Content", "Content"), new("Art", "Art")]);
        using var newer = new TempWorkspace([new("Content", "Content"), new("Art", "Art")]);
        using var consumer = new TempWorkspace();
        using var compatibility = Oxygen.Testing.TemporaryNativeArtifacts.ForInstalledEngine();
        var runner = new CountingSourceRunner();
        var api = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), runner, NullLogger<ImportToolContentPipelineApi>.Instance, compatibility);
        Uri? geometry = null;
        foreach (var library in new[] { older, newer })
        {
            var source = await WriteCrossMountModelAsync(library, this.TestContext.CancellationToken).ConfigureAwait(false);
            var result = await CreateService(library, new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(api)), api, compatibility).CookAssetAsync(source, this.TestContext.CancellationToken).ConfigureAwait(false);
            _ = result.IsPublished.Should().BeTrue();
            geometry = result.CookedAssets.First(static output => output.Kind == ContentCookAssetKind.Geometry).CookedAssetUri;
        }

        var context = consumer.ProjectContext with { LocalFolderMounts = [new("Older", Path.Combine(older.Root, ".cooked/Art")), new("Newer", Path.Combine(newer.Root, ".cooked/Art"))] };
        consumer.ContextService.Activate(context);
        AddGeometryNode(consumer, geometry!, "Library mesh");
        await consumer.WriteSceneAsync("Content/Scenes/Main.oscene.json").ConfigureAwait(false);
        var scene = new Uri("asset:///Content/Scenes/Main.oscene.json");
        var service = CreateService(consumer, new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(api)), api, compatibility);
        var first = await service.CookCurrentSceneAsync(scene, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = first.IsPublished.Should().BeTrue();
        _ = first.InputSnapshot!.CookedDependencies.Select(static dependency => dependency.SourceName).Distinct(StringComparer.Ordinal).Should().ContainSingle().Which.Should().Be("Newer");
        var changed = context with { CookedContentOrder = [new(CookedContentSourceKind.LocalFolder, "Newer"), new(CookedContentSourceKind.LocalFolder, "Older"), new(CookedContentSourceKind.ProjectOutput)] };
        consumer.ContextService.Activate(changed);
        var workers = runner.Count;
        _ = (await service.ReadAsync(changed, [scene], this.TestContext.CancellationToken).ConfigureAwait(false)).Single().Freshness.Should().Be(AssetCookFreshness.OutOfDate);
        _ = runner.Count.Should().Be(workers, "status checks must not start a native process");
        var second = await service.CookCurrentSceneAsync(scene, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = second.IsPublished.Should().BeTrue();
        _ = second.InputSnapshot!.CookedDependencies.Select(static dependency => dependency.SourceName).Distinct(StringComparer.Ordinal).Should().ContainSingle().Which.Should().Be("Older");
    }

    private async Task AssertLibraryFailuresPreserveOutputAsync(TempWorkspace consumer, Oxygen.Editor.Projects.ProjectContext context, string libraryProject, string root, ContentPipelineService service, Uri scene)
    {
        var index = Path.Combine(consumer.Root, ".cooked/Content/container.index.bin");
        var before = await File.ReadAllBytesAsync(index, this.TestContext.CancellationToken).ConfigureAwait(false);
        var unavailable = context with { LocalFolderMounts = [new("Art library", Path.Combine(libraryProject, "Offline"))] };
        consumer.ContextService.Activate(unavailable);
        _ = (await service.ReadAsync(unavailable, [scene], this.TestContext.CancellationToken).ConfigureAwait(false)).Single().Freshness.Should().Be(AssetCookFreshness.InvalidSource);
        var missing = await service.CookCurrentSceneAsync(scene, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = missing.IsPublished.Should().BeFalse();
        _ = missing.Diagnostics.Should().Contain(issue => issue.Code == "asset_cook.library_reference_missing" && issue.AffectedVirtualPath == scene.AbsolutePath);
        _ = (await File.ReadAllBytesAsync(index, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Equal(before);

        consumer.ContextService.Activate(context);
        await File.WriteAllTextAsync(Path.Combine(root, "container.index.bin"), "broken", this.TestContext.CancellationToken).ConfigureAwait(false);
        var corruptStatus = (await service.ReadAsync(context, [scene], this.TestContext.CancellationToken).ConfigureAwait(false)).Single();
        _ = corruptStatus.HasVerifiedOutput.Should().BeFalse();
        _ = corruptStatus.Freshness.Should().Be(AssetCookFreshness.InvalidSource);
        _ = corruptStatus.Diagnostics.Should().Contain(issue => issue.Code == "asset_cook.library_unavailable");
        var corrupt = await service.CookCurrentSceneAsync(scene, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = corrupt.IsPublished.Should().BeFalse();
        _ = corrupt.Diagnostics.Should().Contain(issue => issue.Code == "asset_cook.library_unavailable");
        _ = (await File.ReadAllBytesAsync(index, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Equal(before);
    }
}
