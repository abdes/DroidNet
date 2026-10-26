// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Specialized;
using System.ComponentModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Controls;
using DroidNet.Controls.Selection;
using DroidNet.Documents;
using DroidNet.Routing;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Xaml;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Scenes;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Documents;
using Oxygen.Editor.Scenes.Edits.EditorView;
using Oxygen.Editor.Scenes.Edits.Hierarchy;
using Oxygen.Editor.Scenes.Edits.Layout;
using Oxygen.Editor.Scenes.Interaction;
using Oxygen.Editor.Scenes.Layout;
using Oxygen.Editor.Scenes.Nodes;
using Oxygen.Editor.Scenes.Selection;
using Oxygen.Editor.Scenes.Views;
using Oxygen.Editor.Shell.Commands;
using Oxygen.Editor.Shell.Documents;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.SceneExplorer;

/// <summary>
///     The ViewModel for the <see cref="SceneExplorer.SceneExplorerView" /> view.
/// </summary>
[RetainedViewModel]
public partial class SceneExplorerViewModel : DynamicTreeViewModel
{
    // The selection source of viewport picks; their rows scroll into view.
    private const string ViewportSelectionSource = "Viewport";

    // The selection source of the Inspector's "inspect this node" requests; the row is revealed and focused.
    private const string InspectorSelectionSource = "Inspector";

    private readonly ILogger<SceneExplorerViewModel> logger;
    private readonly IRouter router;
    private readonly IDocumentArea documentArea;
    private readonly OpenDocuments openDocuments;
    private readonly ISceneSelectionService selectionService;
    private readonly IOperationResultPublisher? operationResults;
    private readonly IStatusReducer? statusReducer;
    private readonly SceneNodeInteraction? interaction;
    private readonly IProjectSession? session;
    private readonly Oxygen.Editor.ContentPipeline.Relocation.IAssetRelocationService? relocation;
    private readonly Oxygen.Editor.Shell.Documents.IHistoryService? histories;
    private readonly Oxygen.Editor.Shell.Windows.IPlacementService? placement;
    private readonly ISceneViews? views;
    private readonly ISceneAvailability? availability;
    private readonly ISceneStructureProvider? structures;
    private readonly IDisposable? commandRegistration;
    private readonly SceneExplorerProjection projection = new();
    private readonly SemaphoreSlim searchGate = new(1, 1);
    private readonly System.Threading.Lock searchLifetimeLock = new();

    // The session's clipboard of scene nodes; the tree keeps only its rows' clipboard marks.
    private readonly ISceneNodeClipboard clipboard;

    // Transient search expansions recorded by identity: folders/nodes expanded for a match,
    // restored when search clears, and re-applied as transient across a projection rebuild.
    private readonly HashSet<FolderId> searchExpandedFolderIds = [];
    private readonly HashSet<NodeId> searchExpandedNodeIds = [];

    private int nextEntityIndex;
    private int searchGeneration;
    private int activeSearchOperations;
    private bool searchLifetimeStopped;
    private bool searchGateDisposed;
    private CancellationTokenSource? loadSceneCts;
    private DocumentId loadingDocumentId = DocumentId.Empty;

    // The scene document the explorer shows: its scene, its history and its tab.
    private Document? sceneDocument;
    private bool suppressNodeMessages;
    private SceneAuthoring? observedAuthoring;

    // Set while the Explorer itself writes selection or applies a foreign selection, so the
    // service echo never re-enters the row-sync path.
    private bool suppressSelectionSync;
    private bool isApplyingCategoryFilters;

    // The authoritative context received while applying a foreign selection: publishing forwards
    // it verbatim so identities whose rows are not realized cannot silently shrink the selection.
    private SceneSelectionContext? pendingAuthoritativeContext;

    // Increments per selection application so an await-crossed application can detect that a
    // newer request, document switch or store write has superseded it.
    private int selectionApplyGeneration;

    // Depth counter for projection-sync windows: while a tree reload/refill clears selection
    // outside any batch, the settled publishes must not run — they would overwrite the
    // authoritative context the rebuild is about to restore.
    private int selectionSyncDepth;

    // The exact context instance last published, so an identical re-application never re-notifies
    // consumers while a genuinely changed context notifies even when the visible rows match.
    private SceneSelectionContext? lastPublishedContext;

    private bool isDisposed;

    // No cached selection - capture selection at command time to avoid stale state.

    /// <summary>
    ///     Initializes a new instance of the <see cref="SceneExplorerViewModel" /> class.
    /// </summary>
    /// <param name="router">The router service used for navigation events.</param>
    /// <param name="documentArea">The window's document area, whose active scene document the explorer shows.</param>
    /// <param name="openDocuments">The open documents: the explorer shows the scene document opened last, and follows its reloads.</param>
    /// <param name="selectionService">The document selection service.</param>
    /// <param name="loggerFactory">
    ///     Optional factory for creating loggers. If provided, enables detailed logging of the
    ///     recognition process. If <see langword="null" />, logging is disabled.
    /// </param>
    /// <param name="operationResults">
    ///     Optional operation-result publisher. When present, a clipboard payload that the lifetime
    ///     rule discards is reported to the user instead of only disappearing.
    /// </param>
    /// <param name="statusReducer">Optional status reducer paired with <paramref name="operationResults"/>.</param>
    /// <param name="interaction">
    ///     Optional workspace interaction state owner ("Show in Editor" and Lock). Without it the
    ///     eye and lock actions report that the workspace service is unavailable rather than
    ///     silently doing nothing.
    /// </param>
    /// <param name="session">Optional project session used to scope project-owned Explorer state.</param>
    /// <param name="relocation">Optional relocation service that renames the scene's file.</param>
    /// <param name="histories">Optional history service whose global history records a scene file rename.</param>
    /// <param name="placement">Optional placement, which shows a scene document that becomes active in the window's place for its scene.</param>
    /// <param name="views">Optional scene views, which frame nodes and run camera commands in the scene's editor.</param>
    /// <param name="availability">Optional scene availability, which reports a scene that failed to load and reloads the previous one.</param>
    /// <param name="commands">Optional command service, with which the explorer registers as a command handler.</param>
    /// <param name="clipboard">
    ///     Optional clipboard of the session's scene nodes, which copies and cuts stage and pastes read. Without it the
    ///     explorer keeps a clipboard of its own.
    /// </param>
    /// <param name="structures">
    ///     Optional reader of what an instance's source contains, whose nodes the explorer shows under each instance
    ///     root. Without it an instance root shows no instanced rows.
    /// </param>
    public SceneExplorerViewModel(
        IRouter router,
        IDocumentArea documentArea,
        OpenDocuments openDocuments,
        ISceneSelectionService selectionService,
        ILoggerFactory? loggerFactory = null,
        IOperationResultPublisher? operationResults = null,
        IStatusReducer? statusReducer = null,
        SceneNodeInteraction? interaction = null,
        IProjectSession? session = null,
        Oxygen.Editor.ContentPipeline.Relocation.IAssetRelocationService? relocation = null,
        Oxygen.Editor.Shell.Documents.IHistoryService? histories = null,
        Oxygen.Editor.Shell.Windows.IPlacementService? placement = null,
        ISceneViews? views = null,
        ISceneAvailability? availability = null,
        ICommandService? commands = null,
        ISceneNodeClipboard? clipboard = null,
        ISceneStructureProvider? structures = null)
        : base(loggerFactory)
    {
        this.clipboard = clipboard ?? new SceneNodeClipboard();
        this.structures = structures;
        structures?.Changed += this.OnStructuresChanged;
        this.views = views;
        this.availability = availability;
        this.relocation = relocation;
        this.histories = histories;
        this.placement = placement;
        this.operationResults = operationResults;
        this.statusReducer = statusReducer;
        this.interaction = interaction;
        this.session = session;
        if (interaction is { } service)
        {
            this.ApplyCategoryFilters(service.Categories);
            service.StateChanged += this.OnInteractionStateChanged;
        }

        this.logger = loggerFactory?.CreateLogger<SceneExplorerViewModel>() ?? NullLogger<SceneExplorerViewModel>.Instance;
        this.router = router;
        this.documentArea = documentArea;
        this.openDocuments = openDocuments;
        this.selectionService = selectionService;
        this.selectionService.SelectionChanged += this.OnSelectionServiceChanged;
        this.SelectionSettled += this.OnTreeSelectionSettled;
        this.ItemInvoked += this.OnTreeItemInvoked;
        this.PropertyChanged += this.OnContextOwnerPropertyChanged;
        this.ClipboardContentChanged += (_, _) => this.RefreshContextActions();

        // Default selection mode for Scene Explorer is multiple selection.
        this.SelectionMode = SelectionMode.Multiple;

        // The explorer shows the window's active scene document.
        documentArea.ActiveDocumentChanged += this.OnActiveDocumentChanged;
        this.RegisterSceneLifetime();
        this.commandRegistration = commands?.Register(this);
    }

    /// <summary>
    /// Fired when the ViewModel requests a rename operation in the View.
    /// </summary>
    internal event EventHandler<RenameRequestedEventArgs?>? RenameRequested;

    /// <summary>Fired when the ViewModel asks the View to focus its search box (Ctrl+F).</summary>
    internal event EventHandler? FindRequested;

