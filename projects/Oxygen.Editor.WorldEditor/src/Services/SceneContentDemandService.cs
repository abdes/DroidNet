// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Documents;
using DroidNet.Hosting.WinUI;
using Microsoft.Extensions.Logging;
using Microsoft.UI;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.World.Messages;
using Oxygen.Editor.World.SceneExplorer.Services;
using Oxygen.Editor.World.Slots;
using Oxygen.Managed.Assets.Catalog;

namespace Oxygen.Editor.World.Services;

/// <summary>Submits saved preview demand and retires its observers as authoring intent changes.</summary>
public sealed partial class SceneContentDemandService : ISceneContentDemandService, IDisposable
{
    private readonly HostingContext hosting;
    private readonly IContentBrowserAssetProvider assets;
    private readonly IContentPipelineService pipeline;
    private readonly IProjectContextService projects;
    private readonly IDocumentService documents;
    private readonly ISceneEngineSync sceneSync;
    private readonly ISceneExplorerService explorer;
    private readonly IMessenger messenger;
    private readonly WindowId windowId;
    private readonly ILogger<SceneContentDemandService> logger;
    private readonly List<Demand> demands = [];
    private Scene? scene;
    private SceneDocumentMetadata? metadata;
    private bool disposed;

    /// <summary>Initializes a new instance of the <see cref="SceneContentDemandService"/> class.</summary>
    /// <param name="hosting">The authoring UI dispatcher.</param>
    /// <param name="assets">Shared typed identities and verified cook status.</param>
    /// <param name="pipeline">The saved-input cooking transaction.</param>
    /// <param name="projects">The current project lifetime.</param>
    /// <param name="documents">Document activation, closure and authoring changes.</param>
    /// <param name="sceneSync">The already loaded scene registry.</param>
    /// <param name="explorer">Structural authoring changes.</param>
    /// <param name="messenger">Scene load and selection notifications.</param>
    /// <param name="windowId">The owning workspace window.</param>
    /// <param name="logger">Reports failures to submit preview work.</param>
    public SceneContentDemandService(
        HostingContext hosting,
        IContentBrowserAssetProvider assets,
        IContentPipelineService pipeline,
        IProjectContextService projects,
        IDocumentService documents,
        ISceneEngineSync sceneSync,
        ISceneExplorerService explorer,
        IMessenger messenger,
        WindowId windowId,
        ILogger<SceneContentDemandService> logger)
    {
        this.hosting = hosting;
        this.assets = assets;
        this.pipeline = pipeline;
        this.projects = projects;
        this.documents = documents;
        this.sceneSync = sceneSync;
        this.explorer = explorer;
        this.messenger = messenger;
        this.windowId = windowId;
        this.logger = logger;
        this.Subscribe();
    }

    /// <inheritdoc />
    public void RequestAssignment(Scene scene, IReadOnlyList<Guid> nodeIds, Uri? assetUri, AssetKind kind)
    {
        ArgumentNullException.ThrowIfNull(scene);
        ArgumentNullException.ThrowIfNull(nodeIds);
        if (assetUri is null || nodeIds.Count == 0)
        {
            return;
        }

        var targets = nodeIds.ToHashSet();
        this.Dispatch(() =>
        {
            if (ReferenceEquals(this.scene, scene))
            {
                this.StartDemand(assetUri, kind, targets);
            }
        });
    }

    /// <inheritdoc />
    public void Dispose()
    {
        this.Unsubscribe();
        this.Dispatch(() =>
        {
            this.Reset();
            this.disposed = true;
        });
        GC.SuppressFinalize(this);
    }

    private static IEnumerable<(Guid nodeId, Uri uri, AssetKind kind)> References(Scene scene)
    {
        foreach (var node in scene.AllNodes)
        {
            foreach (var geometry in node.Components.OfType<GeometryComponent>())
            {
                if (geometry.Geometry?.Uri is { } geometryUri)
                {
                    yield return (node.Id, geometryUri, AssetKind.Geometry);
                }

                foreach (var slot in geometry.OverrideSlots.OfType<MaterialsSlot>())
                {
                    yield return (node.Id, slot.Material.Uri, AssetKind.Material);
                }
            }
        }
    }

