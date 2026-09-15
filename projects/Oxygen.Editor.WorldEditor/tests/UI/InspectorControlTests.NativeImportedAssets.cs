// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using AwesomeAssertions;
using CommunityToolkit.Mvvm.Messaging;
using CommunityToolkit.WinUI;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Xaml.Controls;
using Moq;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentBrowser.Infrastructure.Assets;
using Oxygen.Editor.ContentBrowser.Materials;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Editor.World.Inspector.Geometry;
using Oxygen.Editor.World.Messages;
using Oxygen.Editor.World.SceneExplorer.Services;
using Oxygen.Editor.World.Services;
using Oxygen.Managed.Assets.Catalog;

namespace Oxygen.Editor.World.Tests;

/// <summary>Connects native model import and cooked libraries to the inspector's typed asset controls.</summary>
public sealed partial class InspectorControlTests
{
    /// <summary>Imported and library-only assets retain native identity through picking, history and saved scene reload.</summary>
    /// <param name="format">The qualified model source format.</param>
    /// <param name="libraryOnly">Whether the consumer has only the cooked library, without original sources.</param>
    /// <returns>The native typed-use integration check.</returns>
    [TestMethod]
    [DataRow("gltf", false)]
    [DataRow("fbx", false)]
    [DataRow("gltf", true)]
    [DataRow("fbx", true)]
    public Task ImportedAssetsReachNativeInspectorHistoryAndReopen(string format, bool libraryOnly) => EnqueueAsync(async () =>
    {
        var fixture = new NativeSceneFixture(automatic: false, scene => AddGeometryNode(scene, "Cube"));
        await using var lifetime = fixture.ConfigureAwait(true);
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(this.TestContext.CancellationToken);
        timeout.CancelAfter(TimeSpan.FromSeconds(120));
        using var services = new CatalogWorkloadServices(fixture);
        var imported = libraryOnly ? await CreateImportedLibraryAsync(fixture, services, format, timeout.Token).ConfigureAwait(true) : null;
        using var catalog = new ProjectAssetCatalog(services.Projects, services.Storage, services.Builtins);
        using var provider = new ContentBrowserAssetProvider(catalog, services.Projects, services.Scopes, new AssetIdentityReducer(), services.Pipeline, services.Documents, services.Runs, fixture.Runtime);
        using var picker = new MaterialPickerService(provider);
        await fixture.InitializeAsync(timeout.Token).ConfigureAwait(true);
        using var publication = fixture.RegisterWorkspacePublication(services, catalog);
        if (imported is null)
        {
            imported = await ImportTypedModelAsync(fixture, services, format, timeout.Token).ConfigureAwait(true);
            _ = imported.IsMounted.Should().BeTrue();
        }
        else
        {
            var mounts = await services.Mounts.PrepareAsync(services.Projects.ActiveProject!, [], await CookOutputLease.AcquireReadAsync(fixture.ProjectRoot, timeout.Token).ConfigureAwait(true), timeout.Token).ConfigureAwait(true);
            await fixture.Runtime.RefreshProjectCookedRootsAsync(mounts.Roots, mounts).ConfigureAwait(true);
        }

        var geometry = imported.CookedAssets.Single(static asset => asset.Kind == ContentCookAssetKind.Geometry);
        var material = imported.CookedAssets.Single(static asset => asset.Kind == ContentCookAssetKind.Material);
        var geometryRow = await provider.ResolveAsync(geometry.CookedAssetUri, timeout.Token).ConfigureAwait(true);
        var materialRow = await provider.ResolveAsync(material.CookedAssetUri, timeout.Token).ConfigureAwait(true);
        AssertImportedAssetInformation(geometryRow!, libraryOnly);
        AssertImportedAssetInformation(materialRow!, libraryOnly);
        using var demand = fixture.CreateImportedAssetDemand(services, provider);
        using var host = fixture.CreateInspectorHost([fixture.Source.RootNodes[0]], provider, picker, services.Builtins, demand);
        var view = new GeometryView { ViewModel = host.PropertyEditors.OfType<GeometryViewModel>().Single() };
        var scroller = new ScrollViewer { Content = view, VerticalScrollBarVisibility = ScrollBarVisibility.Auto, Width = 480, Height = 560 };
        await LoadTestContentAsync(scroller).ConfigureAwait(true);
        try
        {
            var cookCount = services.Runs.Runs.Count;
            await CheckImportedAssetControlsAsync(fixture, scroller, geometryRow!, materialRow!, timeout.Token).ConfigureAwait(true);
            _ = services.Runs.Runs.Should().HaveCount(cookCount, "using verified mounted outputs must not start another cook");
            _ = (await services.Pipeline.CookCurrentSceneAsync(new("asset:///Content/Scenes/" + Uri.EscapeDataString(fixture.Source.Name) + ".oscene.json"), timeout.Token).ConfigureAwait(true)).IsPublished.Should().BeTrue();
        }
        finally
        {
            await UnloadTestContentAsync(scroller).ConfigureAwait(true);
        }
    });

