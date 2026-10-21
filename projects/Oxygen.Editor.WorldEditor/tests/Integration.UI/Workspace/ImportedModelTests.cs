// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using AwesomeAssertions;
using Moq;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Authoring;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Editor.LivePreview.Assets;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Scenes.Edits.Assets;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Slots;
using Oxygen.Editor.WorldEditor.TestSupport;
using Oxygen.Managed.Core;
using static Oxygen.Editor.WorldEditor.TestSupport.CatalogWorkload;
using static Oxygen.Editor.WorldEditor.TestSupport.ModelImportScenario;
using static Oxygen.Editor.WorldEditor.TestSupport.NativeSceneAssertions;
using static Oxygen.Editor.WorldEditor.TestSupport.NativeSceneData;

namespace Oxygen.Editor.WorldEditor.Integration.UI.Tests.Workspace;

[TestClass]
public sealed partial class ImportedModelTests : DroidNet.Tests.VisualUserInterfaceTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Imported and library-only assets retain native identity through their use in a scene, history and saved scene reload.</summary>
    /// <param name="format">The qualified model source format.</param>
    /// <param name="libraryOnly">Whether the consumer has only the cooked library, without original sources.</param>
    /// <returns>The native typed-use integration check.</returns>
    [TestMethod]
    [DataRow("gltf", false)]
    [DataRow("fbx", false)]
    [DataRow("gltf", true)]
    [DataRow("fbx", true)]
    public Task ImportedAssetsReachNativeThroughHistoryAndReopen(string format, bool libraryOnly) => EnqueueAsync(async () =>
    {
        var fixture = new NativeSceneFixture(automatic: false, scene => AddGeometryNode(scene, "Cube"));
        await using var lifetime = fixture.ConfigureAwait(true);
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(this.TestContext.CancellationToken);
        timeout.CancelAfter(TimeSpan.FromSeconds(120));
        using var services = new CatalogWorkloadServices(fixture);
        var imported = libraryOnly ? await CreateImportedLibraryAsync(fixture, services, format, timeout.Token).ConfigureAwait(true) : null;
        await using var catalog = new AssetCatalog(services.Projects, services.Storage, services.EngineContent, services.Publication);
        using var runtimeStatus = new AssetRuntimeStatusSource(fixture.Runtime);
        await using var provider = new AssetFeed(catalog, services.Projects, services.Scopes, services.Pipeline, services.Documents, services.Runs, runtimeStatus, Mock.Of<IAssetRelocationService>(), Mock.Of<IAssetAuthoring>());
        await fixture.InitializeAsync(timeout.Token).ConfigureAwait(true);
        await using var previewRegistration = await fixture.StartWorkspacePreviewAsync(services, catalog).ConfigureAwait(true);
        if (imported is null)
        {
            imported = await ImportTypedModelAsync(fixture, services, format, timeout.Token).ConfigureAwait(true);
            _ = imported.IsMounted.Should().BeTrue();
        }
        else
        {
            using var publication = await services.Publication.AcquireForMountAsync(services.Projects.ActiveProject!, timeout.Token).ConfigureAwait(true);
            var bindings = publication.Roots.Zip(publication.RootPaths, static (root, path) => new Oxygen.Editor.Runtime.Engine.RuntimeCookedRoot(
                path,
                root.Container == Oxygen.Editor.ContentPipeline.Publication.CookPublicationRootContainer.PakFile
                    ? Oxygen.Editor.Runtime.Engine.RuntimeCookedRootKind.PakFile
                    : Oxygen.Editor.Runtime.Engine.RuntimeCookedRootKind.LooseFolder)).ToArray();
            var mounts = await services.Mounts.PrepareAsync(services.Projects.ActiveProject!, publication, timeout.Token).ConfigureAwait(true);
            await fixture.Runtime.RefreshProjectCookedRootsAsync(bindings, mounts).ConfigureAwait(true);
        }

        var geometry = imported.CookedAssets.Single(static asset => asset.Kind == ContentCookAssetKind.Geometry);
        var material = imported.CookedAssets.Single(static asset => asset.Kind == ContentCookAssetKind.Material);
        var geometryRow = await provider.ResolveAsync(geometry.CookedAssetUri, timeout.Token).ConfigureAwait(true);
        var materialRow = await provider.ResolveAsync(material.CookedAssetUri, timeout.Token).ConfigureAwait(true);
        AssertImportedAssetInformation(geometryRow!, libraryOnly);
        AssertImportedAssetInformation(materialRow!, libraryOnly);
        using var demand = fixture.CreateImportedAssetDemand(services, provider);
        var cookCount = services.Runs.Runs.Count;
        await CheckImportedAssetEditsAsync(fixture, services.Pipeline, geometryRow!, materialRow!, timeout.Token).ConfigureAwait(true);
        _ = services.Runs.Runs.Should().HaveCount(cookCount, "using verified mounted outputs must not start another cook");
        var sceneCook = await services.Pipeline.CookCurrentSceneAsync(AssetPath.Create("Content", $"Scenes/{fixture.Source.Name}.oscene"), timeout.Token).ConfigureAwait(true);
        _ = sceneCook.IsPublished.Should().BeTrue(DescribeWorkloadCook(sceneCook));
    });

    private static void AssertImportedAssetInformation(AssetEntry row, bool libraryOnly)
    {
        _ = row.Should().NotBeNull();
        _ = row.CookedMetadata.Should().NotBeNull();
        _ = row.RuntimeAvailability.Should().Be(AssetRuntimeAvailability.Mounted);
        _ = row.CanCook.Should().Be(!libraryOnly);
        _ = row.DescriptorPath.Should().BeNull();
        if (libraryOnly)
        {
            _ = row.ImportSource.Should().BeNull();
            _ = row.SourcePath.Should().BeNull();
        }
        else
        {
            _ = row.ImportSource.Should().NotBeNull();
            _ = File.Exists(row.ImportSourcePath!.FullPath).Should().BeTrue();
            _ = row.CookStatus!.Freshness.Should().Be(AssetCookFreshness.Current);
        }
    }

    private static async Task CheckImportedAssetEditsAsync(NativeSceneFixture fixture, IGeometryMaterialSlotProvider inventories, AssetEntry geometry, AssetEntry material, CancellationToken cancellationToken)
    {
        var node = fixture.Source.RootNodes[0];
        var original = await AssertGeometryAsync(fixture, node.Id, "Cube", cancellationToken).ConfigureAwait(true);
        _ = (await fixture.Authoring.ExecuteAsync(new SetGeometry([node.Id], geometry.Identity)).ConfigureAwait(true)).Succeeded.Should().BeTrue();
        var selected = await WaitForNodeAsync(fixture, node.Id, static value => value.IndexCount == 3, cancellationToken).ConfigureAwait(true);
        _ = selected.GeometryKey.Should().Be(ImportedNativeKey(geometry));
        _ = fixture.Document.History.UndoStack.Should().ContainSingle();
        var slot = await ReadSingleSlotAsync(inventories, fixture.Projects.ActiveProject!, geometry.Identity, cancellationToken).ConfigureAwait(true);
        var built = await MaterialSlotEdits.SetAsync(inventories, fixture.Projects.ActiveProject!, [node.Id], slot, material.Identity, cancellationToken).ConfigureAwait(true);
        _ = built.Error.Should().BeNull("the current native slot must be editable");
        _ = (await fixture.Authoring.ExecuteAsync(built.Edit!).ConfigureAwait(true)).Succeeded.Should().BeTrue();
        _ = fixture.Document.History.UndoStack.Should().HaveCount(2);
        _ = ReadMaterialAsset(node).Should().Be(material.Identity);
        var assigned = await WaitForNodeAsync(fixture, node.Id, value => value.MaterialBaseColors.Length == 1 && Vector4.Distance(value.MaterialBaseColors[0], new(0.8f, 0.2f, 0.1f, 1)) < 0.001f, cancellationToken).ConfigureAwait(true);
        _ = assigned.MaterialKeys.Should().ContainSingle().Which.Should().Be(ImportedNativeKey(material));
        _ = node.Components.OfType<GeometryComponent>().Single().Geometry.Should().Be(geometry.Identity);
        await fixture.Document.History.UndoAsync(cancellationToken).ConfigureAwait(true);
        _ = node.Components.OfType<GeometryComponent>().Single().OverrideSlots.Should().BeEmpty();
        await fixture.Document.History.UndoAsync(cancellationToken).ConfigureAwait(true);
        _ = await WaitForNodeAsync(fixture, node.Id, value => string.Equals(value.GeometryKey, original.GeometryKey, StringComparison.Ordinal), cancellationToken).ConfigureAwait(true);
        await AssertMaterialAsync(fixture, node.Id, original.MaterialKeys.Single(), cancellationToken).ConfigureAwait(true);
        await fixture.Document.History.RedoAsync(cancellationToken).ConfigureAwait(true);
        _ = await WaitForNodeAsync(fixture, node.Id, static value => value.IndexCount == 3, cancellationToken).ConfigureAwait(true);
        await fixture.Document.History.RedoAsync(cancellationToken).ConfigureAwait(true);
        await AssertMaterialAsync(fixture, node.Id, assigned.MaterialKeys.Single(), cancellationToken).ConfigureAwait(true);
        await fixture.SaveAndReopenAsync(cancellationToken).ConfigureAwait(true);
        var reopened = await WaitForNodeAsync(fixture, node.Id, value => string.Equals(value.GeometryKey, selected.GeometryKey, StringComparison.Ordinal), cancellationToken).ConfigureAwait(true);
        _ = reopened.MaterialKeys.Should().Equal(assigned.MaterialKeys);
        _ = fixture.Source.RootNodes[0].Components.OfType<GeometryComponent>().Single().Geometry.Should().Be(geometry.Identity);
        _ = ReadMaterialAsset(fixture.Source.RootNodes[0]).Should().Be(material.Identity);
    }

    // The geometry's one material slot, as its current inventory names it.
    private static async Task<MaterialSlotTarget> ReadSingleSlotAsync(IGeometryMaterialSlotProvider inventories, ProjectContext project, AssetPath geometry, CancellationToken cancellationToken)
    {
        var inventory = await inventories.ReadAsync(project, geometry, cancellationToken).ConfigureAwait(true);
        _ = inventory.Should().NotBeNull("the imported geometry must have a current slot inventory");
        var slot = inventory!.Slots.Should().ContainSingle().Which;
        return new(geometry, slot.SlotId, inventory.LayoutRevision);
    }

    private static string ImportedNativeKey(AssetEntry asset)
    {
        var bytes = new byte[16];
        asset.CookedMetadata!.AssetKey.WriteBytes(bytes);
        return new Guid(bytes, bigEndian: true).ToString();
    }
}
