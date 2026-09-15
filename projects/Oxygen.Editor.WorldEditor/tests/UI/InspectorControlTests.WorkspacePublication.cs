// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using AwesomeAssertions;
using DroidNet.Hosting.WinUI;
using DroidNet.Storage.Native;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentBrowser.Infrastructure.Assets;
using Oxygen.Editor.ContentBrowser.Materials;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Editor.MaterialEditor;
using Oxygen.Editor.World.Workspace;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Oxygen.Managed.Core.Diagnostics;
using Testably.Abstractions;

namespace Oxygen.Editor.World.Tests;

/// <summary>Exercises material creation, discovery and live updates through workspace publication.</summary>
public sealed partial class InspectorControlTests
{
    /// <summary>Every explicit scope publishes a saved material to existing native consumers.</summary>
    /// <param name="scope">The user-requested cook scope.</param>
    /// <returns>The end-to-end publication check.</returns>
    [TestMethod]
    [DataRow(CookTargetKind.Asset)]
    [DataRow(CookTargetKind.Folder)]
    [DataRow(CookTargetKind.CurrentScene)]
    [DataRow(CookTargetKind.Project)]
    public Task WorkspacePublicationRefreshesSharedMaterial(CookTargetKind scope)
        => EnqueueAsync(() => this.CheckWorkspacePublicationAsync(scope));

    /// <summary>Saving a material uses automatic cooking and preserves unsaved scene assignments.</summary>
    /// <returns>The automatic publication check.</returns>
    [TestMethod]
    public Task WorkspaceAutomaticPublicationPreservesUnsavedScene()
        => EnqueueAsync(() => this.CheckWorkspacePublicationAsync(scope: null));

    private static async Task SetPublicationMaterialColorAsync(MaterialDocumentService documents, Guid documentId, Vector4 color, CancellationToken cancellationToken)
    {
        foreach (var (field, value) in new[]
        {
            (MaterialFieldKeys.BaseColorR, color.X), (MaterialFieldKeys.BaseColorG, color.Y),
            (MaterialFieldKeys.BaseColorB, color.Z), (MaterialFieldKeys.BaseColorA, color.W),
        })
        {
            _ = (await documents.EditScalarAsync(documentId, new(field, value), cancellationToken).ConfigureAwait(true)).Succeeded.Should().BeTrue();
        }

        _ = (await documents.SaveAsync(documentId, cancellationToken).ConfigureAwait(true)).Succeeded.Should().BeTrue();
    }

    private static async Task<CookRunSnapshot> WaitForPublicationRunAsync(CatalogWorkloadServices services, HashSet<Guid> prior, CancellationToken cancellationToken)
    {
        while (true)
        {
            var runs = services.Runs.Runs.Where(run => !prior.Contains(run.OperationId)).ToArray();
            if (runs.Length > 0 && runs.All(static run => run.IsCompleted))
            {
                _ = runs.Should().OnlyContain(run => run.State == CookRunState.Succeeded || run.State == CookRunState.SucceededWithWarnings || run.State == CookRunState.UpToDate);
                return runs[^1];
            }

            await Task.Delay(20, cancellationToken).ConfigureAwait(true);
        }
    }