    private static void AssertImportedAssetInformation(ContentBrowserAssetItem row, bool libraryOnly)
    {
        _ = row.Should().NotBeNull();
        _ = row.CookedMetadata.Should().NotBeNull();
        _ = row.RuntimeAvailability.Should().Be(AssetRuntimeAvailability.Mounted);
        _ = row.CanCook.Should().Be(!libraryOnly);
        _ = row.DescriptorPath.Should().BeNull();
        if (libraryOnly)
        {
            _ = row.ImportSourceUri.Should().BeNull();
            _ = row.SourcePath.Should().BeNull();
        }
        else
        {
            _ = row.ImportSourceUri.Should().NotBeNull();
            _ = File.Exists(row.ImportSourcePath).Should().BeTrue();
            _ = row.CookStatus!.Freshness.Should().Be(AssetCookFreshness.Current);
        }
    }

    private static async Task CheckImportedAssetControlsAsync(NativeSceneFixture fixture, ScrollViewer scroller, ContentBrowserAssetItem geometry, ContentBrowserAssetItem material, CancellationToken cancellationToken)
    {
        var view = (GeometryView)scroller.Content;
        var node = fixture.Source.RootNodes[0];
        var original = await AssertGeometryAsync(fixture, node.Id, "Cube", cancellationToken).ConfigureAwait(true);
        var materialButton = (SplitButton)await FindInspectorControlAsync(scroller, () => view.FindDescendant<SplitButton>(button => string.Equals(button.Name, "MaterialSplitButton", StringComparison.Ordinal)), "Material", cancellationToken).ConfigureAwait(true);
        await DismissImportedAssetPickerAsync(materialButton, cancellationToken).ConfigureAwait(true);
        _ = fixture.Context.History.UndoStack.Should().BeEmpty();
        await PickAssetAsync(materialButton, material.DisplayName, material: true, cancellationToken).ConfigureAwait(true);
        var assigned = await WaitForNodeAsync(fixture, node.Id, value => value.MaterialBaseColors.Length == 1 && Vector4.Distance(value.MaterialBaseColors[0], new(0.8f, 0.2f, 0.1f, 1)) < 0.001f, cancellationToken).ConfigureAwait(true);
        _ = assigned.MaterialKeys.Should().ContainSingle().Which.Should().Be(ImportedNativeKey(material));
        var geometryButton = (SplitButton)await FindInspectorControlAsync(scroller, () => view.FindDescendant<SplitButton>(button => string.Equals(button.Name, "AssetSplitButton", StringComparison.Ordinal)), "Geometry", cancellationToken).ConfigureAwait(true);
        await DismissImportedAssetPickerAsync(geometryButton, cancellationToken).ConfigureAwait(true);
        _ = fixture.Context.History.UndoStack.Should().ContainSingle();
        await PickAssetAsync(geometryButton, geometry.DisplayName, material: false, cancellationToken).ConfigureAwait(true);
        var selected = await WaitForNodeAsync(fixture, node.Id, static value => value.IndexCount == 3, cancellationToken).ConfigureAwait(true);
        _ = selected.GeometryKey.Should().Be(ImportedNativeKey(geometry));
        _ = fixture.Context.History.UndoStack.Should().HaveCount(2);
        _ = selected.MaterialKeys.Should().Equal(assigned.MaterialKeys);
        _ = node.Components.OfType<GeometryComponent>().Single().Geometry!.Uri.Should().Be(geometry.IdentityUri);
        _ = ReadMaterialUri(node).Should().Be(material.IdentityUri);
        await fixture.Context.History.UndoAsync(cancellationToken).ConfigureAwait(true);
        _ = await WaitForNodeAsync(fixture, node.Id, value => string.Equals(value.GeometryKey, original.GeometryKey, StringComparison.Ordinal), cancellationToken).ConfigureAwait(true);
        await fixture.Context.History.UndoAsync(cancellationToken).ConfigureAwait(true);
        await AssertMaterialAsync(fixture, node.Id, original.MaterialKeys.Single(), cancellationToken).ConfigureAwait(true);
        await fixture.Context.History.RedoAsync(cancellationToken).ConfigureAwait(true);
        await AssertMaterialAsync(fixture, node.Id, assigned.MaterialKeys.Single(), cancellationToken).ConfigureAwait(true);
        await fixture.Context.History.RedoAsync(cancellationToken).ConfigureAwait(true);
        _ = await WaitForNodeAsync(fixture, node.Id, value => string.Equals(value.GeometryKey, selected.GeometryKey, StringComparison.Ordinal), cancellationToken).ConfigureAwait(true);
        await fixture.SaveAndReopenAsync(cancellationToken).ConfigureAwait(true);
        var reopened = await WaitForNodeAsync(fixture, node.Id, value => string.Equals(value.GeometryKey, selected.GeometryKey, StringComparison.Ordinal), cancellationToken).ConfigureAwait(true);
        _ = reopened.MaterialKeys.Should().Equal(assigned.MaterialKeys);
        _ = fixture.Source.RootNodes[0].Components.OfType<GeometryComponent>().Single().Geometry!.Uri.Should().Be(geometry.IdentityUri);
        _ = ReadMaterialUri(fixture.Source.RootNodes[0]).Should().Be(material.IdentityUri);
    }

