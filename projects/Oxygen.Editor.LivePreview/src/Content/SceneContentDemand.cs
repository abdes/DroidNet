// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Documents;
using Oxygen.Editor.Scenes.Edits.Assets;
using Oxygen.Editor.World;
using Oxygen.Managed.Assets.Catalog;

namespace Oxygen.Editor.LivePreview.Content;

/// <summary>Cooks for preview the saved assets the open scene documents reference and the engine cannot show yet.</summary>
/// <remarks>
/// A document demands every asset it references when it opens and when it reloads; after that it follows its scene's
/// change stream, demanding the assets a change starts referencing and cancelling the demand for the ones the scene
/// stops referencing. Closing the document cancels its demands. Nothing is demanded from the selection: an asset stays
/// demanded while the scene references it, whatever is selected.
/// </remarks>
internal sealed partial class SceneContentDemand : IDisposable
{
    private readonly OpenDocuments documents;
    private readonly IAssetFeed assets;
    private readonly IContentPipelineService pipeline;
    private readonly IProjectContextService projects;
    private readonly ILogger logger;
    private readonly List<FollowedDocument> followed = [];
    private bool disposed;

    /// <summary>Initializes a new instance of the <see cref="SceneContentDemand"/> class, following the open documents.</summary>
    /// <param name="documents">The workspace's open documents.</param>
    /// <param name="assets">Resolves each asset's identity and cook state.</param>
    /// <param name="pipeline">Cooks an asset for preview.</param>
    /// <param name="projects">The active project.</param>
    /// <param name="loggerFactory">The optional logger factory.</param>
    public SceneContentDemand(OpenDocuments documents, IAssetFeed assets, IContentPipelineService pipeline, IProjectContextService projects, ILoggerFactory? loggerFactory = null)
    {
        this.documents = documents ?? throw new ArgumentNullException(nameof(documents));
        this.assets = assets ?? throw new ArgumentNullException(nameof(assets));
        this.pipeline = pipeline ?? throw new ArgumentNullException(nameof(pipeline));
        this.projects = projects ?? throw new ArgumentNullException(nameof(projects));
        this.logger = loggerFactory?.CreateLogger<SceneContentDemand>() ?? NullLogger<SceneContentDemand>.Instance;
        this.documents.Opened += this.OnDocumentOpened;
        this.documents.Closed += this.OnDocumentClosed;
        foreach (var document in this.documents.Documents.ToArray())
        {
            this.OnDocumentOpened(this.documents, new DocumentEventArgs(document));
        }
    }

    /// <inheritdoc/>
    public void Dispose()
    {
        if (this.disposed)
        {
            return;
        }

        this.disposed = true;
        this.documents.Opened -= this.OnDocumentOpened;
        this.documents.Closed -= this.OnDocumentClosed;
        foreach (var entry in this.followed.ToArray())
        {
            this.StopFollowing(entry);
        }
    }

    private static bool IsDemandable(Uri uri)
        => !string.Equals(AssetUriHelper.GetMountPoint(uri), "Engine", StringComparison.Ordinal);

    private static HashSet<Uri> ReferencesOf(Scene scene)
        => [.. scene.AllNodes.SelectMany(SceneAssetReferences.Runtime).Where(IsDemandable)];

    private static bool ChangesReferences(SceneChange change)
        => change is SceneChange.NodesAdded or SceneChange.NodesRemoved or SceneChange.ComponentAdded
            or SceneChange.ComponentRemoved or SceneChange.GeometryChanged or SceneChange.MaterialSlotsChanged;

    private void OnDocumentOpened(object? sender, DocumentEventArgs args)
    {
        if (this.disposed || args.Document.Content is not SceneContent || this.followed.Exists(entry => ReferenceEquals(entry.Document, args.Document)))
        {
            return;
        }

        var entry = new FollowedDocument(args.Document);
        entry.OnContentReplaced = (_, _) => this.Observe(entry, demandAll: true);
        entry.OnChanged = (_, changes) =>
        {
            if (changes.Changes.Any(ChangesReferences))
            {
                this.Refresh(entry, demandAll: false);
            }
        };
        args.Document.ContentReplaced += entry.OnContentReplaced;
        this.followed.Add(entry);
        this.Observe(entry, demandAll: true);
    }

    private void OnDocumentClosed(object? sender, DocumentEventArgs args)
    {
        if (this.followed.Find(entry => ReferenceEquals(entry.Document, args.Document)) is { } entry)
        {
            this.StopFollowing(entry);
        }
    }

