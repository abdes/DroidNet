// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.TestSupport;
using Oxygen.Managed.Core.Compatibility;
using Oxygen.Managed.Core.Diagnostics;
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
        using var workspace = new CookWorkspace();
        await PrepareIncrementalSceneAsync(workspace).ConfigureAwait(false);
        workspace.WriteMaterial("Content/Materials/Blue.omat.json", "Blue");
        using var compatibility = EditorNativeCompatibilityService.ForCooking();
        var api = CreateRecordingApi(compatibility);
        var pipeline = CreateIncrementalService(workspace, api, compatibility);
        AssertCookSucceeded(await pipeline.CookProjectAsync(this.TestContext.CancellationToken).ConfigureAwait(false));
        var previousName = "Main";
        foreach (var name in new[] { "Small Scene", "Main", "Small Scene" })
        {
            workspace.Scene.Name = name;
            await workspace.WriteSceneAsync($"Content/Scenes/{name}.oscene.json").ConfigureAwait(false);
            File.Delete(Path.Combine(workspace.Root, "Content", "Scenes", $"{previousName}.oscene.json"));
            var result = await pipeline.CookAssetAsync(new($"asset:///Content/Scenes/{Uri.EscapeDataString(name)}.oscene.json"), this.TestContext.CancellationToken).ConfigureAwait(false);
            AssertCookSucceeded(result);
            _ = result.IsPublished.Should().BeTrue();
            var root = workspace.CookedRoot("Content");
            var cookedName = ContentPipelinePaths.NormalizeSceneDescriptorName(name);
            var previousCookedName = ContentPipelinePaths.NormalizeSceneDescriptorName(previousName);
            var inventory = await api.ReadInventoryAsync(root, null, this.TestContext.CancellationToken).ConfigureAwait(false);
            _ = inventory.IsValid.Should().BeTrue();
            _ = inventory.Assets.Select(static asset => asset.VirtualPath).Should()
                .Contain($"/Content/Scenes/{cookedName}.oscene")
                .And.Contain("/Content/Materials/Blue.omat")
                .And.NotContain($"/Content/Scenes/{previousCookedName}.oscene");
            _ = Directory.EnumerateFiles(root, $"{previousCookedName}.oscene", SearchOption.AllDirectories).Should().BeEmpty();
            previousName = name;
        }

        _ = (await pipeline.CookProjectAsync(this.TestContext.CancellationToken).ConfigureAwait(false)).IsUpToDate.Should().BeTrue();
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
        using var workspace = new CookWorkspace();
        workspace.WriteMaterial("Content/Materials/Blue.omat.json", "Blue");
        _ = await WriteRetainedModelAsync(workspace, "Model", "gltf", this.TestContext.CancellationToken).ConfigureAwait(false);
        using var compatibility = EditorNativeCompatibilityService.ForCooking();
        var api = CreateRecordingApi(compatibility);
        var pipeline = CreateIncrementalService(workspace, api, compatibility);
        var first = await pipeline.CookProjectAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        AssertCookSucceeded(first);
        var root = workspace.CookedRoot("Content");
        if (failure == "unowned output")
        {
            await File.WriteAllTextAsync(Path.Combine(root, "Unowned.otex"), "unowned derived bytes", this.TestContext.CancellationToken).ConfigureAwait(false);
        }

        var payload = Directory.EnumerateFiles(root, "buffers.data", SearchOption.AllDirectories).Single();
        var bytes = await File.ReadAllBytesAsync(payload, this.TestContext.CancellationToken).ConfigureAwait(false);
        bytes[^1] ^= 0xFF;
        await File.WriteAllBytesAsync(payload, bytes, this.TestContext.CancellationToken).ConfigureAwait(false);
        var damaged = ReadOutputIdentities(workspace.Root);
        if (failure == "missing source")
        {
            File.Delete(Path.Combine(workspace.Root, "Content/SourceMedia/DCC/Model/model.gltf"));
        }

        api.FailNextImport = failure == "native";
        api.Imported.Clear();
        var result = await pipeline.CookAssetAsync(new("asset:///Content/Materials/Blue.omat.json"), this.TestContext.CancellationToken).ConfigureAwait(false);
        if (failure != "none")
        {
            _ = result.Status.Should().BeOneOf(OperationStatus.Failed, OperationStatus.PartiallySucceeded);
            _ = result.IsPublished.Should().BeFalse();
            _ = ReadOutputIdentities(workspace.Root).Should().BeEquivalentTo(damaged);
            if (failure == "missing source")
            {
                _ = result.Diagnostics.Should().Contain(static diagnostic => diagnostic.AffectedPath != null
                    && diagnostic.AffectedPath.EndsWith("model.gltf", StringComparison.Ordinal));
                _ = api.Imported.Should().BeEmpty();
            }
            else if (failure == "unowned output")
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
        _ = (await api.ReadInventoryAsync(workspace.CookedRoot("Content"), null, this.TestContext.CancellationToken).ConfigureAwait(false)).IsValid.Should().BeTrue();
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
        using var workspace = new CookWorkspace();
        workspace.WriteMaterial("Content/Materials/Blue.omat.json", "Blue");
        workspace.WriteMaterial("Content/Materials/Red.omat.json", "Red");
        using var compatibility = EditorNativeCompatibilityService.ForCooking();
        var api = CreateRecordingApi(compatibility);
        var pipeline = CreateIncrementalService(workspace, api, compatibility);
        var first = await pipeline.CookProjectAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        AssertCookSucceeded(first);
        var descriptor = first.Inspection!.Assets.Single(static asset => asset.VirtualPath == "/Content/Materials/Blue.omat");
        var path = Path.Combine(first.Inspection.CookedRoot, descriptor.DescriptorRelativePath!);
        if (missing)
        {
            File.Delete(path);
        }
        else
        {
            await File.WriteAllBytesAsync(path, [0], this.TestContext.CancellationToken).ConfigureAwait(false);
        }

        var result = await pipeline.CookAssetAsync(new("asset:///Content/Materials/Blue.omat.json"), this.TestContext.CancellationToken).ConfigureAwait(false);
        AssertCookSucceeded(result);
        _ = result.CookedAssets.Select(static asset => asset.VirtualPath).Should().BeEquivalentTo(first.CookedAssets.Select(static asset => asset.VirtualPath));
        _ = (await pipeline.CookProjectAsync(this.TestContext.CancellationToken).ConfigureAwait(false)).IsUpToDate.Should().BeTrue();
    }
}
