// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using System.Security.Cryptography;
using System.Text;
using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;
using Moq;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Authoring;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Editor.Documents;
using Oxygen.Editor.LivePreview.Assets;
using Oxygen.Editor.Materials.Documents;
using Oxygen.Editor.Materials.Properties;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Scenes.Edits.Assets;
using Oxygen.Editor.Schemas;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;
using static Oxygen.Editor.WorldEditor.TestSupport.NativeSceneAssertions;
using static Oxygen.Editor.WorldEditor.TestSupport.NativeSceneData;

namespace Oxygen.Editor.WorldEditor.TestSupport;

internal static class PublicationWorkflows
{
    internal static Dictionary<string, string> ReadPublishedHashes(FileSystemPath projectRoot)
    {
        var headPath = CookPublicationPaths.Head(projectRoot.FullPath);
        if (!File.Exists(headPath))
        {
            return [];
        }

        var head = System.Text.Json.JsonSerializer.Deserialize<CookPublicationHead>(File.ReadAllBytes(headPath), CookPublicationDocument.JsonOptions)!;
        var documentPath = CookPublicationPaths.Document(projectRoot.FullPath, head.PublicationId);
        var document = System.Text.Json.JsonSerializer.Deserialize<CookPublicationDocument>(File.ReadAllBytes(documentPath), CookPublicationDocument.JsonOptions)!;
        return document.Roots.SelectMany(root => Directory.GetFiles(root.ResolvePath(projectRoot.FullPath), "*", SearchOption.AllDirectories)).Append(headPath).Append(documentPath).ToDictionary(static path => path, static path => Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(path))), StringComparer.OrdinalIgnoreCase);
    }

    internal static async Task SetPublicationMaterialColorAsync(MaterialEdits edits, Document material, Vector4 color, CancellationToken cancellationToken)
    {
        var edit = new PropertyEdit.Builder()
            .Set(MaterialDescriptors.BaseColorR, color.X)
            .Set(MaterialDescriptors.BaseColorG, color.Y)
            .Set(MaterialDescriptors.BaseColorB, color.Z)
            .Set(MaterialDescriptors.BaseColorA, color.W)
            .Build();
        _ = edits.Apply(material, edit).Succeeded.Should().BeTrue();
        var saved = await material.SaveAsync(cancellationToken).ConfigureAwait(true);
        _ = saved.Succeeded.Should().BeTrue(saved.Message);
    }

    internal static async Task<CookRunSnapshot> WaitForPublicationRunAsync(CatalogWorkloadServices services, HashSet<Guid> prior, CancellationToken cancellationToken)
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

    internal static async Task<string> PrepareSharedPublicationMaterialAsync(NativeSceneFixture fixture, CatalogWorkloadServices services, IAssetFeed feed, MaterialEdits materials, Document material, CancellationToken cancellationToken)
    {
        var red = new Vector4(1, 0, 0, 1);
        await SetPublicationMaterialColorAsync(materials, material, red, cancellationToken).ConfigureAwait(true);
        var before = await feed.ResolveAsync(material.File.Asset, cancellationToken).ConfigureAwait(true);
        _ = before.Should().NotBeNull();
        _ = before!.CookStatus!.Freshness.Should().Be(AssetCookFreshness.NeedsCooking);
        _ = before.CookStatus.HasPublishedOutput.Should().BeFalse();
        _ = File.Exists(before.CookedPath?.FullPath).Should().BeFalse();
        _ = before.RuntimeAvailability.Should().NotBe(AssetRuntimeAvailability.Mounted);
        _ = (await services.Pipeline.CookAssetAsync(material.File.Asset, cancellationToken).ConfigureAwait(true)).IsPublished.Should().BeTrue();
        var choice = await feed.ResolveAsync(material.File.Asset, cancellationToken).ConfigureAwait(true);
        _ = choice!.CookStatus!.Freshness.Should().Be(AssetCookFreshness.Current);
        _ = choice.RuntimeAvailability.Should().Be(AssetRuntimeAvailability.Mounted);
        _ = File.Exists(choice.CookedPath?.FullPath).Should().BeTrue();
        var nodes = fixture.Source.RootNodes.Select(static node => node.Id).ToArray();
        fixture.Document.History.BeginChangeSet("Assign shared material");
        try
        {
            foreach (var nodeId in nodes)
            {
                var target = await fixture.ReadSingleMaterialSlotAsync(nodeId, cancellationToken).ConfigureAwait(true);
                var result = await fixture.Authoring.ExecuteAsync(new SetMaterialSlot([nodeId], target, choice.Identity)).ConfigureAwait(true);
                _ = result.Succeeded.Should().BeTrue();
            }
        }
        finally
        {
            fixture.Document.History.EndChangeSet();
        }

        var first = await WaitForNodeAsync(fixture, nodes[0], value => value.MaterialBaseColors.Length == 1 && Vector4.Distance(value.MaterialBaseColors[0], red) < 0.001f, cancellationToken).ConfigureAwait(true);
        _ = await WaitForNodeAsync(fixture, nodes[1], value => value.MaterialBaseColors.Length == 1 && Vector4.Distance(value.MaterialBaseColors[0], red) < 0.001f, cancellationToken).ConfigureAwait(true);
        return first.MaterialKeys.Single();
    }

    internal static async Task CheckSharedPublicationRefreshAsync(NativeSceneFixture fixture, CatalogWorkloadServices services, IAssetFeed feed, MaterialEdits materials, Document material, CookTargetKind? scope, string key, CancellationToken cancellationToken)
    {
        if (scope is not null)
        {
            _ = (await fixture.Document.SaveAsync().ConfigureAwait(true)).Succeeded.Should().BeTrue();
        }

        var green = new Vector4(0, 1, 0, 1);
        var source = fixture.Source;
        var revision = fixture.Document.History.Position;
        var history = fixture.Document.History.UndoStack.Count;
        var dirty = fixture.Document.IsDirty;
        var prior = services.Runs.Runs.Select(static run => run.OperationId).ToHashSet();
        SetCookOnChange(services, scope is null);
        await SetPublicationMaterialColorAsync(materials, material, green, cancellationToken).ConfigureAwait(true);
        if (scope is null)
        {
            var run = await WaitForPublicationRunAsync(services, prior, cancellationToken).ConfigureAwait(true);
            _ = run.Request.IsAutomatic.Should().BeTrue();
        }
        else
        {
            var sceneAsset = AssetPath.Create("Content", $"Scenes/{source.Name}.oscene");
            var cooked = await (scope switch
            {
                CookTargetKind.Asset => services.Pipeline.CookAssetAsync(material.File.Asset, cancellationToken),
                CookTargetKind.Folder => services.Pipeline.CookFolderAsync(AssetPath.Parse("/Content/Materials"), cancellationToken),
                CookTargetKind.CurrentScene => services.Pipeline.CookCurrentSceneAsync(sceneAsset, cancellationToken),
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
        _ = fixture.Document.History.Position.Should().Be(revision);
        _ = fixture.Document.History.UndoStack.Should().HaveCount(history);
        _ = fixture.Document.IsDirty.Should().Be(dirty);
        var updated = await feed.ResolveAsync(material.File.Asset, cancellationToken).ConfigureAwait(true);
        _ = updated!.CookStatus!.Freshness.Should().Be(AssetCookFreshness.Current);
        _ = updated.RuntimeAvailability.Should().Be(AssetRuntimeAvailability.Mounted);
    }

    internal async static Task CheckWorkspacePublicationAsync(TestContext testContext, CookTargetKind? scope, Func<PublicationScenario, CancellationToken, Task>? verify = null)
    {
        var fixture = new NativeSceneFixture(automatic: false, scene =>
        {
            AddGeometryNode(scene, "Cube");
            AddGeometryNode(scene, "Sphere");
        });
        await using var lifetime = fixture.ConfigureAwait(true);
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(testContext.CancellationToken);
        timeout.CancelAfter(TimeSpan.FromSeconds(120));
        using var services = new CatalogWorkloadServices(fixture);
        SetCookOnChange(services, enabled: false);
        await using var catalog = new AssetCatalog(services.Projects, services.Storage, services.Builtins, services.Publication);
        using var automatic = new AutomaticCookService(services.Projects, Mock.Of<IProjectManagerService>(), catalog, services.Pipeline, services.Pipeline, services.Runs, NullLogger<AutomaticCookService>.Instance);
        using var runtimeStatus = new AssetRuntimeStatusSource(fixture.Runtime);
        using var provider = new AssetFeed(catalog, services.Projects, services.Scopes, services.Pipeline, services.Documents, services.Runs, runtimeStatus, Mock.Of<IAssetRelocationService>(), Mock.Of<IAssetAuthoring>());
        await fixture.InitializeAsync(timeout.Token).ConfigureAwait(true);
        await using var publication = await fixture.StartWorkspacePreviewAsync(services, catalog).ConfigureAwait(true);
        using var cookInputs = new OpenDocumentCookInputs(fixture.OpenDocuments, services.Documents);
        var materials = new MaterialEdits(new MaterialValidation());
        var material = await CreateMaterialAsync(fixture, "Shared", timeout.Token).ConfigureAwait(true);
        try
        {
            var key = await PrepareSharedPublicationMaterialAsync(fixture, services, provider, materials, material, timeout.Token).ConfigureAwait(true);
            if (verify is null)
            {
                await CheckSharedPublicationRefreshAsync(fixture, services, provider, materials, material, scope, key, timeout.Token).ConfigureAwait(true);
            }
            else
            {
                await verify(new(fixture, services, catalog, provider, materials, material, key), timeout.Token).ConfigureAwait(true);
            }
        }
        finally
        {
            fixture.OpenDocuments.Close(material);
            foreach (var pending in services.Runs.Runs.Where(static run => !run.IsCompleted))
            {
                await services.Runs.CancelAsync(pending.OperationId).ConfigureAwait(true);
            }
        }
    }

    /// <summary>Writes a new material in the fixture's project, as New Material does, and opens it.</summary>
    /// <param name="fixture">The fixture whose project and open documents hold the material.</param>
    /// <param name="name">The material's name.</param>
    /// <param name="cancellationToken">Cancels the write and the open.</param>
    /// <returns>The material's document.</returns>
    internal static async Task<Document> CreateMaterialAsync(NativeSceneFixture fixture, string name, CancellationToken cancellationToken)
    {
        var path = fixture.ProjectRoot.Combine("Content").Combine("Materials").Combine(name + ".omat.json");
        _ = Directory.CreateDirectory(path.Parent!.FullPath);
        await File.WriteAllBytesAsync(path.FullPath, Encoding.UTF8.GetBytes(AssetTemplates.Material(name)), cancellationToken).ConfigureAwait(true);
        var opened = await fixture.OpenDocuments.OpenAsync(new DocumentFile(path, AssetPath.Create("Content", $"Materials/{name}.omat")), cancellationToken).ConfigureAwait(true);
        return opened.Document ?? throw new InvalidOperationException(opened.Problem);
    }

    /// <summary>Turns the scenario's Cook on change setting on or off, as the setting's toggle does.</summary>
    /// <param name="services">The scenario's services.</param>
    /// <param name="enabled">Whether changed sources cook without being asked.</param>
    internal static void SetCookOnChange(CatalogWorkloadServices services, bool enabled)
        => services.Projects.UpdateSettings(services.Projects.Settings with { CookOnChange = enabled });

    /// <summary>Turns Cook on change on and holds the cook writer, so the automatic cook of the next change stays queued.</summary>
    /// <param name="services">The scenario's services.</param>
    /// <param name="cancellationToken">Cancels the hold.</param>
    /// <returns>Releases the writer to the queued cooks; the hold also ends when the project closes.</returns>
    internal static Func<Task> QueueCookOnChange(CatalogWorkloadServices services, CancellationToken cancellationToken)
    {
        SetCookOnChange(services, enabled: true);
        var release = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var held = services.Runs.RunAsync(
            async (_, token) =>
            {
                await release.Task.WaitAsync(token).ConfigureAwait(false);
                return 0;
            },
            cancellationToken);
        return async () =>
        {
            _ = release.TrySetResult();
            _ = await held.ConfigureAwait(true);
        };
    }

    internal sealed record PublicationScenario(NativeSceneFixture SceneAuthoringFixture, CatalogWorkloadServices Services, AssetCatalog Catalog, IAssetFeed Feed, MaterialEdits Materials, Document Material, string Key);
}