    private static string ImportedNativeKey(ContentBrowserAssetItem asset)
    {
        var bytes = new byte[16];
        asset.CookedMetadata!.AssetKey.WriteBytes(bytes);
        return new Guid(bytes, bigEndian: true).ToString();
    }

    private static async Task DismissImportedAssetPickerAsync(SplitButton button, CancellationToken cancellationToken)
    {
        var flyout = (Flyout)button.Flyout;
        flyout.AreOpenCloseAnimationsEnabled = false;
        var closed = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        void OnClosed(object? sender, object args) => closed.TrySetResult();
        flyout.Closed += OnClosed;
        try
        {
            flyout.ShowAt(button);
            await WaitForRenderAsync().WaitAsync(cancellationToken).ConfigureAwait(true);
            flyout.Hide();
            await closed.Task.WaitAsync(cancellationToken).ConfigureAwait(true);
        }
        finally
        {
            flyout.Closed -= OnClosed;
            flyout.Hide();
        }
    }

    private sealed partial class NativeSceneFixture
    {
        public SceneContentDemandService CreateImportedAssetDemand(CatalogWorkloadServices services, ContentBrowserAssetProvider provider)
        {
            var demand = new SceneContentDemandService(this.hosting, provider, services.Pipeline, services.Projects, this.documents.Object, this.sync, Mock.Of<ISceneExplorerService>(), this.messenger, default, NullLogger<SceneContentDemandService>.Instance);
            _ = this.messenger.Send(new SceneAuthoringLoadedMessage(this.Source, this.Context.Metadata));
            return demand;
        }
    }
}