    private static async Task<string> PrepareSharedPublicationMaterialAsync(NativeSceneFixture fixture, CatalogWorkloadServices services, MaterialPickerService picker, MaterialDocumentService materials, MaterialDocument material, CancellationToken cancellationToken)
    {
        var red = new Vector4(1, 0, 0, 1);
        await SetPublicationMaterialColorAsync(materials, material.DocumentId, red, cancellationToken).ConfigureAwait(true);
        var before = await picker.ResolveAsync(material.MaterialUri, cancellationToken).ConfigureAwait(true);
        _ = before.Should().NotBeNull();
        _ = before!.CookStatus!.Freshness.Should().Be(AssetCookFreshness.NeedsCooking);
        _ = before.CookStatus.HasPublishedOutput.Should().BeFalse();
        _ = File.Exists(before.CookedPath).Should().BeFalse();
        _ = before.RuntimeAvailability.Should().NotBe(AssetRuntimeAvailability.Mounted);
        services.Runs.IsAutomaticCookingPaused = false;
        _ = await WaitForPublicationRunAsync(services, [], cancellationToken).ConfigureAwait(true);
        var choice = await picker.ResolveAsync(material.MaterialUri, cancellationToken).ConfigureAwait(true);
        _ = choice!.CookStatus!.Freshness.Should().Be(AssetCookFreshness.Current);
        _ = choice.RuntimeAvailability.Should().Be(AssetRuntimeAvailability.Mounted);
        _ = File.Exists(choice.CookedPath).Should().BeTrue();
        var nodes = fixture.Source.RootNodes.Select(static node => node.Id).ToArray();
        _ = (await fixture.Commands.EditMaterialSlotAsync(fixture.Context, nodes, 0, choice.MaterialUri, EditSessionToken.OneShot).ConfigureAwait(true)).Succeeded.Should().BeTrue();
        var first = await WaitForNodeAsync(fixture, nodes[0], value => value.MaterialBaseColors.Length == 1 && Vector4.Distance(value.MaterialBaseColors[0], red) < 0.001f, cancellationToken).ConfigureAwait(true);
        _ = await WaitForNodeAsync(fixture, nodes[1], value => value.MaterialBaseColors.Length == 1 && Vector4.Distance(value.MaterialBaseColors[0], red) < 0.001f, cancellationToken).ConfigureAwait(true);
        return first.MaterialKeys.Single();
    }

    private static async Task CheckSharedPublicationRefreshAsync(NativeSceneFixture fixture, CatalogWorkloadServices services, MaterialPickerService picker, MaterialDocumentService materials, MaterialDocument material, CookTargetKind? scope, string key, CancellationToken cancellationToken)
    {
        if (scope is not null)
        {
            _ = (await fixture.Commands.SaveSceneAsync(fixture.Context).ConfigureAwait(true)).Succeeded.Should().BeTrue();
        }

        var green = new Vector4(0, 1, 0, 1);
        var source = fixture.Source;
        var revision = fixture.Context.Metadata.ChangeVersion;
        var history = fixture.Context.History.UndoStack.Count;
        var dirty = fixture.Context.Metadata.IsDirty;
        var prior = services.Runs.Runs.Select(static run => run.OperationId).ToHashSet();
        services.Runs.IsAutomaticCookingPaused = true;
        await SetPublicationMaterialColorAsync(materials, material.DocumentId, green, cancellationToken).ConfigureAwait(true);
        if (scope is null)
        {
            services.Runs.IsAutomaticCookingPaused = false;
            var run = await WaitForPublicationRunAsync(services, prior, cancellationToken).ConfigureAwait(true);
            _ = run.Request.IsAutomatic.Should().BeTrue();
        }
        else
        {
            var sceneUri = new Uri("asset:///Content/Scenes/" + Uri.EscapeDataString(source.Name) + ".oscene.json");
            var cooked = await (scope switch
            {
                CookTargetKind.Asset => services.Pipeline.CookAssetAsync(material.MaterialUri, cancellationToken),
                CookTargetKind.Folder => services.Pipeline.CookFolderAsync(new("asset:///Content/Materials"), cancellationToken),
                CookTargetKind.CurrentScene => services.Pipeline.CookCurrentSceneAsync(sceneUri, cancellationToken),
                _ => services.Pipeline.CookProjectAsync(cancellationToken),
            }).ConfigureAwait(true);
            _ = cooked.Status.Should().BeOneOf(OperationStatus.Succeeded, OperationStatus.SucceededWithWarnings);
            _ = cooked.IsPublished.Should().BeTrue();
            _ = cooked.IsMounted.Should().BeTrue();
        }

        foreach (var node in source.RootNodes)
        {
            var refreshed = await fixture.ReadNodeAsync(node.Id, cancellationToken).ConfigureAwait(true);
            _ = refreshed.MaterialBaseColors.Should().ContainSingle().Which.Should().Be(green);
            _ = refreshed.MaterialKeys.Should().ContainSingle().Which.Should().Be(key);
        }

        _ = fixture.Source.Should().BeSameAs(source);
        _ = fixture.Context.Metadata.ChangeVersion.Should().Be(revision);
        _ = fixture.Context.History.UndoStack.Should().HaveCount(history);
        _ = fixture.Context.Metadata.IsDirty.Should().Be(dirty);
        var updated = await picker.ResolveAsync(material.MaterialUri, cancellationToken).ConfigureAwait(true);
        _ = updated!.CookStatus!.Freshness.Should().Be(AssetCookFreshness.Current);
        _ = updated.RuntimeAvailability.Should().Be(AssetRuntimeAvailability.Mounted);
    }