    /// <summary>
    ///     Gets or sets a value indicating whether there are unlocked items in the current selection.
    /// </summary>
    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(SceneExplorerViewModel.RemoveSelectedItemsCommand))]
    public partial bool HasUnlockedSelectedItems { get; set; }

    /// <summary>Gets or sets the transient search text shown by the Explorer.</summary>
    [ObservableProperty]
    public partial string SearchText { get; set; } = string.Empty;

    /// <summary>Gets or sets the number of matching scene nodes and folders.</summary>
    [ObservableProperty]
    public partial int SearchResultCount { get; set; }

    /// <summary>Gets a value indicating whether a non-empty search query is active.</summary>
    public bool HasSearchQuery => !string.IsNullOrWhiteSpace(this.SearchText);

    /// <summary>Gets the accessible search result summary.</summary>
    public string SearchResultText => this.SearchResultCount == 0
        ? "No matches"
        : string.Create(System.Globalization.CultureInfo.CurrentCulture, $"{this.SearchResultCount} {(this.SearchResultCount == 1 ? "match" : "matches")}");

    /// <summary>Gets or sets whether mesh-category rows are shown in the Scene Explorer.</summary>
    [ObservableProperty]
    public partial bool ShowMeshesInExplorer { get; set; } = true;

    /// <summary>Gets or sets whether light-category rows are shown in the Scene Explorer.</summary>
    [ObservableProperty]
    public partial bool ShowLightsInExplorer { get; set; } = true;

    /// <summary>Gets or sets whether camera-category rows are shown in the Scene Explorer.</summary>
    [ObservableProperty]
    public partial bool ShowCamerasInExplorer { get; set; } = true;

    /// <summary>Gets a value indicating whether the active scene contains a mesh-category node.</summary>
    public bool HasMeshCategory => this.Scene?.AttachedObject.AllNodes
        .Any(static node => node.Components.OfType<GeometryComponent>().Any()) == true;

    /// <summary>Gets a value indicating whether the active scene contains a light-category node.</summary>
    public bool HasLightCategory => this.Scene?.AttachedObject.AllNodes
        .Any(static node => node.Components.Any(static component =>
            component is DirectionalLightComponent or PointLightComponent or SpotLightComponent)) == true;

    /// <summary>Gets a value indicating whether the active scene contains a camera-category node.</summary>
    public bool HasCameraCategory => this.Scene?.AttachedObject.AllNodes
        .Any(static node => node.Components.Any(static component =>
            component is PerspectiveCamera or OrthographicCamera)) == true;

    /// <summary>Gets a value indicating whether any filterable category is represented in the scene.</summary>
    public bool HasAnyCategory => this.HasMeshCategory || this.HasLightCategory || this.HasCameraCategory;

    /// <summary>Gets the mesh column width, or zero when the scene has no mesh-category rows.</summary>
    public GridLength MeshCategoryColumnWidth => this.HasMeshCategory ? new(1, GridUnitType.Star) : new(0);

    /// <summary>Gets the light column width, or zero when the scene has no light-category rows.</summary>
    public GridLength LightCategoryColumnWidth => this.HasLightCategory ? new(1, GridUnitType.Star) : new(0);

    /// <summary>Gets the camera column width, or zero when the scene has no camera-category rows.</summary>
    public GridLength CameraCategoryColumnWidth => this.HasCameraCategory ? new(1, GridUnitType.Star) : new(0);

    /// <summary>Gets a value indicating whether category filters have a loaded workspace owner.</summary>
    public bool CanChangeCategories => this.interaction is not null && this.Scene is not null;

    /// <summary>
    ///     Gets the current scene adapter.
    /// </summary>
    public SceneAdapter? Scene { get; private set; }

    // Selection, edit requests and frame requests name the scene's document, not the scene.
    private DocumentId SceneDocumentId => this.sceneDocument?.Id ?? DocumentId.Empty;

    /// <summary>Applies a transient name search, revealing collapsed matches by expanding their ancestor paths.</summary>
    /// <param name="query">The search text; empty or whitespace clears the search.</param>
    /// <returns>The number of matching nodes (excluding context-only ancestors) plus the folder matches from the authored layout domain.</returns>
    public async Task<int> SearchAsync(string? query)
    {
        if (!this.TryBeginSearchOperation())
        {
            return 0;
        }

        var gateAcquired = false;
        try
        {
            var generation = Interlocked.Increment(ref this.searchGeneration);
            this.SearchText = query ?? string.Empty;
            await this.searchGate.WaitAsync().ConfigureAwait(true);
            gateAcquired = true;

            return await this.SearchCoreAsync(query ?? string.Empty, generation).ConfigureAwait(true);
        }
        finally
        {
            if (gateAcquired)
            {
                this.searchGate.Release();
            }

            this.EndSearchOperation();
        }
    }

    /// <summary>Clears the transient search and restores the expansion state it changed.</summary>
    /// <returns>A task that completes when the search has been cleared.</returns>
    public async Task ClearSearchAsync()
    {
        if (!this.TryBeginSearchOperation())
        {
            return;
        }

        var gateAcquired = false;
        try
        {
            _ = Interlocked.Increment(ref this.searchGeneration);
            this.SearchText = string.Empty;
            this.SearchResultCount = 0;
            await this.searchGate.WaitAsync().ConfigureAwait(true);
            gateAcquired = true;
            if (!this.isDisposed)
            {
                await this.ClearSearchCoreAsync().ConfigureAwait(true);
            }
        }
        finally
        {
            if (gateAcquired)
            {
                this.searchGate.Release();
            }

            this.EndSearchOperation();
        }
    }

    /// <inheritdoc />
    [RelayCommand(CanExecute = nameof(CanRemoveContextItems))]
    public override async Task RemoveSelectedItems()
    {
        await this.ExecuteContextActionAsync(
            SceneExplorerCommandKind.Delete,
            this.CaptureExplorerContext(anchor: null, background: false)).ConfigureAwait(true);
    }

    /// <inheritdoc />
    public override async Task<TreeItemRenameResult> CommitRenameAsync(ITreeItem item, string newName)
    {
        ArgumentNullException.ThrowIfNull(item);

        var authoring = this.CurrentAuthoring();
        if (authoring is null || !this.IsCurrentContextRow(item) || this.FindLockedContextRow(item) is not null)
        {
            return TreeItemRenameResult.Rejected("The row is no longer editable in the loaded scene document.");
        }

        var trimmed = (newName ?? string.Empty).Trim();
        if (!item.ValidateItemName(trimmed))
        {
            return TreeItemRenameResult.Rejected("The name is not valid.");
        }

        if (string.Equals(item.Label, trimmed, StringComparison.Ordinal))
        {
            return TreeItemRenameResult.Success;
        }

        return item switch
        {
            SceneAdapter => await this.RenameSceneAssetAsync(trimmed).ConfigureAwait(true),
            SceneNodeAdapter node => Renamed(await authoring.ExecuteAsync(new RenameNode(node.AttachedObject.Id, trimmed)).ConfigureAwait(true)),
            InstancedNodeAdapter instanced when InstanceRules.RefusesStructure(authoring.Scene, [instanced.Reference], _ => instanced.Name) is { } refusal
                => TreeItemRenameResult.Rejected(refusal.Message),
            FolderAdapter folder => await RenameFolderAsync(authoring, folder, trimmed).ConfigureAwait(true),
            _ => TreeItemRenameResult.Rejected("The rename was rejected."),
        };
    }

    /// <summary>
    /// Selects scene nodes by their stable identities: the authoritative context is stored with
    /// every id the caller named (even ones whose rows are not realized yet), and the rows reveal
    /// and select whatever resolves. Nothing is published when the rows already carry the
    /// selection, so an unresolvable batch can never shrink the domain selection.
    /// </summary>
    /// <param name="nodeIds">The node identities to select, in stable order; the last is primary.</param>
    /// <param name="focusPrimary">Whether the Explorer should take keyboard focus after selection.</param>
    /// <returns>
    /// A task whose result is <see langword="true" /> when every named identity resolved to a
    /// selected row (an empty batch is trivially satisfied); <see langword="false" /> when some
    /// ids remain unrealized or the store changed under the application. The authoritative
    /// context keeps all named ids either way.
    /// </returns>
    public async Task<bool> SetSelectedNodes(IReadOnlyList<NodeId> nodeIds, bool focusPrimary = false)
    {
        ArgumentNullException.ThrowIfNull(nodeIds);
        if (this.Scene is not { } sceneAdapter)
        {
            return false;
        }

        var context = SceneSelectionContext.ForNodes(nodeIds.Select(NodeReference.To));

        this.suppressSelectionSync = true;
        try
        {
            this.selectionService.Publish(this.SceneDocumentId, context, "SceneExplorer");
        }
        finally
        {
            this.suppressSelectionSync = false;
        }

        return await this.ApplySelectionContextAsync(context, focusPrimary: focusPrimary).ConfigureAwait(true);
    }

    /// <summary>
    ///     Returns the <see cref="TreeItemAdapter"/> associated with the given scene node <paramref name="nodeId"/>,
    ///     or <see langword="null"/> if no adapter is registered for that id.
    /// </summary>
    /// <param name="nodeId">The id of the scene node to look up.</param>
    /// <returns>A <see cref="Task"/> that returns the adapter or <see langword="null"/>.</returns>
    public Task<TreeItemAdapter?> FindAdapterByNodeIdAsync(NodeId nodeId)
        => Task.FromResult<TreeItemAdapter?>(this.projection.GetNode(nodeId));

    /// <summary>Selects scene-level properties for an explicit diagnostic navigation.</summary>
    /// <param name="sceneId">The scene whose stored node selection is cleared, when the explorer shows it.</param>
    public void SelectEnvironment(SceneId sceneId)
    {
        if (this.Scene?.AttachedObject.Id == sceneId)
        {
            this.selectionService.Clear(this.SceneDocumentId);
            this.WithSelectionBatch(() =>
            {
                this.SetActiveItem(item: null);
                this.SelectionModel?.ClearSelection();
            });
        }
    }

    /// <inheritdoc />
    public override Task CopyItemsAsync(IReadOnlyList<ITreeItem> items)
        => this.StageExplorerClipboardAsync(items, cut: false);

    /// <inheritdoc />
    public override Task CutItemsAsync(IReadOnlyList<ITreeItem> items)
        => items.Any(item => this.FindLockedContextRow(item) is not null)
            ? Task.CompletedTask
            : this.StageExplorerClipboardAsync(items, cut: true);

    /// <inheritdoc />
    public override async Task PasteItemsAsync(ITreeItem? targetParent = null, int? insertIndex = null)
    {
        _ = insertIndex;
        if (!this.ClipboardLifetimeIsCurrent())
        {
            this.InvalidateClipboard("The cut was cleared because its source scene is no longer loaded.");
            return;
        }

        await this.ExecuteContextActionAsync(SceneExplorerCommandKind.Paste,
            this.CaptureExplorerContext(targetParent, background: false)).ConfigureAwait(true);
    }

    /// <inheritdoc />
    public override async Task<TreeDropResult> CommitDropAsync(TreeDropRequest request)
    {
        ArgumentNullException.ThrowIfNull(request);

        if (request.Items.Count == 0)
        {
            return TreeDropResult.Rejected;
        }

        if (this.CurrentAuthoring() is not { } authoring)
        {
            return TreeDropResult.Rejected;
        }

        // Nodes an instance brought in are neither moved nor given children (scene instances 6.2).
        var instanced = request.Items.Append(request.Parent).OfType<InstancedNodeAdapter>().ToList();
        if (InstanceRules.RefusesStructure(
                authoring.Scene,
                instanced.Select(static row => row.Reference),
                reference => instanced.Find(row => row.Reference == reference)?.Name) is { } refusal)
        {
            this.ReportInstancedStructureRefused(refusal);
            return TreeDropResult.Rejected;
        }

        var folderIds = request.Items.OfType<FolderAdapter>().Select(folder => folder.Id).ToList();
        var nodeIds = request.Items.OfType<SceneNodeAdapter>().Select(adapter => adapter.AttachedObject.Id).ToList();
        if (this.CreateDropEdit(request, authoring, nodeIds, folderIds) is not { } edit)
        {
            return TreeDropResult.Rejected;
        }

        this.suppressNodeMessages = true;
        try
        {
            if (!(await authoring.ExecuteAsync(edit).ConfigureAwait(true)).Succeeded)
            {
                return TreeDropResult.Rejected;
            }

            await this.ReconcileProjectionAsync().ConfigureAwait(true);
        }
        finally
        {
            this.suppressNodeMessages = false;
        }

        if (request.Operation == TreeDropOperation.Copy && edit is SceneEditBatch batch)
        {
            nodeIds = [.. batch.Edits.OfType<InsertNodes>().SelectMany(static insert => insert.Inserts).Select(static insert => insert.Node.Id)];
            folderIds = [.. batch.Edits.OfType<CreateFolder>().Select(static folder => folder.Folder)];
        }

        var moved = nodeIds
            .Where(this.projection.ContainsNode)
            .Select(id => (ITreeItem)this.projection.GetNode(id)!)
            .Concat(folderIds.Where(this.projection.ContainsFolder).Select(id => (ITreeItem)this.projection.GetFolder(id)!))
            .ToList();
        return TreeDropResult.Committed(moved);
    }

    /// <summary>Returns the folder adapter for the given folder identity, or <see langword="null"/>.</summary>
    /// <param name="folderId">The folder to look up.</param>
    /// <returns>A task completing with the adapter, or <see langword="null"/> when not realized.</returns>
    public Task<FolderAdapter?> FindFolderAdapterAsync(FolderId folderId)
        => Task.FromResult(this.projection.GetFolder(folderId));

    internal static SceneSelectionContext BuildSelectionContext(IReadOnlyList<ITreeItem> items, ITreeItem? activeItem = null)
    {
        if (items.Count == 0)
        {
            return SceneSelectionContext.Empty;
        }

        var hasScene = items.Any(static item => item is SceneAdapter);
        var hasFolder = items.Any(static item => item is FolderAdapter);
        var hasNode = items.Any(static item => item is NodeRowAdapter);
        var kind = SceneSelectionContext.Classify(hasScene, hasFolder, hasNode);

        var nodes = items.OfType<NodeRowAdapter>().Select(static row => row.Reference).ToList();
        var folderIds = items.OfType<FolderAdapter>().Select(folder => folder.Id).ToList();

        // The explicit active row is the primary identity; selection order is only the fallback
        // when no interactive selection established an active row (programmatic restores).
        var primary = activeItem is not null && items.Contains(activeItem) ? activeItem : items[^1];

        return new SceneSelectionContext(
            kind,
            nodes,
            folderIds,
            primary is NodeRowAdapter primaryNode ? primaryNode.Reference : null,
            primary is FolderAdapter primaryFolder ? primaryFolder.Id : null);
    }

    /// <summary>Reports a search failure caught at the asynchronous view event boundary.</summary>
    /// <param name="exception">The search exception.</param>
    /// <param name="query">The query that failed.</param>
    internal void ReportSearchFailure(Exception exception, string query)
        => this.LogSearchFailed(exception, query);

    /// <summary>
    /// Handles document-open actions once the target scene has been resolved.
    /// Kept protected for testability; does no UI-thread dispatching.
    /// </summary>
    /// <param name="scene">Scene to load.</param>
    /// <returns>Task that completes after the scene has been loaded.</returns>
    protected internal virtual async Task HandleDocumentOpenedAsync(Scene scene)
        => _ = await this.LoadSceneAsync(scene).ConfigureAwait(true);

    /// <inheritdoc />
    protected override void Dispose(bool disposing)
    {
        if (this.isDisposed)
        {
            return;
        }

        if (disposing)
        {
            this.StopSearchOperations();
            this.selectionService.SelectionChanged -= this.OnSelectionServiceChanged;
            this.ObserveAuthoring(authoring: null);
            this.documentArea.ActiveDocumentChanged -= this.OnActiveDocumentChanged;
            this.openDocuments.Opened -= this.OnSceneDocumentOpened;
            this.openDocuments.Closed -= this.OnSceneDocumentClosed;
            if (this.sceneDocument is { } shown)
            {
                shown.ContentReplaced -= this.OnShownContentReplaced;
            }

            if (this.interaction is { } service)
            {
                service.StateChanged -= this.OnInteractionStateChanged;
            }

            this.structures?.Changed -= this.OnStructuresChanged;

            this.commandRegistration?.Dispose();
            if (this.availability is { } source)
            {
                source.SceneUnavailable -= this.OnSceneUnavailable;
            }


            // Ensure any in-flight scene load is cancelled and the CTS is disposed.
            try
            {
                if (this.loadSceneCts?.IsCancellationRequested == false)
                {
                    this.loadSceneCts?.Cancel();
                }
            }
#pragma warning disable CA1031 // Do not catch general exception types
            catch
            {
                // ignore cancellation errors during dispose
            }
#pragma warning restore CA1031 // Do not catch general exception types

            this.loadSceneCts?.Dispose();

            this.loadSceneCts = null;
            this.Scene?.Detach();
            this.projection.Clear();
        }

        this.isDisposed = true;
        this.RefreshContextActions();
        base.Dispose(disposing);
    }

    /// <inheritdoc />
    protected override void OnSelectionModelChanged(SelectionModel<ITreeItem>? oldValue)
    {
        base.OnSelectionModelChanged(oldValue);

        if (this.SelectionMode == SelectionMode.Single)
        {
            oldValue?.PropertyChanged -= this.OnSingleSelectionChanged;

            this.SelectionModel?.PropertyChanged += this.OnSingleSelectionChanged;
        }
        else if (this.SelectionMode == SelectionMode.Multiple)
        {
            if (oldValue is MultipleSelectionModel<ITreeItem> oldSelectionModel)
            {
                ((INotifyCollectionChanged)oldSelectionModel.SelectedIndices).CollectionChanged -=
                    this.OnMultipleSelectionChanged;
            }

            if (this.SelectionModel is MultipleSelectionModel<ITreeItem> currentSelectionModel)
            {
                ((INotifyCollectionChanged)currentSelectionModel.SelectedIndices).CollectionChanged +=
                    this.OnMultipleSelectionChanged;
            }
        }

        this.NotifySelectionDependentCommands();
    }

    /// <inheritdoc />
    protected override void OnClipboardCleared() => this.clipboard.Clear();

    /// <summary>
    /// The scene root row is exclusive: selecting it alone is the scene context, and it can never
    /// share a selection with node or folder rows (Explorer contract, section 4 of the plan).
    /// </summary>
    /// <inheritdoc />
    protected override bool AllowsCoSelection(ITreeItem candidate, IReadOnlyList<ITreeItem> currentlySelected)
        => candidate is not SceneAdapter && !currentlySelected.Any(static item => item is SceneAdapter);

    /// <inheritdoc />
    protected override bool IsIncludedInBulk(ITreeItem item) => item is not SceneAdapter;

    private static TreeItemRenameResult Renamed(SceneEditResult result)
        => result.Succeeded
            ? TreeItemRenameResult.Success
            : TreeItemRenameResult.Rejected(ErrorOf(result) ?? "The rename was rejected.");

    private static string? ErrorOf(SceneEditResult result)
        => result.Diagnostics.FirstOrDefault(static diagnostic => diagnostic.IsError)?.Message;

    private static async Task<TreeItemRenameResult> RenameFolderAsync(SceneAuthoring authoring, FolderAdapter folder, string newName)
    {
        var result = await authoring.ExecuteAsync(new RenameFolder(folder.Id, newName)).ConfigureAwait(true);
        if (result.Succeeded)
        {
            folder.Name = newName;
        }

        return Renamed(result);
    }

    private static SceneNodeAdapter? AsSceneNodeAdapter(ITreeItem? item)
        => item switch
        {
            SceneNodeAdapter lna => lna,
            _ => null,
        };

    private static SceneNode? AsSceneNode(ITreeItem? item)
        => item switch
        {
            SceneNodeAdapter lna => lna.AttachedObject,
            _ => null,
        };

    private static ITreeItem? FindPrimaryRow(IReadOnlyList<ITreeItem> targets, SceneSelectionContext context)
    {
        if (context.Kind == SceneSelectionKind.Scene)
        {
            return targets.FirstOrDefault(static target => target is SceneAdapter);
        }

        if (context.PrimaryFolderId is { } folderId)
        {
            return targets.FirstOrDefault(target => target is FolderAdapter folder && folder.Id == folderId);
        }

        if (context.PrimaryNode is { } node)
        {
            return targets.FirstOrDefault(target => target is NodeRowAdapter row && row.Reference == node);
        }

        return null;
    }

    /// <summary>Walks the actual scene ancestry, which is the only chain that can hide a node.</summary>
    /// <param name="node">The scene node to test.</param>
    /// <param name="service">The workspace interaction owner.</param>
    /// <returns><see langword="true"/> when an ancestor of <paramref name="node"/> is hidden.</returns>
    private static bool HasHiddenSceneAncestor(SceneNode node, SceneNodeInteraction service)
    {
        for (var parent = node.Hierarchy.Parent; parent is not null; parent = parent.Hierarchy.Parent)
        {
            if (service.IsHidden(NodeReference.To(parent.Id)))
            {
                return true;
            }
        }

        return false;
    }

    // A scene's name is its file name: renaming the scene renames its file, as the Content Explorer does, with one entry in
    // the global history; its document follows the file.
    private async Task<TreeItemRenameResult> RenameSceneAssetAsync(string name)
    {
        if (this.relocation is null || this.histories is null || this.sceneDocument is not { } document
            || this.session?.Context.FindByFilePath(document.File.Path) is not { } location)
        {
            return TreeItemRenameResult.Rejected("The scene's file cannot be renamed here.");
        }

        var target = Oxygen.Editor.ContentPipeline.Relocation.RelocationPaths.Rename(location.Path, name, isFolder: false);
        if (VirtualPath.ValidateSegment(target.Name, VirtualPathRuleSet.Authored) is { } invalid)
        {
            return TreeItemRenameResult.Rejected(invalid.ToMessage());
        }

        var relocation = this.relocation;
        if (!await this.documentArea.SaveBeforeAsync(Oxygen.Editor.Shell.Documents.SaveBeforeOperation.Rename, _ => Task.FromResult(relocation.FindUnsavedDocuments())).ConfigureAwait(true))
        {
            return TreeItemRenameResult.Rejected("Not renamed: the documents were not saved.");
        }

        try
        {
            _ = await relocation.RelocateAsync(
                new Oxygen.Editor.ContentPipeline.Relocation.AssetRelocationRequest { Moves = [new(location.Path, target)] },
                "Rename",
                this.histories.GlobalHistory,
                CancellationToken.None).ConfigureAwait(true);
            return TreeItemRenameResult.Success;
        }
        catch (Oxygen.Editor.ContentPipeline.Relocation.AssetRelocationException failure)
        {
            this.LogSceneRenameRejected(document.Title, name, failure.Message);
            return TreeItemRenameResult.Rejected(failure.Message);
        }
    }

    [RelayCommand(CanExecute = nameof(CanRenameSelected))]
    private void RenameSelected()
    {
        var item = this.SelectionModel?.SelectedItem ?? this.Scene;
        if (item is SceneAdapter || item?.IsLocked == false)
        {
            this.RenameRequested?.Invoke(this, new RenameRequestedEventArgs(item));
        }
    }

    private bool CanRenameSelected()
        => this.GetContextDisabledReason(
            SceneExplorerCommandKind.Rename,
            this.CaptureExplorerContext(anchor: null, background: false)) is null;

    [RelayCommand]
    private async Task Undo()
    {
        if (this.CurrentAuthoring() is not { } authoring)
        {
            return;
        }

        this.suppressNodeMessages = true;
        try
        {
            _ = await authoring.UndoAsync().ConfigureAwait(true);
            await this.ReconcileProjectionAsync().ConfigureAwait(true);
        }
        finally
        {
            this.suppressNodeMessages = false;
        }
    }

    [RelayCommand]
    private async Task Redo()
    {
        if (this.CurrentAuthoring() is not { } authoring)
        {
            return;
        }

        this.suppressNodeMessages = true;
        try
        {
            _ = await authoring.RedoAsync().ConfigureAwait(true);
            await this.ReconcileProjectionAsync().ConfigureAwait(true);
        }
        finally
        {
            this.suppressNodeMessages = false;
        }
    }

    private bool CanAddEntity()
    {
        if (this.Scene is null)
        {
            return false;
        }

        switch (this.SelectionModel)
        {
            case null:
                return true; // allow root creation when nothing is selected

            case SingleSelectionModel { SelectedItem: var item }:
                return item is null || item is SceneAdapter || AsSceneNode(item) is not null;

            case MultipleSelectionModel<ITreeItem> multiple:
                if (multiple.SelectedIndices.Count == 0)
                {
                    return true; // no selection: create at root
                }

                if (multiple.SelectedIndices.Count == 1)
                {
                    var selected = multiple.SelectedItems.FirstOrDefault();
                    return selected is null || selected is SceneAdapter || AsSceneNode(selected) is not null;
                }

                return false; // multi-select of 2+ items is invalid for creation

            default:
                return false;
        }
    }

    [RelayCommand(CanExecute = nameof(CanAddEntity))]
    private async Task AddEntity()
    {
        if (this.CurrentAuthoring() is not { } authoring)
        {
            return;
        }

        var target = this.GetSingleSelectionTarget();
        FolderId? parentFolderId = target switch { FolderAdapter folder => folder.Id, _ => null };
        NodeId? parentNodeId = target switch { SceneNodeAdapter nodeRow => nodeRow.AttachedObject.Id, _ => null };
        if (parentFolderId is { } inFolder && LayoutScopes.TryScopeOf(authoring.Layout, LayoutEntryId.Of(inFolder), out var folderScope))
        {
            parentNodeId = folderScope;
        }

        var node = SceneNodeTemplates.Empty(authoring.Scene, this.GetNextEntityName());
        this.suppressNodeMessages = true;
        try
        {
            var result = await authoring.ExecuteAsync(new InsertNodes([SceneNodeTemplates.Append(authoring.Scene, authoring.Layout, node, parentNodeId, parentFolderId)])).ConfigureAwait(true);
            if (result.Succeeded)
            {
                await this.ReconcileProjectionAsync().ConfigureAwait(true);
                _ = await this.SetSelectedNodes([node.Id]).ConfigureAwait(true);
            }
        }
        finally
        {
            this.suppressNodeMessages = false;
        }
    }

    private ITreeItem? GetSingleSelectionTarget()
        => this.SelectionModel switch
        {
            SingleSelectionModel { SelectedItem: var item } => item,
            MultipleSelectionModel<ITreeItem> { SelectedItems.Count: 1 } multiple => multiple.SelectedItems[0],
            _ => null,
        };

    [RelayCommand(CanExecute = nameof(CanCopy))]
    private Task Copy() => this.CopyItemsAsync(this.GetSelectedItems());

    private bool CanCopy()
        => this.GetContextDisabledReason(SceneExplorerCommandKind.Copy, this.CaptureExplorerContext(anchor: null, background: false)) is null;

    [RelayCommand(CanExecute = nameof(CanCut))]
    private Task Cut() => this.CutItemsAsync(this.GetSelectedItems());

    private bool CanCut()
        => this.GetContextDisabledReason(SceneExplorerCommandKind.Cut, this.CaptureExplorerContext(anchor: null, background: false)) is null;

    [RelayCommand(CanExecute = nameof(CanPaste))]
    private Task Paste() => this.PasteItemsAsync(targetParent: null);

    private bool CanPaste()
        => this.GetContextDisabledReason(SceneExplorerCommandKind.Paste, this.CaptureExplorerContext(anchor: null, background: false)) is null;

    /// <summary>
    /// Tests the staged payload against the clipboard's lifetime without changing anything: a copy can be pasted into
    /// any scene of the session, a cut only into the scene that staged it.
    /// </summary>
    /// <returns><see langword="true"/> when the payload may still be pasted.</returns>
    private bool ClipboardLifetimeIsCurrent() => this.clipboard.IsCurrentFor(this.Scene?.AttachedObject);

    /// <summary>
    /// Drops a payload that outlived its ratified lifetime and tells the user why.
    /// </summary>
    /// <param name="reason">The plain-language explanation shown with the warning.</param>
    private void InvalidateClipboard(string reason)
    {
        this.OnClipboardCleared();
        this.ClipboardStateStore = ClipboardState.Empty;
        this.ClearCutMarks();
        this.RaiseClipboardChanged();

        if (this.operationResults is not { } publisher || this.statusReducer is not { } reducer)
        {
            return;
        }

        _ = SceneOperationResults.PublishWarning(
            publisher,
            reducer,
            SceneOperationKinds.NodeDuplicate,
            FailureDomain.SceneAuthoring,
            DiagnosticCodes.ScenePrefix + "CLIPBOARD_INVALIDATED",
            "Clipboard was cleared",
            reason,
            new AffectedScope { SceneId = this.Scene?.AttachedObject.Id });
    }

    private async Task RemoveFromFolderAsync(ITreeItem anchor)
    {
        if (this.CurrentAuthoring() is not { } authoring || anchor is not SceneNodeAdapter node || node.Parent is not FolderAdapter folder
            || LayoutScopes.PositionOf(authoring.Layout, LayoutEntryId.Of(folder.Id)) is not { } folderPlace)
        {
            return;
        }

        // The node's entry goes right after its folder, in the folder's own container.
        var entry = LayoutEntryId.Of(node.AttachedObject.Id);
        this.suppressNodeMessages = true;
        try
        {
            var result = await authoring.ExecuteAsync(new MoveLayoutEntries([entry], folderPlace with { Index = folderPlace.Index + 1 })).ConfigureAwait(true);
            if (result.Succeeded)
            {
                await this.ReconcileProjectionAsync().ConfigureAwait(true);
            }
        }
        finally
        {
            this.suppressNodeMessages = false;
        }
    }

    private async Task MoveToSceneRootAsync(ITreeItem anchor)
    {
        if (this.CurrentAuthoring() is not { } authoring || anchor is not SceneNodeAdapter node)
        {
            return;
        }

        this.suppressNodeMessages = true;
        try
        {
            var result = await authoring.ExecuteAsync(new ReparentNodes([node.AttachedObject.Id], parent: null, preserveWorld: false)).ConfigureAwait(true);
            if (result.Succeeded)
            {
                await this.ReconcileProjectionAsync().ConfigureAwait(true);
            }
        }
        finally
        {
            this.suppressNodeMessages = false;
        }
    }

    private async void OnActiveDocumentChanged(object? sender, EventArgs e)
    {
        // Only scene documents replace the shown scene; another document leaves it in place.
        if (this.documentArea.ActiveDocument is not { Content: SceneContent } sceneDocument)
        {
            return;
        }

        // If we're already showing this scene, or already loading it, do nothing
        if (ReferenceEquals(this.sceneDocument, sceneDocument) || this.loadingDocumentId == sceneDocument.Id)
        {
            return;
        }

        this.LogDocumentActivated(sceneDocument.Id);

        // Scene replacement belongs to the Shell's document service; tab activation must not bypass its close guards
        // or the retirement of the previous scene.
        if (this.placement is { } placement)
        {
            _ = await placement.OpenDocumentAsync(sceneDocument.File.Asset).ConfigureAwait(true);
        }
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "The authoring operation boundary preserves committed state and reports failures to the editor instead of terminating the command loop.")]
    private async Task InitializeLoadedSceneAsync(Scene loadedScene, IList<ExplorerEntryData> layout)
    {
        var previousScene = this.Scene?.AttachedObject;
        this.ResetSearchSession();

        // Build the scene layout from the loaded scene
        this.Scene?.Detach();
        this.Scene = new SceneAdapter(loadedScene, layout)
        {
            IsExpanded = true,
            IsLocked = true,
            IsRoot = true,
            UseLayoutAdapters = true,
        };
        this.NotifyCategoryAvailabilityChanged();

        // The refill clears the previous scene's selected rows; keep that inside a sync window so
        // it cannot publish over the stored document selection, then restore by identity outside it.
        await this.WithProjectionSyncAsync(async () =>
        {
            await this.InitializeRootAsync(this.Scene, skipRoot: false).ConfigureAwait(true);
            this.AddInstancedRows(this.Scene);
            this.projection.Rebuild(this.Scene);

            // Reload what a previous session stored before the first rows are presented, otherwise
            // the persistence is write-only and every scene opens with nothing hidden or locked.
            await this.RestoreWorkspaceInteractionAsync(loadedScene).ConfigureAwait(true);
            this.ApplyExplorerFilter();
            this.ApplyWorkspaceInteractionState();
        }).ConfigureAwait(true);

        // Restore the stored document selection by identity: pruning gone nodes, then revealing and
        // reselecting the adapters that resolve. forcePublish makes the switch itself a visible
        // selection change, so panels following the loaded document converge on its own selection.
        _ = this.selectionService.Reconcile(this.SceneDocumentId, loadedScene, layout);
        await this.ApplySelectionContextAsync(
            this.selectionService.GetContext(this.SceneDocumentId),
            forcePublish: true).ConfigureAwait(true);
        this.ReleaseStaleClipboard(previousScene, loadedScene);

        // The instances' rows come once their sources are read; the scene does not wait for them.
        _ = this.ReadInstanceStructuresAsync();
    }

    // A loaded scene starts a fresh view session: a previous scene's transient search
    // expansions and filter predicate must not leak into it, and their adapters are retired
    // anyway, so restore could never find them.
    private void ResetSearchSession()
    {
        _ = Interlocked.Increment(ref this.searchGeneration);
        this.FilterPredicate = null;
        this.SearchText = string.Empty;
        this.SearchResultCount = 0;
        this.searchExpandedFolderIds.Clear();
        this.searchExpandedNodeIds.Clear();
    }

    // Another scene's rows lose their cut marks; a cut from a closed scene is dropped.
    private void ReleaseStaleClipboard(Scene? previousScene, Scene loadedScene)
    {
        if (!ReferenceEquals(previousScene, loadedScene))
        {
            this.ClearCutMarks();
            this.ClipboardItemStore = [];
        }

        if (this.clipboard.Payload is not null && !this.ClipboardLifetimeIsCurrent())
        {
            this.InvalidateClipboard("The staged Cut was cancelled because its scene lifetime ended.");
        }
    }

    /// <summary>
    /// Runs a projection refill with selection publishing suppressed: the refill clears selected
    /// rows outside any batch, and letting that publish through would overwrite the authoritative
    /// context with a spurious empty one before the caller can restore it.
    /// </summary>
    private async Task WithProjectionSyncAsync(Func<Task> refill)
    {
        ArgumentNullException.ThrowIfNull(refill);
        this.selectionSyncDepth++;
        try
        {
            await refill().ConfigureAwait(true);
        }
        finally
        {
            this.selectionSyncDepth--;
        }
    }

    private async Task<CancellationToken> BeginSceneLoadAsync()
    {
        if (this.loadSceneCts is { IsCancellationRequested: false })
        {
            await this.loadSceneCts.CancelAsync().ConfigureAwait(true);
        }

        this.loadSceneCts?.Dispose();
        this.loadSceneCts = new CancellationTokenSource();
        return this.loadSceneCts.Token;
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "Scene loading reports failures without terminating the editor.")]
    private async Task<bool> LoadSceneAsync(Scene scene)
    {
        var loadedDocument = this.documentArea.Documents
            .FirstOrDefault(document => document.Content is SceneContent content && ReferenceEquals(content.Scene, scene));
        if (loadedDocument is null)
        {
            return false;
        }

        this.loadingDocumentId = loadedDocument.Id;
        try
        {
            var ct = await this.BeginSceneLoadAsync().ConfigureAwait(true);

            var loadedScene = scene;

            // Trace: scene data successfully loaded from project storage
            this.LogSceneLoaded(loadedScene.Id, loadedScene.Name);

            this.nextEntityIndex = loadedScene.AllNodes.Count();

            return !ct.IsCancellationRequested
                && await this.InstallLoadedSceneAsync(loadedScene, loadedDocument, ct).ConfigureAwait(true);
        }
        catch (OperationCanceledException)
        {
            return false;
        }
        catch (Exception ex)
        {
            this.LogAuthoringLoadFailed(ex, scene.Id, scene.Name);
            return false;
        }
        finally
        {
            if (this.loadingDocumentId == loadedDocument.Id)
            {
                this.loadingDocumentId = DocumentId.Empty;
            }
        }
    }

    private async Task<int> SearchCoreAsync(string query, int generation)
    {
        if (generation != Volatile.Read(ref this.searchGeneration))
        {
            return 0;
        }

        if (this.Scene is not { } sceneAdapter)
        {
            await this.ClearSearchCoreAsync().ConfigureAwait(true);
            this.SearchResultCount = 0;
            return 0;
        }

        var trimmed = query.Trim();
        if (trimmed.Length == 0)
        {
            await this.ClearSearchCoreAsync().ConfigureAwait(true);
            if (generation == Volatile.Read(ref this.searchGeneration))
            {
                this.SearchResultCount = 0;
            }

            return 0;
        }

        return await this.SearchSceneAsync(sceneAdapter, trimmed, generation).ConfigureAwait(true);
    }

    private async Task<int> SearchSceneAsync(SceneAdapter sceneAdapter, string query, int generation)
    {
        var nodeById = sceneAdapter.AttachedObject.AllNodes.ToDictionary(node => node.Id);
        var matchingNodeIds = nodeById.Values
            .Where(node => this.IsIncludedByCategories(node)
                && node.Metadata.Name.Contains(query, StringComparison.OrdinalIgnoreCase))
            .Select(node => node.Id)
            .ToList();

        foreach (var nodeId in matchingNodeIds)
        {
            if (!this.IsCurrentSearch(generation, sceneAdapter))
            {
                return 0;
            }

            await this.ExpandMatchAncestryAsync(nodeId, nodeById, generation, sceneAdapter).ConfigureAwait(true);
        }

        var matchingFolders = this.projection.LayoutFolders
            .Where(folder => folder.Name.Contains(query, StringComparison.OrdinalIgnoreCase))
            .ToArray();
        foreach (var folder in matchingFolders)
        {
            if (!this.IsCurrentSearch(generation, sceneAdapter))
            {
                return 0;
            }

            if (folder.Id is { } folderId
                && this.projection.GetFolderLayoutAncestors(folderId) is { } layoutChain)
            {
                await this.ExpandLayoutAncestryAsync(layoutChain, generation, sceneAdapter).ConfigureAwait(true);
            }
        }

        if (!this.IsCurrentSearch(generation, sceneAdapter))
        {
            return 0;
        }

        // Count from the authored-layout domain so collapsed and unrealized folders remain included.
        var resultCount = matchingNodeIds.Count + matchingFolders.Length;
        this.ApplyExplorerFilter(query);
        this.SearchResultCount = resultCount;
        return resultCount;
    }

    private bool TryBeginSearchOperation()
    {
        lock (this.searchLifetimeLock)
        {
            if (this.searchLifetimeStopped || this.isDisposed)
            {
                return false;
            }

            this.activeSearchOperations++;
            return true;
        }
    }

    private void EndSearchOperation()
    {
        var disposeSearchGate = false;
        lock (this.searchLifetimeLock)
        {
            this.activeSearchOperations--;
            if (this.searchLifetimeStopped && this.activeSearchOperations == 0 && !this.searchGateDisposed)
            {
                this.searchGateDisposed = true;
                disposeSearchGate = true;
            }
        }

        if (disposeSearchGate)
        {
            this.searchGate.Dispose();
        }
    }

    private void StopSearchOperations()
    {
        var disposeSearchGate = false;
        lock (this.searchLifetimeLock)
        {
            this.searchLifetimeStopped = true;
            _ = Interlocked.Increment(ref this.searchGeneration);
            if (this.activeSearchOperations == 0 && !this.searchGateDisposed)
            {
                this.searchGateDisposed = true;
                disposeSearchGate = true;
            }
        }

        if (disposeSearchGate)
        {
            this.searchGate.Dispose();
        }
    }

    private async Task ClearSearchCoreAsync()
    {
        this.ApplyExplorerFilter();
        foreach (var folderId in this.searchExpandedFolderIds.ToArray())
        {
            if (this.projection.GetFolder(folderId) is not { } folder)
            {
                continue;
            }

            if (folder.IsExpanded)
            {
                await this.CollapseItemAsync(folder).ConfigureAwait(true);
            }

            // Clear the transient flag only after the search expansion is undone so the authored
            // layout entry never observes a search-driven expansion write.
            folder.SetExpansionTransient(transient: false);
        }

        foreach (var nodeId in this.searchExpandedNodeIds.ToArray())
        {
            if (this.projection.GetNode(nodeId) is { IsExpanded: true } node)
            {
                await this.CollapseItemAsync(node).ConfigureAwait(true);
            }
        }

        this.searchExpandedFolderIds.Clear();
        this.searchExpandedNodeIds.Clear();
    }

    private void ApplyExplorerFilter(string? query = null)
    {
        var trimmed = query ?? this.SearchText.Trim();
        var filterCategories = !this.ShowMeshesInExplorer || !this.ShowLightsInExplorer || !this.ShowCamerasInExplorer;
        if (trimmed.Length == 0 && !filterCategories)
        {
            this.FilterPredicate = null;
            return;
        }

        this.FilterPredicate = item =>
        {
            if (item is SceneNodeAdapter node && !this.IsIncludedByCategories(node.AttachedObject))
            {
                return false;
            }

            if (item is not NodeRowAdapter and not FolderAdapter)
            {
                return false;
            }

            return trimmed.Length == 0 || item.Label.Contains(trimmed, StringComparison.OrdinalIgnoreCase);
        };
    }

    private bool IsIncludedByCategories(SceneNode node)
    {
        var hasCategory = false;
        var included = false;
        foreach (var component in node.Components)
        {
            switch (component)
            {
                case GeometryComponent:
                    hasCategory = true;
                    included |= this.ShowMeshesInExplorer;
                    break;
                case DirectionalLightComponent or PointLightComponent or SpotLightComponent:
                    hasCategory = true;
                    included |= this.ShowLightsInExplorer;
                    break;
                case PerspectiveCamera or OrthographicCamera:
                    hasCategory = true;
                    included |= this.ShowCamerasInExplorer;
                    break;
            }
        }

        // Uncategorized nodes remain available regardless of the category toggles.
        return !hasCategory || included;
    }

    private void UpdateSearchResultCountForCurrentQuery()
    {
        var query = this.SearchText.Trim();
        if (query.Length == 0 || this.Scene is not { } sceneAdapter)
        {
            return;
        }

        var matchingNodeCount = sceneAdapter.AttachedObject.AllNodes.Count(node =>
            this.IsIncludedByCategories(node) && node.Metadata.Name.Contains(query, StringComparison.OrdinalIgnoreCase));
        var matchingFolderCount = this.projection.LayoutFolders.Count(folder =>
            folder.Name.Contains(query, StringComparison.OrdinalIgnoreCase));
        this.SearchResultCount = matchingNodeCount + matchingFolderCount;
    }

    private bool IsCurrentSearch(int generation, SceneAdapter sceneAdapter)
        => generation == Volatile.Read(ref this.searchGeneration)
            && !this.isDisposed
            && ReferenceEquals(this.Scene, sceneAdapter);

    private async Task ExpandMatchAncestryAsync(
        NodeId nodeId,
        Dictionary<NodeId, SceneNode> nodeById,
        int generation,
        SceneAdapter sceneAdapter)
    {
        if (this.projection.GetNodeLayoutAncestors(nodeId) is { } layoutChain)
        {
            // The node is seated in the authored layout: expand its visual ancestry (folders and
            // node seats) top-down using the layout position rather than scene parentage alone.
            await this.ExpandLayoutAncestryAsync(layoutChain, generation, sceneAdapter).ConfigureAwait(true);
            return;
        }

        // The node has no layout seat: fall back to its scene-graph ancestry over realized
        // adapters. Nodes use their actual scene ancestry rather than the realized adapter
        // Parent (unset while collapsed).
        var ancestors = new Stack<SceneNode>();
        for (var parent = nodeById[nodeId].Hierarchy.Parent; parent is not null; parent = parent.Hierarchy.Parent)
        {
            ancestors.Push(parent);
        }

        while (ancestors.TryPop(out var ancestorNode))
        {
            if (!this.IsCurrentSearch(generation, sceneAdapter))
            {
                return;
            }

            if (this.projection.GetNode(ancestorNode.Id) is { } ancestor && !ancestor.IsExpanded)
            {
                _ = this.searchExpandedNodeIds.Add(ancestorNode.Id);
                await this.ExpandItemAsync(ancestor).ConfigureAwait(true);
            }
        }
    }

    private async Task ExpandLayoutAncestryAsync(
        IReadOnlyList<SceneExplorerProjection.LayoutAncestor> layoutChain,
        int generation,
        SceneAdapter sceneAdapter)
    {
        foreach (var ancestor in layoutChain)
        {
            if (!this.IsCurrentSearch(generation, sceneAdapter))
            {
                return;
            }

            // An ancestor without a stably addressable adapter blocks the rest of the chain.
            if (!await this.ExpandSearchAncestorAsync(ancestor, generation, sceneAdapter).ConfigureAwait(true))
            {
                return;
            }
        }
    }

    private async Task<bool> ExpandSearchAncestorAsync(
        SceneExplorerProjection.LayoutAncestor ancestor,
        int generation,
        SceneAdapter sceneAdapter)
    {
        if (!this.IsCurrentSearch(generation, sceneAdapter))
        {
            return false;
        }

        ITreeItem? item = ancestor switch
        {
            { IsFolder: true, Folder: { } folderId } => this.projection.GetFolder(folderId),
            { IsFolder: false, Node: { } nodeId } => this.projection.GetNode(nodeId),
            _ => null,
        };
        if (item is null)
        {
            return false;
        }

        if (item.IsExpanded)
        {
            return true;
        }

        if (item is FolderAdapter folder)
        {
            // Search expansion is transient: it must not write the authored layout entry or
            // dirty the document. It is recorded by identity so a projection rebuild can
            // re-apply it as transient instead of baking the expanded view into the layout.
            folder.SetExpansionTransient(transient: true);
            _ = this.searchExpandedFolderIds.Add(folder.Id);
        }
        else if (ancestor.Node is { } nodeId)
        {
            _ = this.searchExpandedNodeIds.Add(nodeId);
        }

        await this.ExpandItemAsync(item).ConfigureAwait(true);
        return this.IsCurrentSearch(generation, sceneAdapter);
    }

    partial void OnSearchTextChanged(string value)
    {
        _ = value;
        this.OnPropertyChanged(nameof(this.HasSearchQuery));
    }

    partial void OnSearchResultCountChanged(int value)
    {
        _ = value;
        this.OnPropertyChanged(nameof(this.SearchResultText));
    }

    private void NotifyCategoryAvailabilityChanged()
    {
        this.OnPropertyChanged(nameof(this.HasMeshCategory));
        this.OnPropertyChanged(nameof(this.HasLightCategory));
        this.OnPropertyChanged(nameof(this.HasCameraCategory));
        this.OnPropertyChanged(nameof(this.HasAnyCategory));
        this.OnPropertyChanged(nameof(this.MeshCategoryColumnWidth));
        this.OnPropertyChanged(nameof(this.LightCategoryColumnWidth));
        this.OnPropertyChanged(nameof(this.CameraCategoryColumnWidth));
    }

    private void RefreshCategoryPresentation()
    {
        _ = Interlocked.Increment(ref this.searchGeneration);
        this.NotifyCategoryAvailabilityChanged();
        this.ApplyExplorerFilter();
        this.UpdateSearchResultCountForCurrentQuery();
        this.RestartActiveSearch();
    }

    private void OnSingleSelectionChanged(object? sender, PropertyChangedEventArgs args)
    {
        var isSelectionChange = string.Equals(args.PropertyName, nameof(SelectionModel<>.SelectedIndex), StringComparison.Ordinal)
                                || string.Equals(args.PropertyName, "SelectedItem", StringComparison.Ordinal);

        if (!isSelectionChange)
        {
            return;
        }

        // Item-level notifications arrive mid-transaction; the settled pass publishes once with
        // the complete membership and final active identity.
        if (this.IsSelectionBatchActive)
        {
            return;
        }

        this.UpdateSelectionDependentState();
    }

    private void OnMultipleSelectionChanged(object? sender, NotifyCollectionChangedEventArgs args)
    {
        if (this.SelectionModel is not MultipleSelectionModel)
        {
            return;
        }

        if (this.IsSelectionBatchActive)
        {
            return;
        }

        this.UpdateSelectionDependentState();
    }

    /// <summary>Frames a double-clicked node, or the whole scene for the scene row, in the focused viewport.</summary>
    private void OnTreeItemInvoked(object? sender, DynamicTreeEventArgs args)
    {
        _ = sender;
        if (this.Scene is not { } sceneAdapter)
        {
            return;
        }

        IReadOnlyList<NodeReference>? nodes = args.TreeItem switch
        {
            NodeRowAdapter node => [node.Reference],
            SceneAdapter => [],
            _ => null,
        };
        if (nodes is not null)
        {
            _ = this.views?.FrameAsync(this.SceneDocumentId, nodes);
        }
    }

    private void OnTreeSelectionSettled(object? sender, EventArgs args)
    {
        _ = sender;
        _ = args;
        this.UpdateSelectionDependentState();
    }

    /// <summary>Recomputes selection-derived state and publishes membership with primary once.</summary>
    private void UpdateSelectionDependentState()
    {
        // Inside a projection-sync window (tree reload/refill clears selection outside any
        // batch) publishing would overwrite the authoritative context with a spurious empty
        // one; the window owner publishes the restored state itself when it closes.
        if (this.selectionSyncDepth > 0)
        {
            return;
        }

        var items = this.GetSelectedItems();
        this.NotifySelectionDependentCommands();
        this.HasUnlockedSelectedItems = items.Any(static item => !item.IsLocked);
        this.PublishCurrentSelection(this.pendingAuthoritativeContext);
        this.RefreshContextActions();
    }

    private void PublishCurrentSelection(SceneSelectionContext? authoritativeContext = null)
    {
        if (this.Scene is not { } sceneAdapter)
        {
            return;
        }

        var items = this.GetSelectedItems();

        // Foreign selections forward the received context verbatim: re-deriving it from the rows
        // that happened to resolve would silently shrink the authoritative selection.
        var context = authoritativeContext ?? BuildSelectionContext(items, this.ActiveItem);

        this.suppressSelectionSync = true;
        try
        {
            this.selectionService.Publish(this.SceneDocumentId, context, "SceneExplorer");
        }
        finally
        {
            this.suppressSelectionSync = false;
        }

        this.lastPublishedContext = context;
    }

    /// <summary>
    /// Reconciles this Explorer's rows with a selection the service stored from another source
    /// (viewport picking, an external panel, a command reveal). Own echoes and other documents
    /// are ignored; the resolved row set is republished as the Explorer's classification.
    /// </summary>
    private void OnSelectionServiceChanged(object? sender, SceneSelectionChangedEventArgs args)
    {
        _ = sender;
        if (this.suppressSelectionSync
            || string.Equals(args.Source, "SceneExplorer", StringComparison.Ordinal))
        {
            return;
        }

        if (this.isDisposed || this.selectionSyncDepth > 0
            || this.Scene is not { } sceneAdapter || this.SceneDocumentId != args.DocumentId)
        {
            return;
        }

        _ = this.ApplyForeignSelectionAsync(args);
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "Selection event callbacks log failures at the task boundary instead of losing unobserved exceptions.")]
    private async Task ApplyForeignSelectionAsync(SceneSelectionChangedEventArgs args)
    {
        try
        {
            // The Inspector asks to inspect a node: a search that hides its row ends, and the row takes the focus.
            var inspecting = string.Equals(args.Source, InspectorSelectionSource, StringComparison.Ordinal)
                && this.RowsOf(args.Context.SelectedNodes).Any();
            if (inspecting)
            {
                await this.ClearSearchAsync().ConfigureAwait(true);
            }

            _ = await this.ApplySelectionContextAsync(args.Context, focusPrimary: inspecting).ConfigureAwait(true);

            // A viewport pick scrolls its row into view, leaving keyboard focus in the viewport.
            if (string.Equals(args.Source, ViewportSelectionSource, StringComparison.Ordinal)
                && this.ActiveItem is { } primary)
            {
                _ = this.BringItemIntoView(primary);
            }
        }
        catch (Exception exception)
        {
            this.LogSelectionApplyFailed(exception, args.DocumentId);
        }
    }

    private async Task<bool> ApplySelectionContextAsync(
        SceneSelectionContext context,
        bool forcePublish = false,
        bool focusPrimary = false)
    {
        if (this.Scene is not { } sceneAdapter || this.SelectionModel is not { } model)
        {
            return false;
        }

        // Reveal awaits can hop a thread; a newer selection request, a document switch or a newer
        // store write supersedes this application. It must then apply nothing, and publish nothing,
        // with the context it captured before the await.
        var generation = ++this.selectionApplyGeneration;
        var documentId = this.SceneDocumentId;
        var targets = await this.ResolveSelectionTargetsAsync(sceneAdapter, context).ConfigureAwait(true);

        if (!this.IsCurrentSelectionApplication(generation, sceneAdapter, documentId, context))
        {
            return false;
        }

        var primary = FindPrimaryRow(targets, context);

        // True when every identity the context names resolves to one of the applied rows.
        var fullyResolved =
            context.SelectedNodes.Count == targets.OfType<NodeRowAdapter>().Count()
            && context.SelectedFolderIds.Count == targets.OfType<FolderAdapter>().Count()
            && (context.Kind != SceneSelectionKind.Scene || targets.OfType<SceneAdapter>().Any());

        if (!forcePublish && this.SelectionMatchesTargets(targets, primary))
        {
            // Rows already carry this selection. A changed authoritative context still has to
            // reach consumers (it may name new folder or unresolvable identities); an identical
            // one must not re-notify.
            if (!ReferenceEquals(context, this.lastPublishedContext))
            {
                this.PublishCurrentSelection(context);
            }

            this.FocusSelectionPrimary(primary, focusPrimary);
            return fullyResolved;
        }

        this.ApplySelectionTargets(model, targets, primary, context);
        this.FocusSelectionPrimary(primary, focusPrimary);
        return fullyResolved;
    }

    private async Task<List<ITreeItem>> ResolveSelectionTargetsAsync(SceneAdapter sceneAdapter, SceneSelectionContext context)
    {
        var targets = new List<ITreeItem>();
        if (context.Kind == SceneSelectionKind.Scene)
        {
            targets.Add(sceneAdapter);
        }

        foreach (var node in context.SelectedNodes)
        {
            if (this.projection.GetRow(node) is { } nodeAdapter)
            {
                await this.RevealRowAsync(nodeAdapter).ConfigureAwait(true);
                if (this.ShownItems.Contains(nodeAdapter))
                {
                    targets.Add(nodeAdapter);
                }
            }
        }

        foreach (var folderId in context.SelectedFolderIds)
        {
            if (this.projection.GetFolder(folderId) is { } folderAdapter)
            {
                await this.RevealRowAsync(folderAdapter).ConfigureAwait(true);
                if (this.ShownItems.Contains(folderAdapter))
                {
                    targets.Add(folderAdapter);
                }
            }
        }

        return targets;
    }

    private bool IsCurrentSelectionApplication(
        int generation,
        SceneAdapter sceneAdapter,
        DocumentId documentId,
        SceneSelectionContext context)
        => generation == this.selectionApplyGeneration
            && !this.isDisposed
            && ReferenceEquals(this.Scene, sceneAdapter)
            && ReferenceEquals(this.selectionService.GetContext(documentId), context);

    private bool SelectionMatchesTargets(List<ITreeItem> targets, ITreeItem? primary)
    {
        var current = this.GetSelectedItems();
        return current.Count == targets.Count
            && targets.All(current.Contains)
            && ReferenceEquals(this.ActiveItem, primary);
    }

    private void ApplySelectionTargets(
        SelectionModel<ITreeItem> model,
        IReadOnlyCollection<ITreeItem> targets,
        ITreeItem? primary,
        SceneSelectionContext context)
    {
        // Forward the received context verbatim at settle: republishing rows-only state would let
        // projection visibility silently delete valid domain objects.
        this.pendingAuthoritativeContext = context;
        try
        {
            this.WithSelectionBatch(() =>
            {
                this.SetActiveItem(primary);
                model.ClearSelection();
                foreach (var target in targets)
                {
                    model.SelectItem(target);
                }
            });
        }
        finally
        {
            this.pendingAuthoritativeContext = null;
        }
    }

    private void FocusSelectionPrimary(ITreeItem? primary, bool focusPrimary)
    {
        if (focusPrimary && primary is not null)
        {
            _ = this.FocusItem(primary, RequestOrigin.Programmatic);
        }
    }

    /// <summary>
    /// Expands a row's collapsed ancestors so the row enters <see cref="DynamicTreeViewModel.ShownItems"/>.
    /// Folder ancestors are revealed as transient: selecting an object is a view action and must not
    /// author layout expansion or dirty the document.
    /// </summary>
    private async Task RevealRowAsync(ITreeItem item)
    {
        if (this.ShownItems.Contains(item))
        {
            return;
        }

        // Adapter.Parent is unset until lazy children load; the projection retains the full
        // realized placement even under collapsed folders and node seats.
        foreach (var ancestor in this.projection.GetAncestors(item))
        {
            if (ancestor.CanAcceptChildren && !ancestor.IsExpanded)
            {
                if (ancestor is FolderAdapter folder)
                {
                    folder.SetExpansionTransient(transient: true);
                }

                await this.ExpandItemAsync(ancestor).ConfigureAwait(true);
            }
        }
    }

    private List<ITreeItem> GetSelectedItems()
        => this.SelectionModel switch
        {
            null => [],
            MultipleSelectionModel multi2 => [.. multi2.SelectedIndices.Select(this.GetShownItemAt)],
            SingleSelectionModel when this.SelectedItem is not null => [this.SelectedItem],
            _ => [],
        };

    // The tree follows the edits other views make; the explorer reconciles after its own.
    private void OnSceneChanged(object? sender, SceneChangedEventArgs args)
    {
        if (args.Changes.Any(static change => change is SceneChange.ComponentAdded or SceneChange.ComponentRemoved
            or SceneChange.NodesAdded or SceneChange.NodesRemoved))
        {
            this.RefreshCategoryPresentation();
        }

        if (args.Changes.Any(static change => change is SceneChange.NodesAdded or SceneChange.NodesRemoved or SceneChange.NodesPlaced
            or SceneChange.NodeRenamed or SceneChange.LayoutChanged) || this.ChangesInstanceRoots(args.Changes))
        {
            _ = this.RefreshProjectionAsync();
        }
    }

    // Follows the authoring of the shown scene, which a reload replaces.
    private void ObserveAuthoring(SceneAuthoring? authoring)
    {
        if (ReferenceEquals(this.observedAuthoring, authoring))
        {
            return;
        }

        if (this.observedAuthoring is { } previous)
        {
            previous.Changed -= this.OnSceneChanged;
        }

        this.observedAuthoring = authoring;
        authoring?.Changed += this.OnSceneChanged;
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "Change callbacks report projection failures instead of losing unobserved task exceptions.")]
    private async Task RefreshProjectionAsync()
    {
        if (this.isDisposed || this.suppressNodeMessages || this.Scene is null)
        {
            return;
        }

        try
        {
            await this.ReconcileProjectionAsync().ConfigureAwait(true);
        }
        catch (Exception ex)
        {
            this.LogProjectionUpdateFailed(ex, this.SceneDocumentId);
        }
    }

    [RelayCommand(CanExecute = nameof(CanCreateFolder))]
    private async Task CreateFolder()
    {
        if (this.CurrentAuthoring() is not { } authoring)
        {
            return;
        }

        this.LogCreateFolderInvoked(this.SelectionModel, this.ShownItemsCount);

        var target = this.GetSingleSelectionTarget();
        var container = target switch
        {
            FolderAdapter folder => LayoutPosition.InFolder(folder.Id, 0),
            SceneNodeAdapter node => LayoutPosition.UnderNode(node.AttachedObject.Id, 0),
            _ => LayoutPosition.AtRoot(0),
        };

        this.suppressNodeMessages = true;
        try
        {
            var result = await authoring.ExecuteAsync(new CreateFolder(FolderId.New(), LayoutScopes.EndOf(authoring.Layout, container), "New Folder")).ConfigureAwait(true);
            if (result.Succeeded)
            {
                await this.ReconcileProjectionAsync().ConfigureAwait(true);
            }
        }
        finally
        {
            this.suppressNodeMessages = false;
        }
    }

    private bool CanCreateFolder()
        => this.SelectionModel is not MultipleSelectionModel<ITreeItem> multiple || multiple.SelectedIndices.Count <= 1;

    private void NotifySelectionDependentCommands()
    {
        this.AddEntityCommand.NotifyCanExecuteChanged();
        this.CreateFolderCommand.NotifyCanExecuteChanged();
        this.CopyCommand.NotifyCanExecuteChanged();
        this.CutCommand.NotifyCanExecuteChanged();
        this.PasteCommand.NotifyCanExecuteChanged();
    }

    private string GetNextEntityName()
    {
        var index = Interlocked.Increment(ref this.nextEntityIndex);
        return string.Create(System.Globalization.CultureInfo.InvariantCulture, $"New Entity {index}");
    }

    // The authoring of the shown scene; none while the document shows another scene or is gone.
    private SceneAuthoring? CurrentAuthoring()
        => !this.isDisposed && this.sceneDocument?.Content is SceneContent { Authoring: var authoring }
            && ReferenceEquals(authoring.Scene, this.Scene?.AttachedObject)
                ? authoring
                : null;

    // The authoring, when a row belongs to the shown scene.
    private SceneAuthoring? AuthoringFor(ITreeItem? item = null)
    {
        if (this.CurrentAuthoring() is not { } authoring)
        {
            return null;
        }

        var scene = authoring.Scene;

        if (item is not null)
        {
            var root = item;
            while (root.Parent is { } parent)
            {
                root = parent;
            }

            var owner = AsSceneNodeAdapter(item)?.AttachedObject.Scene ?? (root as SceneAdapter)?.AttachedObject;
            if (!ReferenceEquals(owner, scene))
            {
                return null;
            }
        }

        return authoring;
    }

    private async Task<bool> InstallLoadedSceneAsync(Scene loadedScene, Document sceneDocument, CancellationToken ct)
    {
        if (sceneDocument.Content is not SceneContent content)
        {
            return false;
        }

        this.WatchSceneDocument(sceneDocument);
        this.ObserveAuthoring(content.Authoring);

        await this.InitializeLoadedSceneAsync(loadedScene, content.Authoring.Layout).ConfigureAwait(true);
        return !ct.IsCancellationRequested && ReferenceEquals(this.Scene?.AttachedObject, loadedScene);
    }

    // A drop is a paste of the dragged items: a Ctrl-drag copies them, any other drop moves them; one node moved
    // among its siblings is a reorder.
    private ISceneEdit? CreateDropEdit(TreeDropRequest request, SceneAuthoring authoring, List<NodeId> nodeIds, List<FolderId> folderIds)
    {
        if (request.Operation != TreeDropOperation.Copy && nodeIds.Count == 1 && folderIds.Count == 0 && this.IsSameParentReorder(request, nodeIds[0]))
        {
            return this.ReorderWithinParent(authoring.Layout, nodeIds[0], request);
        }

        var target = request.Parent switch
        {
            FolderAdapter folder => new PasteTarget(Folder: folder.Id),
            SceneNodeAdapter node => new PasteTarget(Parent: node.AttachedObject.Id),
            _ => new PasteTarget(),
        };
        return SceneClipboard.Capture(authoring.Scene, authoring.Layout, nodeIds, folderIds) is { } dragged
            && dragged.TryPaste(authoring.Scene, authoring.Layout, target, request.Operation != TreeDropOperation.Copy, preserveWorld: false, out var paste, out _)
            ? paste
            : null;
    }

    // The row index is a place among the parent's entries; the graph index counts the node entries before it.
    private ReorderNode? ReorderWithinParent(IList<ExplorerEntryData> layout, NodeId nodeId, TreeDropRequest request)
    {
        var container = request.Parent is SceneNodeAdapter node ? LayoutPosition.UnderNode(node.AttachedObject.Id, 0) : LayoutPosition.AtRoot(0);
        var self = LayoutEntryId.Of(nodeId);
        var entries = LayoutScopes.EntriesOf(layout, container);
        var current = entries.ToList().IndexOf(self);
        if (current < 0)
        {
            return null;
        }

        var rest = entries.Where(entry => entry != self).ToList();
        var index = Math.Clamp(request.Index > current ? request.Index - 1 : request.Index, 0, rest.Count);
        var graphIndex = rest.Take(index).Count(static entry => entry.Node is not null);
        return new ReorderNode(nodeId, new NodePosition(this.projection.GetNode(nodeId)?.AttachedObject.Hierarchy.Parent?.Id, graphIndex), container with { Index = index });
    }

    private bool IsSameParentReorder(TreeDropRequest request, NodeId nodeId)
    {
        if (this.projection.GetNode(nodeId) is not { } adapter)
        {
            return false;
        }

        var sceneParentId = adapter.AttachedObject.Hierarchy.Parent?.Id;
        return request.Parent switch
        {
            SceneAdapter => sceneParentId is null,
            SceneNodeAdapter node => sceneParentId == node.AttachedObject.Id,
            _ => false,
        };
    }

    private async Task ReconcileProjectionAsync()
    {
        if (this.Scene is not { } sceneAdapter)
        {
            return;
        }

        // Replacing adapters must not publish intermediate empty selection over the store.
        await this.WithProjectionSyncAsync(async () =>
        {
            // The rebuild replaces every node row: the expanded ones are expanded again, parents before children.
            NodeReference[] expanded = [.. this.ShownItems.OfType<NodeRowAdapter>().Where(static row => row.IsExpanded).Select(static row => row.Reference)];
            var expandedFolderIds = sceneAdapter.GetExpandedFolderIds();
            await sceneAdapter.ReloadChildrenAsync(
                expandedFolderIds,
                preserveNodeExpansion: true,
                transientlyExpandedFolderIds: sceneAdapter.GetExpandedFolderIds(transientOnly: true)).ConfigureAwait(true);

            await this.InitializeRootAsync(sceneAdapter, skipRoot: false).ConfigureAwait(true);
            this.AddInstancedRows(sceneAdapter);
            this.projection.Rebuild(sceneAdapter);
            this.ApplyWorkspaceInteractionState();
            _ = this.selectionService.Reconcile(this.SceneDocumentId, sceneAdapter.AttachedObject, sceneAdapter.Layout);

            // Re-apply the recorded transient search expansion on the fresh adapters (folders were
            // re-expanded as transient during the reload; node seats need their runtime expansion).
            foreach (var nodeId in this.searchExpandedNodeIds.ToArray())
            {
                if (this.projection.GetNode(nodeId) is { IsExpanded: false } node)
                {
                    await this.ExpandItemAsync(node).ConfigureAwait(true);
                }
            }

            await this.RestoreExpansionAsync(expanded).ConfigureAwait(true);
        }).ConfigureAwait(true);

        // A foreign writer may change selection while the refill awaits. Restore the latest
        // authoritative context, never the snapshot from before the refill.
        if (ReferenceEquals(this.Scene, sceneAdapter))
        {
            await this.ApplySelectionContextAsync(this.selectionService.GetContext(this.SceneDocumentId)).ConfigureAwait(true);
        }

        // An edit may have placed an instance of a source not read yet.
        _ = this.ReadInstanceStructuresAsync();
    }

    /// <summary>
    /// Loads the stored hide and lock state for a freshly initialized scene and applies it.
    /// </summary>
    /// <param name="loadedScene">The scene model that just became active.</param>
    /// <returns>The task completing once the restored state is visible on the rows.</returns>
    /// <remarks>
    /// Without this the persistence is write-only: state saved by a previous session would never
    /// come back. An explorer composed without the interaction service has no hide or lock state.
    /// </remarks>
    private async Task RestoreWorkspaceInteractionAsync(Scene loadedScene)
    {
        if (this.interaction is not { } service)
        {
            return;
        }

        await service.RestoreAsync(loadedScene.Id).ConfigureAwait(true);
    }

    /// <summary>
    /// Projects the workspace interaction state onto the realized rows: the eye slot shows each
    /// node's explicit hidden entry, the row dimming follows the actual scene-ancestor closure, and
    /// the lock column mirrors the stored lock set.
    /// </summary>
    /// <remarks>
    /// This reads only workspace state and never calls the command service, so applying it cannot
    /// dirty a document or record history. A root row keeps its permanent lock; the workspace set
    /// does not unlock the scene root.
    /// </remarks>
    private void ApplyWorkspaceInteractionState()
    {
        if (this.interaction is not { } service)
        {
            return;
        }

        foreach (var node in this.projection.Nodes)
        {
            var nodeId = node.AttachedObject.Id;
            var isExplicitlyHidden = service.IsHidden(NodeReference.To(nodeId));
            node.IsHiddenInEditor = isExplicitlyHidden;
            node.IsEffectivelyHiddenInEditor = isExplicitlyHidden || HasHiddenSceneAncestor(node.AttachedObject, service);

            // A hidden node's row dims like the content the scene does not own.
            node.IsDimmed = node.IsEffectivelyHiddenInEditor;
            if (!node.IsRoot)
            {
                node.IsLocked = service.IsLocked(NodeReference.To(nodeId));
            }
        }

        // A node an instance brought in is hidden through its instanced ancestors and its instance root's ancestry too.
        if (this.Scene?.AttachedObject is { } scene)
        {
            foreach (var row in this.projection.InstancedNodes)
            {
                row.IsHiddenInEditor = service.IsHidden(row.Reference);
                row.IsEffectivelyHiddenInEditor = row.IsHiddenInEditor || AncestorsOf(scene, row.Reference).Any(service.IsHidden);
                row.IsLocked = service.IsLocked(row.Reference);
            }
        }

        // Logical folders group nodes; they are never part of the scene-hide closure (contract R1).
        foreach (var folder in this.projection.Folders)
        {
            folder.IsEffectivelyHiddenInEditor = false;
        }
    }

    private void OnInteractionStateChanged(object? sender, EventArgs args)
    {
        _ = sender;
        _ = args;
        this.ApplyWorkspaceInteractionState();
        if (this.interaction is { } service)
        {
            this.ApplyCategoryFilters(service.Categories);
        }

        this.OnPropertyChanged(nameof(this.CanChangeCategories));
        this.RefreshContextActions();
    }

    partial void OnShowMeshesInExplorerChanged(bool value)
    {
        _ = value;
        this.OnCategoryFilterChanged();
    }

    partial void OnShowLightsInExplorerChanged(bool value)
    {
        _ = value;
        this.OnCategoryFilterChanged();
    }

    partial void OnShowCamerasInExplorerChanged(bool value)
    {
        _ = value;
        this.OnCategoryFilterChanged();
    }

    private void OnCategoryFilterChanged()
    {
        if (this.isApplyingCategoryFilters)
        {
            return;
        }

        _ = Interlocked.Increment(ref this.searchGeneration);
        this.ApplyExplorerFilter();
        this.UpdateSearchResultCountForCurrentQuery();
        this.PersistCategoryFilters();
        this.RestartActiveSearch();
    }

    private void PersistCategoryFilters()
    {
        if (!this.isApplyingCategoryFilters && this.interaction is { } service)
        {
            service.SetCategories(new(
                this.ShowMeshesInExplorer,
                this.ShowLightsInExplorer,
                this.ShowCamerasInExplorer));
        }
    }

    private void ApplyCategoryFilters(SceneCategories categories)
    {
        this.isApplyingCategoryFilters = true;
        try
        {
            this.ShowMeshesInExplorer = categories.ShowMeshes;
            this.ShowLightsInExplorer = categories.ShowLights;
            this.ShowCamerasInExplorer = categories.ShowCameras;
        }
        finally
        {
            this.isApplyingCategoryFilters = false;
        }

        this.ApplyExplorerFilter();
        this.UpdateSearchResultCountForCurrentQuery();
        this.RestartActiveSearch();
    }

    private void RestartActiveSearch()
    {
        if (this.HasSearchQuery && !this.isDisposed)
        {
            _ = this.RestartActiveSearchAsync(this.SearchText);
        }
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage(
        "Design",
        "CA1031:Do not catch general exception types",
        Justification = "Background search restarts report failures through the same logger as the view event boundary.")]
    private async Task RestartActiveSearchAsync(string query)
    {
        try
        {
            _ = await this.SearchAsync(query).ConfigureAwait(true);
        }
        catch (Exception exception)
        {
            this.ReportSearchFailure(exception, query);
        }
    }

    /// <summary>
    /// Hides or shows the anchor row — or the whole selection when no anchor is given — in the
    /// editing viewports. This is workspace state: it never writes authored visibility.
    /// </summary>
    /// <param name="anchor">The row invoked from an eye slot or context menu.</param>
    [RelayCommand]
    private async Task ToggleEditorHiddenAsync(ITreeItem? anchor)
    {
        if (this.interaction is not { } service)
        {
            this.ReportWorkspaceStateUnavailable("Hide in editor");
            return;
        }

        var nodes = this.ResolveWorkspaceTargets(anchor);
        if (nodes.Count == 0 || this.CurrentAuthoring() is not { } authoring)
        {
            return;
        }

        // One intent, one undo step: the batch target is decided from the first row so a mixed
        // selection hides or shows uniformly, the way Delete and Reparent batches already do.
        var hide = !service.IsHidden(nodes[0]);
        _ = await authoring.ExecuteAsync(new SetEditorHidden(nodes, hide)).ConfigureAwait(true);
    }

    /// <summary>Locks or unlocks the anchor row, or the whole selection, against editing.</summary>
    /// <param name="anchor">The row invoked from a lock slot or context menu.</param>
    [RelayCommand]
    private void ToggleEditorLocked(ITreeItem? anchor)
    {
        if (this.interaction is not { } service)
        {
            this.ReportWorkspaceStateUnavailable("Lock");
            return;
        }

        foreach (var node in this.ResolveWorkspaceTargets(anchor))
        {
            service.SetLocked(node, !service.IsLocked(node));
        }
    }

    /// <summary>
    /// Shows every hidden node of the active scene, leaving locks untouched. Recorded as one undo
    /// step through the command owner, so a mistaken Show All is recoverable.
    /// </summary>
    [RelayCommand]
    private async Task ShowAllInEditorAsync()
    {
        if (this.interaction is not { } service)
        {
            this.ReportWorkspaceStateUnavailable("Show All");
            return;
        }

        var hidden = service.HiddenNodes();
        if (hidden.Count == 0)
        {
            return;
        }

        if (this.CurrentAuthoring() is not { } authoring)
        {
            return;
        }

        _ = await authoring.ExecuteAsync(new SetEditorHidden([.. hidden], hidden: false)).ConfigureAwait(true);
    }

    /// <summary>
    /// Resolves the nodes a workspace interaction acts on: the invoked row when one is given,
    /// otherwise the current selection. Folder rows carry no node identity and are skipped.
    /// </summary>
    /// <param name="anchor">The invoked row, or <see langword="null"/> for the selection.</param>
    /// <returns>The references of the nodes to toggle, which may be nodes an instance brought in.</returns>
    private IReadOnlyList<NodeReference> ResolveWorkspaceTargets(ITreeItem? anchor)
    {
        if (anchor is NodeRowAdapter single)
        {
            return [single.Reference];
        }

        return [.. this.GetSelectedItems().OfType<NodeRowAdapter>().Select(static node => node.Reference)];
    }

    /// <summary>Reports a drop refused because it moves a node an instance brought in, or gives one a child.</summary>
    /// <param name="refusal">The <see cref="SceneEditCodes.InstancedStructure"/> refusal.</param>
    private void ReportInstancedStructureRefused(SceneDiagnostic refusal)
    {
        this.LogInstancedDropRefused(refusal.Node);
        if (this.operationResults is not { } publisher || this.statusReducer is not { } reducer)
        {
            return;
        }

        _ = SceneOperationResults.PublishFailure(
            publisher,
            reducer,
            SceneOperationKinds.NodeReparent,
            FailureDomain.SceneAuthoring,
            refusal.Code,
            "Nodes not moved",
            refusal.Message,
            new AffectedScope { SceneId = this.Scene?.AttachedObject.Id });
    }

    /// <summary>
    /// Reports a workspace interaction that cannot be honoured because its owner is not present.
    /// A silent no-op would leave the user believing the row responded.
    /// </summary>
    /// <param name="action">The action the user invoked.</param>
    private void ReportWorkspaceStateUnavailable(string action)
    {
        if (this.operationResults is not { } publisher || this.statusReducer is not { } reducer)
        {
            this.LogWorkspaceStateUnavailable(action);
            return;
        }

        _ = SceneOperationResults.PublishWarning(
            publisher,
            reducer,
            RuntimeOperationKinds.SettingsApply,
            FailureDomain.Settings,
            DiagnosticCodes.SettingsPrefix + "WORKSPACE_STATE_UNAVAILABLE",
            action + " is unavailable",
            "The workspace interaction service is not composed in this window, so '" + action + "' did nothing.",
            new AffectedScope { SceneId = this.Scene?.AttachedObject.Id });
    }
}
