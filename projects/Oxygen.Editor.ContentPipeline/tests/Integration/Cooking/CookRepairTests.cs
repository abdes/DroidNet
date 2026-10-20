// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Storage.Native;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.ContentPipeline.TestSupport;
using Oxygen.Editor.Input.Testing;
using Oxygen.Editor.Scripting.Testing;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Assets.Catalog.FileSystem;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Compatibility;
using Oxygen.Managed.Core.Diagnostics;
using Testably.Abstractions;
using static Oxygen.Editor.ContentPipeline.TestSupport.IncrementalCookScenario;
using static Oxygen.Editor.ContentPipeline.TestSupport.RetainedModelScenario;

namespace Oxygen.Editor.ContentPipeline.Integration.Tests.Cooking;

[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Naming", "CA1707:Identifiers should not contain underscores", Justification = "Scenario-based MSTest method names separate the operation and expected behavior.")]
public sealed class CookRepairTests
{
    public TestContext TestContext { get; set; } = null!;

    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task RenamingCookedSceneRetiresOldOutputAndPreservesOtherAssets()
    {
        using var workspace = new CookWorkspace(engineContent: EngineContentPak.Installed());
        workspace.WriteMaterial("Content/Materials/Blue.omat.json", "Blue");
        using var compatibility = EditorNativeCompatibilityService.ForCooking();
        var api = CreateRecordingApi(compatibility);
        await PrepareIncrementalSceneAsync(workspace, api).ConfigureAwait(false);
        var pipeline = CreateIncrementalService(workspace, api, compatibility);
        AssertCookSucceeded(await pipeline.CookProjectAsync(this.TestContext.CancellationToken).ConfigureAwait(false));
        var previousName = "Main";
        foreach (var name in new[] { "Small Scene", "Main", "Small Scene" })
        {
            workspace.Scene.Name = name;
            await workspace.WriteSceneAsync($"Content/Scenes/{name}.oscene.json").ConfigureAwait(false);
            File.Delete(Path.Combine(workspace.Root, "Content", "Scenes", $"{previousName}.oscene.json"));
            var result = await pipeline.CookAssetAsync(AssetPath.Parse($"/Content/Scenes/{name}.oscene.json"), this.TestContext.CancellationToken).ConfigureAwait(false);
            AssertCookSucceeded(result);
            _ = result.IsPublished.Should().BeTrue();
            var root = workspace.CookedRoot("Content");
            var inventory = await api.ReadInventoryAsync(root, artifacts: null, this.TestContext.CancellationToken).ConfigureAwait(false);
            _ = inventory.IsValid.Should().BeTrue();
            _ = inventory.Assets.Select(static asset => asset.VirtualPath).Should()
                .Contain($"/Content/Scenes/{name}.oscene")
                .And.Contain("/Content/Materials/Blue.omat")
                .And.NotContain($"/Content/Scenes/{previousName}.oscene");
            _ = Directory.EnumerateFiles(root, $"{previousName}.oscene", SearchOption.AllDirectories).Should().BeEmpty();
            previousName = name;
        }

        _ = (await pipeline.CookProjectAsync(this.TestContext.CancellationToken).ConfigureAwait(false)).IsUpToDate.Should().BeTrue();
    }

    /// <summary>A renamed material leaves the cooked index under its old identity, and the other materials are reused, not cooked again.</summary>
    /// <returns>The asynchronous native retirement regression.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task RenamingCookedMaterialRetiresOldOutputAndPreservesOtherAssets()
    {
        using var workspace = new CookWorkspace(engineContent: EngineContentPak.Installed());
        workspace.WriteMaterial("Content/Materials/Blue.omat.json", "Blue");
        workspace.WriteMaterial("Content/Materials/Red.omat.json", "Red");
        using var compatibility = EditorNativeCompatibilityService.ForCooking();
        var api = CreateRecordingApi(compatibility);
        var pipeline = CreateIncrementalService(workspace, api, compatibility);
        AssertCookSucceeded(await pipeline.CookProjectAsync(this.TestContext.CancellationToken).ConfigureAwait(false));
        File.Move(Path.Combine(workspace.Root, "Content/Materials/Blue.omat.json"), Path.Combine(workspace.Root, "Content/Materials/Teal.omat.json"));

        var result = await pipeline.CookAssetAsync(AssetPath.Parse("/Content/Materials/Teal.omat.json"), this.TestContext.CancellationToken).ConfigureAwait(false);

        AssertCookSucceeded(result);
        _ = result.IsPublished.Should().BeTrue();
        _ = result.CookedAssets.Select(static asset => asset.VirtualPath).Should().Equal("/Content/Materials/Teal.omat");
        _ = api.Removed.Should().ContainSingle().Which.VirtualPaths.Should().Equal("/Content/Materials/Blue.omat");
        var inventory = await api.ReadInventoryAsync(workspace.CookedRoot("Content"), artifacts: null, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = inventory.IsValid.Should().BeTrue();
        _ = inventory.Assets.Select(static asset => asset.VirtualPath).Should()
            .Contain("/Content/Materials/Teal.omat").And.Contain("/Content/Materials/Red.omat").And.NotContain("/Content/Materials/Blue.omat");
        _ = (await pipeline.CookProjectAsync(this.TestContext.CancellationToken).ConfigureAwait(false)).IsUpToDate.Should().BeTrue();
    }

    /// <summary>A material in a folder named as the editor names copies, with a space and parentheses, cooks.</summary>
    /// <returns>The asynchronous native regression.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task MaterialInFolderWithSpaceAndParenthesesCooks()
    {
        using var workspace = new CookWorkspace(engineContent: EngineContentPak.Installed());
        workspace.WriteMaterial("Content/New folder (2)/Wet Rubber.omat.json", "Wet Rubber");
        using var compatibility = EditorNativeCompatibilityService.ForCooking();
        var api = CreateRecordingApi(compatibility);
        var pipeline = CreateIncrementalService(workspace, api, compatibility);

        var result = await pipeline.CookAssetAsync(AssetPath.Parse("/Content/New folder (2)/Wet Rubber.omat.json"), this.TestContext.CancellationToken).ConfigureAwait(false);

        AssertCookSucceeded(result);
        _ = (await api.ReadInventoryAsync(workspace.CookedRoot("Content"), artifacts: null, this.TestContext.CancellationToken).ConfigureAwait(false))
            .Assets.Select(static asset => asset.VirtualPath).Should().Contain("/Content/New folder (2)/Wet Rubber.omat");
    }

    /// <summary>An automatic cook of a deleted source removes its cooked output and keeps every other asset without cooking it.</summary>
    /// <returns>The asynchronous native regression.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task AutomaticCookOfDeletedSourceRemovesItsOutput()
    {
        using var workspace = new CookWorkspace(engineContent: EngineContentPak.Installed());
        workspace.WriteMaterial("Content/Materials/Blue.omat.json", "Blue");
        workspace.WriteMaterial("Content/Materials/Red.omat.json", "Red");

        using var compatibility = EditorNativeCompatibilityService.ForCooking();
        var api = CreateRecordingApi(compatibility);
        var pipeline = CreateIncrementalService(workspace, api, compatibility);
        AssertCookSucceeded(await pipeline.CookProjectAsync(this.TestContext.CancellationToken).ConfigureAwait(false));
        var red = ReadDescriptorBytes(workspace, "Red.omat");
        File.Delete(Path.Combine(workspace.Root, "Content/Materials/Blue.omat.json"));
        api.Imported.Clear();

        var result = await pipeline.CookSavedAssetAsync(AssetPath.Parse("/Content/Materials/Blue.omat.json"), workspace.ProjectContext, this.TestContext.CancellationToken).ConfigureAwait(false);

        AssertCookSucceeded(result);
        _ = result.IsPublished.Should().BeTrue();
        _ = api.Imported.Should().BeEmpty();
        _ = api.Removed.Should().ContainSingle().Which.VirtualPaths.Should().Equal("/Content/Materials/Blue.omat");
        var paths = (await api.ReadInventoryAsync(workspace.CookedRoot("Content"), artifacts: null, this.TestContext.CancellationToken).ConfigureAwait(false))
            .Assets.Select(static asset => asset.VirtualPath).ToArray();
        _ = paths.Should().NotContain("/Content/Materials/Blue.omat").And.Contain("/Content/Materials/Red.omat");
        _ = ReadDescriptorBytes(workspace, "Red.omat").Should().Equal(red);
        _ = Directory.EnumerateFiles(workspace.CookedRoot("Content"), "Blue.omat", SearchOption.AllDirectories).Should().BeEmpty();

        _ = (await pipeline.CookProjectAsync(this.TestContext.CancellationToken).ConfigureAwait(false)).IsUpToDate.Should().BeTrue();
    }

    /// <summary>
    /// Deleting a material cooks the scene that uses it again: the scene reports the missing material, and nothing
    /// is published until it is fixed.
    /// </summary>
    /// <returns>The asynchronous native regression.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task DeletingAMaterialCooksTheSceneThatUsesIt()
    {
        using var workspace = new CookWorkspace(engineContent: EngineContentPak.Installed());
        using var compatibility = EditorNativeCompatibilityService.ForCooking();
        var api = CreateRecordingApi(compatibility);
        await PrepareIncrementalSceneAsync(workspace, api).ConfigureAwait(false);
        var pipeline = CreateIncrementalService(workspace, api, compatibility);
        AssertCookSucceeded(await pipeline.CookProjectAsync(this.TestContext.CancellationToken).ConfigureAwait(false));
        File.Delete(Path.Combine(workspace.Root, "Content/Materials/Blue.omat.json"));

        var result = await pipeline.CookSavedAssetAsync(AssetPath.Parse("/Content/Materials/Blue.omat.json"), workspace.ProjectContext, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = result.IsPublished.Should().BeFalse();
        _ = result.Status.Should().NotBe(OperationStatus.Succeeded);
        _ = result.Diagnostics.Should().Contain(static diagnostic => diagnostic.Code == "scene.descriptor.reference_missing"
            && diagnostic.Message.Contains("/Content/Materials/Blue.omat", StringComparison.Ordinal));
        _ = api.Imported.Should().Contain(static execution => execution.Manifest.Jobs.Any(static job => job.Type == "scene-descriptor"));
    }

    /// <summary>
    /// Deleting a folder whose material was cooked removes its cooked output through cook on change: the file
    /// catalog reports the material removed and the automatic cook publishes the content again without it.
    /// </summary>
    /// <returns>The asynchronous native regression.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task DeletedFolderLosesItsCookedMaterialThroughCookOnChange()
    {
        using var workspace = new CookWorkspace(engineContent: EngineContentPak.Installed());
        workspace.WriteMaterial("Content/Materials/Red.omat.json", "Red");
        workspace.WriteMaterial("Content/New folder/Blue.omat.json", "Blue");
        using var compatibility = EditorNativeCompatibilityService.ForCooking();
        var api = CreateRecordingApi(compatibility);
        var pipeline = CreateIncrementalService(workspace, api, compatibility);
        AssertCookSucceeded(await pipeline.CookProjectAsync(this.TestContext.CancellationToken).ConfigureAwait(false));
        using var catalog = new FileSystemAssetCatalog(
            new NativeStorageProvider(new RealFileSystem()),
            new() { RootFolderPath = FileSystemPath.From(Path.Combine(workspace.Root, "Content")), MountPoint = "Content" });
        _ = await catalog.QueryAsync(new(AssetQueryScope.All), this.TestContext.CancellationToken).ConfigureAwait(false);
        using var automatic = new AutomaticCookService(
            workspace.ContextService, workspace.Manager, catalog, pipeline, pipeline, workspace.CookCoordinator, new FakeScripts(), new FakeInputSources(), NullLogger<AutomaticCookService>.Instance);

        Directory.Delete(Path.Combine(workspace.Root, "Content", "New folder"), recursive: true);

        string[] paths = [];
        for (var attempt = 0; attempt < 120 && (paths.Length == 0 || paths.Contains("/Content/New folder/Blue.omat")); attempt++)
        {
            await Task.Delay(250, this.TestContext.CancellationToken).ConfigureAwait(false);
            try
            {
                paths = [.. (await api.ReadInventoryAsync(workspace.CookedRoot("Content"), artifacts: null, this.TestContext.CancellationToken).ConfigureAwait(false))
                    .Assets.Select(static asset => asset.VirtualPath)];
            }
            catch (InvalidDataException)
            {
                // A publication replaced the generation between finding it and reading it; the next read finds the new one.
            }
        }

        _ = paths.Should().NotContain("/Content/New folder/Blue.omat").And.Contain("/Content/Materials/Red.omat");
    }

    /// <summary>
    /// Moving a cooked material into a new folder cooks it there through cook on change, and its output at the old
    /// path goes.
    /// </summary>
    /// <returns>The asynchronous native regression.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task MovedMaterialCooksAtItsNewPathThroughCookOnChange()
    {
        using var workspace = new CookWorkspace(engineContent: EngineContentPak.Installed());
        workspace.WriteMaterial("Content/Materials/Red.omat.json", "Red");
        workspace.WriteMaterial("Content/Materials/Blue.omat.json", "Blue");
        using var compatibility = EditorNativeCompatibilityService.ForCooking();
        var api = CreateRecordingApi(compatibility);
        var pipeline = CreateIncrementalService(workspace, api, compatibility);
        AssertCookSucceeded(await pipeline.CookProjectAsync(this.TestContext.CancellationToken).ConfigureAwait(false));
        using var catalog = new FileSystemAssetCatalog(
            new NativeStorageProvider(new RealFileSystem()),
            new() { RootFolderPath = FileSystemPath.From(Path.Combine(workspace.Root, "Content")), MountPoint = "Content" });
        _ = await catalog.QueryAsync(new(AssetQueryScope.All), this.TestContext.CancellationToken).ConfigureAwait(false);
        using var automatic = new AutomaticCookService(
            workspace.ContextService, workspace.Manager, catalog, pipeline, pipeline, workspace.CookCoordinator, new FakeScripts(), new FakeInputSources(), NullLogger<AutomaticCookService>.Instance);
        _ = Directory.CreateDirectory(Path.Combine(workspace.Root, "Content", "New folder"));
        await Task.Delay(500, this.TestContext.CancellationToken).ConfigureAwait(false);

        File.Move(Path.Combine(workspace.Root, "Content", "Materials", "Blue.omat.json"), Path.Combine(workspace.Root, "Content", "New folder", "Blue.omat.json"));

        string[] paths = [];
        for (var attempt = 0; attempt < 120 && !paths.Contains("/Content/New folder/Blue.omat"); attempt++)
        {
            await Task.Delay(250, this.TestContext.CancellationToken).ConfigureAwait(false);
            try
            {
                paths = [.. (await api.ReadInventoryAsync(workspace.CookedRoot("Content"), artifacts: null, this.TestContext.CancellationToken).ConfigureAwait(false))
                    .Assets.Select(static asset => asset.VirtualPath)];
            }
            catch (InvalidDataException)
            {
                // A publication replaced the generation between finding it and reading it; the next read finds the new one.
            }
        }

        _ = paths.Should().Contain("/Content/New folder/Blue.omat").And.NotContain("/Content/Materials/Blue.omat");
    }

    /// <summary>
    /// Deleting the last asset of a mount takes the mount's root out of the publication, as if it was never cooked: the
    /// preview no longer mounts it and its generation is reclaimed.
    /// </summary>
    /// <returns>The asynchronous native regression.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task DeletingTheLastAssetOfAMountDropsItsRoot()
    {
        using var workspace = new CookWorkspace(engineContent: EngineContentPak.Installed());
        workspace.WriteMaterial("Content/Materials/Blue.omat.json", "Blue");
        using var compatibility = EditorNativeCompatibilityService.ForCooking();
        var api = CreateRecordingApi(compatibility);
        var pipeline = CreateIncrementalService(workspace, api, compatibility);
        AssertCookSucceeded(await pipeline.CookProjectAsync(this.TestContext.CancellationToken).ConfigureAwait(false));
        var generation = workspace.CookedRoot("Content");
        var preview = new RecordingPublicationPreview();
        await using var previewLifetime = preview.ConfigureAwait(false);
        using var registration = workspace.Publication.RegisterPreview(workspace.ProjectContext, () => Task.FromResult<ICookPublicationPreview?>(preview));
        File.Delete(Path.Combine(workspace.Root, "Content/Materials/Blue.omat.json"));

        var result = await pipeline.CookSavedAssetAsync(AssetPath.Parse("/Content/Materials/Blue.omat.json"), workspace.ProjectContext, this.TestContext.CancellationToken).ConfigureAwait(false);

        AssertCookSucceeded(result);
        _ = result.IsPublished.Should().BeTrue();
        _ = ReadProjectRoots(workspace.Root).Should().BeEmpty();
        _ = preview.Committed.Should().ContainSingle().Which.Should().BeEmpty();
        _ = Directory.Exists(generation).Should().BeFalse("nothing references the dropped generation");
        AssertCookSucceeded(await pipeline.CookProjectAsync(this.TestContext.CancellationToken).ConfigureAwait(false));
    }

    /// <summary>One cook takes an emptied mount out of the publication and publishes the cook of another mount.</summary>
    /// <returns>The asynchronous native regression.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task CookDropsAnEmptiedMountAndPublishesAnother()
    {
        using var workspace = new CookWorkspace([new("Content", "Content"), new("Extra", "Extra")], EngineContentPak.Installed());
        workspace.WriteMaterial("Content/Materials/Blue.omat.json", "Blue");
        workspace.WriteMaterial("Extra/Materials/Red.omat.json", "Red");
        using var compatibility = EditorNativeCompatibilityService.ForCooking();
        var api = CreateRecordingApi(compatibility);
        var pipeline = CreateIncrementalService(workspace, api, compatibility);
        AssertCookSucceeded(await pipeline.CookProjectAsync(this.TestContext.CancellationToken).ConfigureAwait(false));
        _ = ReadProjectRoots(workspace.Root).Should().BeEquivalentTo("Content", "Extra");
        File.Delete(Path.Combine(workspace.Root, "Content/Materials/Blue.omat.json"));
        workspace.WriteText("Extra/Materials/Red.omat.json", workspace.ReadText("Extra/Materials/Red.omat.json").Replace("0.5", "0.8", StringComparison.Ordinal));

        var result = await pipeline.CookProjectAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        AssertCookSucceeded(result);
        _ = result.IsPublished.Should().BeTrue();
        _ = ReadProjectRoots(workspace.Root).Should().Equal("Extra");
        _ = (await api.ReadInventoryAsync(workspace.CookedRoot("Extra"), artifacts: null, this.TestContext.CancellationToken).ConfigureAwait(false))
            .Assets.Select(static asset => asset.VirtualPath).Should().Contain("/Extra/Materials/Red.omat");
        _ = (await pipeline.CookProjectAsync(this.TestContext.CancellationToken).ConfigureAwait(false)).IsUpToDate.Should().BeTrue();
    }

    /// <summary>An automatic cook of a source deleted before it was ever cooked ends without an error.</summary>
    /// <returns>The asynchronous native regression.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task AutomaticCookOfNeverCookedDeletedSourceEndsWithoutError()
    {
        using var workspace = new CookWorkspace(engineContent: EngineContentPak.Installed());
        workspace.WriteMaterial("Content/Materials/Red.omat.json", "Red");
        using var compatibility = EditorNativeCompatibilityService.ForCooking();
        var api = CreateRecordingApi(compatibility);
        var pipeline = CreateIncrementalService(workspace, api, compatibility);
        AssertCookSucceeded(await pipeline.CookProjectAsync(this.TestContext.CancellationToken).ConfigureAwait(false));
        workspace.WriteMaterial("Content/Materials/Blue.omat.json", "Blue");
        File.Delete(Path.Combine(workspace.Root, "Content/Materials/Blue.omat.json"));

        var result = await pipeline.CookSavedAssetAsync(AssetPath.Parse("/Content/Materials/Blue.omat.json"), workspace.ProjectContext, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = result.Status.Should().Be(OperationStatus.Cancelled);
        _ = result.Diagnostics.Should().NotContain(static diagnostic => diagnostic.Severity >= DiagnosticSeverity.Error);
    }

    /// <summary>An asset request reconstructs the complete damaged root or preserves the prior publication.</summary>
    /// <param name="failure">A missing source, failed native job, or successful repair.</param>
    /// <returns>The asynchronous native repair regression.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    [DataRow("none")]
    [DataRow("missing source")]
    [DataRow("native")]
    [DataRow("unowned output")]
    public async Task AssetCookRebuildsDamagedSharedRoot(string failure)
    {
        using var workspace = new CookWorkspace(engineContent: EngineContentPak.Installed());
        workspace.WriteMaterial("Content/Materials/Blue.omat.json", "Blue");
        _ = await WriteRetainedModelAsync(workspace, "Model", "gltf", this.TestContext.CancellationToken).ConfigureAwait(false);
        using var compatibility = EditorNativeCompatibilityService.ForCooking();
        var api = CreateRecordingApi(compatibility);
        var pipeline = CreateIncrementalService(workspace, api, compatibility);
        var first = await pipeline.CookProjectAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        AssertCookSucceeded(first);
        var root = workspace.CookedRoot("Content");
        if (string.Equals(failure, "unowned output", StringComparison.Ordinal))
        {
            await File.WriteAllTextAsync(Path.Combine(root, "Unowned.otex"), "unowned derived bytes", this.TestContext.CancellationToken).ConfigureAwait(false);
        }

        var payload = Directory.EnumerateFiles(root, "buffers.data", SearchOption.AllDirectories).Single();
        var bytes = await File.ReadAllBytesAsync(payload, this.TestContext.CancellationToken).ConfigureAwait(false);
        bytes[^1] ^= 0xFF;
        await File.WriteAllBytesAsync(payload, bytes, this.TestContext.CancellationToken).ConfigureAwait(false);
        var damaged = ReadOutputIdentities(workspace.Root);
        if (string.Equals(failure, "missing source", StringComparison.Ordinal))
        {
            File.Delete(Path.Combine(workspace.Root, "Content/Models/Model/model.gltf"));
        }

        api.FailNextImport = string.Equals(failure, "native", StringComparison.Ordinal);
        api.Imported.Clear();
        var result = await pipeline.CookAssetAsync(AssetPath.Parse("/Content/Materials/Blue.omat.json"), this.TestContext.CancellationToken).ConfigureAwait(false);
        if (!string.Equals(failure, "none", StringComparison.Ordinal))
        {
            _ = result.Status.Should().BeOneOf(OperationStatus.Failed, OperationStatus.PartiallySucceeded);
            _ = result.IsPublished.Should().BeFalse();
            _ = ReadOutputIdentities(workspace.Root).Should().BeEquivalentTo(damaged);
            if (string.Equals(failure, "missing source", StringComparison.Ordinal))
            {
                _ = result.Diagnostics.Should().Contain(static diagnostic => diagnostic.AffectedPath != null
                    && diagnostic.AffectedPath.EndsWith("model.gltf", StringComparison.Ordinal));
                _ = api.Imported.Should().BeEmpty();
            }
            else if (string.Equals(failure, "unowned output", StringComparison.Ordinal))
            {
                _ = result.Diagnostics.Should().Contain(static diagnostic => diagnostic.Code == "asset_cook.repair_source_unknown"
                    && diagnostic.AffectedPath != null && diagnostic.AffectedPath.EndsWith("Unowned.otex", StringComparison.Ordinal));
                _ = api.Imported.Should().BeEmpty();
            }

            return;
        }

        AssertCookSucceeded(result);
        _ = result.IsPublished.Should().BeTrue();
        _ = result.CookedAssets.Select(static asset => asset.VirtualPath).Should().BeEquivalentTo(first.CookedAssets.Select(static asset => asset.VirtualPath));
        _ = result.ReusedAssets.Should().BeEmpty();
        _ = (await api.ReadInventoryAsync(workspace.CookedRoot("Content"), artifacts: null, this.TestContext.CancellationToken).ConfigureAwait(false)).IsValid.Should().BeTrue();
        _ = (await File.ReadAllBytesAsync(Path.Combine(workspace.CookedRoot("Content"), Path.GetRelativePath(root, payload)), this.TestContext.CancellationToken).ConfigureAwait(false)).Should().NotEqual(bytes);
        _ = (await pipeline.CookProjectAsync(this.TestContext.CancellationToken).ConfigureAwait(false)).IsUpToDate.Should().BeTrue();
    }

    /// <summary>Rebuilding a root from empty cannot discard another healthy owner's product.</summary>
    /// <param name="missing">Whether the descriptor is missing or truncated.</param>
    /// <returns>The asynchronous native descriptor repair regression.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    [DataRow(true)]
    [DataRow(false)]
    public async Task DescriptorRepairPreservesUnrelatedRootOwners(bool missing)
    {
        using var workspace = new CookWorkspace(engineContent: EngineContentPak.Installed());
        workspace.WriteMaterial("Content/Materials/Blue.omat.json", "Blue");
        workspace.WriteMaterial("Content/Materials/Red.omat.json", "Red");
        using var compatibility = EditorNativeCompatibilityService.ForCooking();
        var api = CreateRecordingApi(compatibility);
        var pipeline = CreateIncrementalService(workspace, api, compatibility);
        var first = await pipeline.CookProjectAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        AssertCookSucceeded(first);
        var descriptor = first.Inspection!.Assets.Single(static asset => string.Equals(asset.VirtualPath, "/Content/Materials/Blue.omat", StringComparison.Ordinal));
        var path = Path.Combine(first.Inspection.CookedRoot, descriptor.DescriptorRelativePath!);
        if (missing)
        {
            File.Delete(path);
        }
        else
        {
            await File.WriteAllBytesAsync(path, [0], this.TestContext.CancellationToken).ConfigureAwait(false);
        }

        var result = await pipeline.CookAssetAsync(AssetPath.Parse("/Content/Materials/Blue.omat.json"), this.TestContext.CancellationToken).ConfigureAwait(false);
        AssertCookSucceeded(result);
        _ = result.CookedAssets.Select(static asset => asset.VirtualPath).Should().BeEquivalentTo(first.CookedAssets.Select(static asset => asset.VirtualPath));
        _ = (await pipeline.CookProjectAsync(this.TestContext.CancellationToken).ConfigureAwait(false)).IsUpToDate.Should().BeTrue();
    }

    // The bytes of a cooked descriptor in the current Content root.
    private static byte[] ReadDescriptorBytes(CookWorkspace workspace, string fileName)
        => File.ReadAllBytes(Directory.EnumerateFiles(workspace.CookedRoot("Content"), fileName, SearchOption.AllDirectories).Single());

    // The project mounts the current publication holds a root for.
    private static string[] ReadProjectRoots(string projectRoot)
    {
        var head = System.Text.Json.JsonSerializer.Deserialize<CookPublicationHead>(File.ReadAllBytes(CookPublicationPaths.Head(projectRoot)), CookPublicationDocument.JsonOptions)!;
        var document = System.Text.Json.JsonSerializer.Deserialize<CookPublicationDocument>(File.ReadAllBytes(CookPublicationPaths.Document(projectRoot, head.PublicationId)), CookPublicationDocument.JsonOptions)!;
        return [.. document.Roots.Where(static root => root.Owner == CookPublicationRootOwner.Project).Select(static root => root.Name).Order(StringComparer.Ordinal)];
    }
}