    private async Task CheckWorkspacePublicationAsync(CookTargetKind? scope, Func<PublicationScenario, CancellationToken, Task>? verify = null)
    {
        var fixture = new NativeSceneFixture(automatic: false, scene =>
        {
            AddGeometryNode(scene, "Cube");
            AddGeometryNode(scene, "Sphere");
        });
        await using var lifetime = fixture.ConfigureAwait(true);
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(this.TestContext.CancellationToken);
        timeout.CancelAfter(TimeSpan.FromSeconds(120));
        using var services = new CatalogWorkloadServices(fixture);
        services.Runs.IsAutomaticCookingPaused = true;
        using var automatic = new AutomaticCookService(services.Projects, services.Pipeline, services.Runs, NullLogger<AutomaticCookService>.Instance);
        using var catalog = new ProjectAssetCatalog(services.Projects, services.Storage, services.Builtins);
        using var provider = new ContentBrowserAssetProvider(catalog, services.Projects, services.Scopes, new AssetIdentityReducer(), services.Pipeline, services.Documents, services.Runs, fixture.Runtime);
        using var picker = new MaterialPickerService(provider);
        await fixture.InitializeAsync(timeout.Token).ConfigureAwait(true);
        using var publication = fixture.RegisterWorkspacePublication(services, catalog);
        var materials = new MaterialDocumentService(
            automatic,
            new ProjectMaterialSourcePathResolver(services.Projects),
            new MaterialCookService(services.Pipeline, services.Projects, NullLogger<MaterialCookService>.Instance),
            services.Documents,
            new NativeAtomicFileStore(new RealFileSystem()));
        var uri = new Uri("asset:///Content/Materials/Shared.omat.json");
        var material = await materials.CreateAsync(uri, timeout.Token).ConfigureAwait(true);
        try
        {
            var key = await PrepareSharedPublicationMaterialAsync(fixture, services, picker, materials, material, timeout.Token).ConfigureAwait(true);
            if (verify is null)
            {
                await CheckSharedPublicationRefreshAsync(fixture, services, picker, materials, material, scope, key, timeout.Token).ConfigureAwait(true);
            }
            else
            {
                await verify(new(fixture, services, picker, materials, material, key), timeout.Token).ConfigureAwait(true);
            }
        }
        finally
        {
            await materials.CloseAsync(material.DocumentId, discard: true, timeout.Token).ConfigureAwait(true);
            foreach (var pending in services.Runs.Runs.Where(static run => !run.IsCompleted))
            {
                await services.Runs.CancelAsync(pending.OperationId).ConfigureAwait(true);
            }
        }
    }

    private sealed record PublicationScenario(NativeSceneFixture Fixture, CatalogWorkloadServices Services, MaterialPickerService Picker, MaterialDocumentService Materials, MaterialDocument Material, string Key);

    private sealed partial class NativeSceneFixture
    {
        public IDisposable RegisterWorkspacePublication(CatalogWorkloadServices services, IProjectAssetCatalog catalog)
        {
            var project = services.Projects.ActiveProject!;
            return services.Publication.RegisterPreview(project, () => this.hosting.Dispatcher.DispatchAsync(
                () => Task.FromResult<ICookPublicationPreview?>(new WorkspacePublicationPreview(project, this.engine, this.hosting, services.Mounts, catalog, this.messenger, () => ReferenceEquals(project, services.Projects.ActiveProject)))));
        }
    }
}