    private void StartDemand(Uri uri, AssetKind kind, HashSet<Guid>? targets)
    {
        if (this.scene is not { } current || this.projects.ActiveProject is not { } project
            || current.Project.ProjectInfo?.Id != project.ProjectId
            || string.Equals(AssetUriHelper.GetMountPoint(uri), "Engine", StringComparison.OrdinalIgnoreCase)
            || string.Equals(uri.AbsolutePath, "/__uninitialized__", StringComparison.Ordinal))
        {
            return;
        }

        var demand = new Demand(current, project, uri, kind, targets);
        if (!this.IsCurrent(demand))
        {
            demand.Cancellation.Dispose();
            return;
        }

        this.demands.Add(demand);
        _ = this.CookDemandAsync(demand);
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "This owns asynchronous preview requests after the authoring action has returned. Cooking records operation failures; submission failures must be observed without failing the accepted edit.")]
    private async Task CookDemandAsync(Demand demand)
    {
        try
        {
            var asset = await this.assets.ResolveAsync(demand.Uri, demand.Cancellation.Token).ConfigureAwait(true);
            demand.Cancellation.Token.ThrowIfCancellationRequested();
            if (!this.IsCurrent(demand) || asset?.CanCook != true || !asset.IsSelectable
                || asset.Kind != demand.Kind || asset.CookStatus?.Freshness is not (AssetCookFreshness.NeedsCooking or AssetCookFreshness.OutOfDate))
            {
                return;
            }

            _ = await this.pipeline.CookPreviewAssetAsync(asset.IdentityUri, demand.Project, demand.Cancellation.Token).ConfigureAwait(true);
        }
        catch (OperationCanceledException)
        {
            // This observer no longer owns preview demand. Shared work retains its other owners.
        }
        catch (Exception exception)
        {
            this.LogDemandFailure(exception, demand.Uri);
        }
        finally
        {
            _ = this.demands.Remove(demand);
            try
            {
                if (demand.CancellationCompletion is { } cancellation)
                {
                    await cancellation.ConfigureAwait(true);
                }
            }
            catch (Exception exception)
            {
                this.LogDemandFailure(exception, demand.Uri);
            }
            finally
            {
                demand.Cancellation.Dispose();
            }
        }
    }

    private bool IsCurrent(Demand demand)
    {
        if (this.disposed || !ReferenceEquals(this.scene, demand.Scene) || !ReferenceEquals(this.projects.ActiveProject, demand.Project)
            || this.documents.GetActiveDocumentId(this.windowId) != demand.Scene.Id
            || !this.documents.GetOpenDocuments(this.windowId).Any(document => ReferenceEquals(document, this.metadata)))
        {
            return false;
        }

        var matches = References(demand.Scene).Where(reference => reference.kind == demand.Kind && reference.uri == demand.Uri)
            .Select(static reference => reference.nodeId).ToHashSet();
        if (demand.Targets is not { } targets)
        {
            return matches.Count != 0;
        }

        var selection = this.messenger.Send(new SceneNodeSelectionRequestMessage());
        return selection.HasReceivedResponse && targets.IsSubsetOf(matches)
            && targets.SetEquals(selection.SelectedEntities.Where(node => ReferenceEquals(node.Scene, demand.Scene)).Select(static node => node.Id));
    }

    [LoggerMessage(Level = LogLevel.Error, Message = "Unable to prepare preview content for {AssetUri}.")]
    private partial void LogDemandFailure(Exception exception, Uri assetUri);

    private sealed class Demand(Scene scene, ProjectContext project, Uri uri, AssetKind kind, HashSet<Guid>? targets)
    {
        public Scene Scene { get; } = scene;

        public ProjectContext Project { get; } = project;

        public Uri Uri { get; } = uri;

        public AssetKind Kind { get; } = kind;

        public HashSet<Guid>? Targets { get; } = targets;

        public CancellationTokenSource Cancellation { get; } = new();

        public Task? CancellationCompletion { get; set; }

        public void Cancel() => this.CancellationCompletion ??= this.Cancellation.CancelAsync();
    }
}