    private void StopFollowing(FollowedDocument entry)
    {
        _ = this.followed.Remove(entry);
        entry.Document.ContentReplaced -= entry.OnContentReplaced;
        if (entry.Content is { } content)
        {
            content.Authoring.Changed -= entry.OnChanged;
            entry.Content = null;
        }

        foreach (var demand in entry.Demands.Values.ToArray())
        {
            demand.Cancel();
        }

        entry.Referenced.Clear();
    }

    // Opening and reloading start from the content as it is: every reference is demanded again.
    private void Observe(FollowedDocument entry, bool demandAll)
    {
        if (entry.Content is { } previous)
        {
            previous.Authoring.Changed -= entry.OnChanged;
        }

        var content = SceneContent.Of(entry.Document);
        entry.Content = content;
        content.Authoring.Changed += entry.OnChanged;
        this.Refresh(entry, demandAll);
    }

    private void Refresh(FollowedDocument entry, bool demandAll)
    {
        if (this.disposed || entry.Content?.Authoring is not { } authoring)
        {
            return;
        }

        var now = ReferencesOf(authoring.Scene);
        foreach (var uri in entry.Referenced.Where(uri => !now.Contains(uri)).ToArray())
        {
            if (entry.Demands.TryGetValue(uri, out var stale))
            {
                this.LogDemandCancelled(uri);
                stale.Cancel();
            }
        }

        foreach (var uri in now.Where(uri => demandAll || !entry.Referenced.Contains(uri)))
        {
            this.StartDemand(entry, uri);
        }

        entry.Referenced = now;
    }

    // A demand still cooking is kept; one cancelled while its cook unwinds, as by an undo then a redo, is replaced.
    private void StartDemand(FollowedDocument entry, Uri uri)
    {
        if ((entry.Demands.TryGetValue(uri, out var existing) && !existing.IsCancelled) || this.projects.ActiveProject is not { } project
            || entry.Content?.Project.ProjectId != project.ProjectId)
        {
            return;
        }

        var demand = new Demand(uri, project);
        entry.Demands[uri] = demand;
        this.LogDemandStarted(uri);
        _ = this.CookDemandAsync(entry, demand);
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "Preview demand runs after the edit returned; a failure to submit it is logged without failing the edit. Cooking reports its own failures.")]
    private async Task CookDemandAsync(FollowedDocument entry, Demand demand)
    {
        try
        {
            var asset = await this.assets.ResolveAsync(demand.Uri, demand.Cancellation.Token).ConfigureAwait(true);
            demand.Cancellation.Token.ThrowIfCancellationRequested();
            if (asset?.CanCook != true || !asset.IsSelectable || !ReferenceEquals(this.projects.ActiveProject, demand.Project)
                || asset.CookStatus?.Freshness is not (AssetCookFreshness.NeedsCooking or AssetCookFreshness.OutOfDate))
            {
                return;
            }

            _ = await this.pipeline.CookPreviewAssetAsync(asset.IdentityUri, demand.Project, demand.Cancellation.Token).ConfigureAwait(true);
        }
        catch (OperationCanceledException)
        {
            // The scene stopped referencing the asset; shared cook work keeps its other owners.
        }
        catch (Exception exception)
        {
            this.LogDemandFailure(exception, demand.Uri);
        }
        finally
        {
            if (entry.Demands.TryGetValue(demand.Uri, out var current) && ReferenceEquals(current, demand))
            {
                _ = entry.Demands.Remove(demand.Uri);
            }

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

    private sealed class FollowedDocument(Document document)
    {
        public Document Document { get; } = document;

        public SceneContent? Content { get; set; }

        public HashSet<Uri> Referenced { get; set; } = [];

        public Dictionary<Uri, Demand> Demands { get; } = [];

        public EventHandler<DocumentContentReplacedEventArgs>? OnContentReplaced { get; set; }

        public EventHandler<SceneChangedEventArgs>? OnChanged { get; set; }
    }

    private sealed class Demand(Uri uri, ProjectContext project)
    {
        public Uri Uri { get; } = uri;

        public ProjectContext Project { get; } = project;

        public CancellationTokenSource Cancellation { get; } = new();

        public Task? CancellationCompletion { get; set; }

        public bool IsCancelled => this.CancellationCompletion is not null;

        public void Cancel() => this.CancellationCompletion ??= this.Cancellation.CancelAsync();
    }
}
