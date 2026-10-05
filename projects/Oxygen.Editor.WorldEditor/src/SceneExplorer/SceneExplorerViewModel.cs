// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Collections.Specialized;
using System.ComponentModel;
using System.Diagnostics;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Controls;
using DroidNet.Controls.Selection;
using DroidNet.Documents;
using DroidNet.Routing;
using DroidNet.TimeMachine;
using DroidNet.TimeMachine.Changes;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI;
using Microsoft.UI.Xaml;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Diagnostics;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.World.Messages;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Services;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Oxygen.Editor.WorldEditor.Documents.Selection;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.World.SceneExplorer;

/// <summary>
///     The ViewModel for the <see cref="SceneExplorer.SceneExplorerView" /> view.
/// </summary>
public partial class SceneExplorerViewModel : DynamicTreeViewModel
{
    private readonly ILogger<SceneExplorerViewModel> logger;
    private readonly IMessenger messenger;
    private readonly IRouter router;
    private readonly IProjectManagerService projectManager;
    private readonly IDocumentService documentService;
    private readonly WindowId windowId;
    private readonly ISceneEngineSync sceneEngineSync;
    private readonly ISceneSelectionService selectionService;
    private readonly ISceneDocumentCommandService commandService;
    private readonly IOperationResultPublisher? operationResults;
    private readonly IStatusReducer? statusReducer;
    private readonly Workspace.WorkspaceInteractionService? interaction;
    private readonly IProjectContextService? projectContexts;
    private readonly SceneExplorerProjection projection = new();
    private readonly SemaphoreSlim searchGate = new(1, 1);
    private readonly System.Threading.Lock searchLifetimeLock = new();

    // Node-clipboard state: copied node identities plus whether they were cut (moved) rather than copied.
    private readonly List<Guid> clipboardNodeIds = [];
    private readonly List<SceneNodeData> clipboardSnapshots = [];
    private bool clipboardIsCut;

    // Ratified clipboard lifetime (D7): a payload is stamped with the project and scene that
    // produced it, so a stale payload can never be pasted into another project's scene.
    private Guid? clipboardProjectId;
    private string? clipboardProjectRoot;
    private Guid? clipboardSceneId;
    private WeakReference<Scene>? clipboardSceneLifetime;

    // Transient search expansions recorded by identity: folders/nodes expanded for a match,
    // restored when search clears, and re-applied as transient across a projection rebuild.
    private readonly HashSet<Guid> searchExpandedFolderIds = [];
    private readonly HashSet<Guid> searchExpandedNodeIds = [];
    private int nextEntityIndex;
    private int searchGeneration;
    private int activeSearchOperations;
    private bool searchLifetimeStopped;
    private bool searchGateDisposed;
    private CancellationTokenSource? loadSceneCts;
    private Guid loadingDocumentId = Guid.Empty;
    private bool suppressNodeMessages;

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
    /// <param name="projectManager">The project manager service.</param>
    /// <param name="messenger">The messenger service used for cross-component communication.</param>
    /// <param name="router">The router service used for navigation events.</param>
    /// <param name="documentService">The document service for handling document operations.</param>
    /// <param name="windowId">The window identifier for the associated window.</param>
    /// <param name="sceneEngineSync">The scene-engine synchronization service.</param>
    /// <param name="selectionService">The document selection service.</param>
    /// <param name="commandService">The document command service.</param>
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
    /// <param name="projectContexts">Optional project context used to scope project-owned Explorer state.</param>
    public SceneExplorerViewModel(
        IProjectManagerService projectManager,
        IMessenger messenger,
        IRouter router,
        IDocumentService documentService,
        WindowId windowId,
        ISceneEngineSync sceneEngineSync,
        ISceneSelectionService selectionService,
        ISceneDocumentCommandService commandService,
        ILoggerFactory? loggerFactory = null,
        IOperationResultPublisher? operationResults = null,
        IStatusReducer? statusReducer = null,
        Workspace.WorkspaceInteractionService? interaction = null,
        IProjectContextService? projectContexts = null)
        : base(loggerFactory)
    {
        this.operationResults = operationResults;
        this.statusReducer = statusReducer;
        this.interaction = interaction;
        this.projectContexts = projectContexts;
        if (interaction is { } service)
        {
            this.ApplyCategoryFilters(service.Categories);
            service.StateChanged += this.OnInteractionStateChanged;
        }

        this.logger = loggerFactory?.CreateLogger<SceneExplorerViewModel>() ??
                      NullLoggerFactory.Instance.CreateLogger<SceneExplorerViewModel>();
        this.projectManager = projectManager;
        this.messenger = messenger;
        this.router = router;
        this.documentService = documentService;
        this.windowId = windowId;
        this.sceneEngineSync = sceneEngineSync;
        this.sceneEngineSync.SceneSynchronized += this.OnSceneSynchronized;
        this.selectionService = selectionService;
        this.selectionService.SelectionChanged += this.OnSelectionServiceChanged;
        this.SelectionSettled += this.OnTreeSelectionSettled;
        this.commandService = commandService;
        this.PropertyChanged += this.OnContextOwnerPropertyChanged;
        this.ClipboardContentChanged += (_, _) => this.RefreshContextActions();

        this.UndoStack = this.History.UndoStack;
        this.RedoStack = this.History.RedoStack;

        messenger.Register<SceneNodeSelectionRequestMessage>(this, this.OnSceneNodeSelectionRequested);
        messenger.Register<InspectSceneNodeMessage>(this, (_, message) =>
        {
            if (!this.isDisposed && message.WindowId == this.windowId && !message.HasReceivedResponse)
            {
                message.Reply(this.InspectNodeAsync(message.NodeId));
            }
        });
        messenger.Register<SceneNodeAddedMessage>(this, this.OnSceneNodeAdded);
        messenger.Register<SceneNodeRemovedMessage>(this, this.OnSceneNodeRemoved);
        messenger.Register<ComponentAddedMessage>(this, this.OnComponentAdded);
        messenger.Register<ComponentRemovedMessage>(this, this.OnComponentRemoved);
        messenger.Register<SceneReloadedMessage>(this, (_, message) =>
        {
            if (!this.isDisposed && message.WindowId == this.windowId
                && this.documentService.GetActiveDocumentId(this.windowId) == message.Metadata.DocumentId
                && !message.HasReceivedResponse)
            {
                message.Reply(this.ApplyReloadedSceneAsync(message));
            }
        });

        // Default selection mode for Scene Explorer is multiple selection.
        this.SelectionMode = SelectionMode.Multiple;

        // Subscribe to document events to load scene when a scene document is opened
        documentService.DocumentOpened += this.OnDocumentOpened;
        documentService.DocumentActivated += this.OnDocumentActivated;
        this.RegisterSceneLifetime();
    }

    /// <summary>
    /// Fired when the ViewModel requests a rename operation in the View.
    /// </summary>
    internal event EventHandler<RenameRequestedEventArgs?>? RenameRequested;

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

    /// <summary>
    ///     Gets the undo stack.
    /// </summary>
    [ObservableProperty]
    public partial ReadOnlyObservableCollection<IChange> UndoStack { get; set; }

    /// <summary>
    ///     Gets the redo stack.
    /// </summary>
    [ObservableProperty]
    public partial ReadOnlyObservableCollection<IChange> RedoStack { get; set; }

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

    /// <summary>Reports a search failure caught at the asynchronous view event boundary.</summary>
    /// <param name="exception">The search exception.</param>
    /// <param name="query">The query that failed.</param>
    internal void ReportSearchFailure(Exception exception, string query)
        => LogSearchFailed(this.logger, exception, query);

    private HistoryKeeper History => this.Scene != null ? UndoRedo.GetHistory(this.Scene.AttachedObject.Id) : UndoRedo.Default[this];

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

        var context = this.CreateCommandContext();
        if (context is null || !this.IsCurrentContextRow(item) || this.FindLockedContextRow(item) is not null)
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

        var result = item switch
        {
            SceneNodeAdapter node => await this.commandService.RenameNodeAsync(context, node.AttachedObject.Id, trimmed).ConfigureAwait(true),
            FolderAdapter folder => await this.RenameFolderAsync(context, folder, trimmed).ConfigureAwait(true),
            _ => new SceneCommandResult(Succeeded: false),
        };

        return result.Succeeded
            ? TreeItemRenameResult.Success
            : TreeItemRenameResult.Rejected(result.ValidationMessage ?? "The rename was rejected.");
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
    public async Task<bool> SetSelectedNodes(IReadOnlyList<Guid> nodeIds, bool focusPrimary = false)
    {
        ArgumentNullException.ThrowIfNull(nodeIds);
        if (this.Scene is not { } sceneAdapter)
        {
            return false;
        }

        var distinctIds = nodeIds.Distinct().ToArray();
        var context = distinctIds.Length == 0
            ? SceneSelectionContext.Empty
            : new SceneSelectionContext(SceneSelectionKind.Node, distinctIds, [], distinctIds[^1], PrimaryFolderId: null);

        this.suppressSelectionSync = true;
        try
        {
            this.selectionService.Publish(sceneAdapter.AttachedObject.Id, context, "SceneExplorer");
        }
        finally
        {
            this.suppressSelectionSync = false;
        }

        return await this.ApplySelectionContextAsync(context, focusPrimary: focusPrimary).ConfigureAwait(true);
    }

    private async Task<SceneCommandResult> RenameFolderAsync(SceneDocumentCommandContext context, FolderAdapter folder, string newName)
    {
        var result = await this.commandService.RenameFolderAsync(context, folder.Id, newName).ConfigureAwait(true);
        if (result.Succeeded)
        {
            folder.Name = newName;
        }

        return result;
    }

    /// <summary>
    ///     Returns the <see cref="TreeItemAdapter"/> associated with the given scene node <paramref name="nodeId"/>,
    ///     or <see langword="null"/> if no adapter is registered for that id.
    /// </summary>
    /// <param name="nodeId">The id of the scene node to look up.</param>
    /// <returns>A <see cref="Task"/> that returns the adapter or <see langword="null"/>.</returns>
    public Task<TreeItemAdapter?> FindAdapterByNodeIdAsync(Guid nodeId)
        => Task.FromResult<TreeItemAdapter?>(this.projection.GetNode(nodeId));

    /// <summary>Selects scene-level properties for an explicit diagnostic navigation.</summary>
    /// <param name="sceneId">The scene whose stored node selection is cleared.</param>
    public void SelectEnvironment(Guid sceneId)
    {
        this.selectionService.Clear(sceneId);
        if (this.Scene?.AttachedObject.Id == sceneId)
        {
            this.WithSelectionBatch(() =>
            {
                this.SetActiveItem(item: null);
                this.SelectionModel?.ClearSelection();
            });
        }
    }

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
            this.sceneEngineSync.SceneSynchronized -= this.OnSceneSynchronized;
            this.selectionService.SelectionChanged -= this.OnSelectionServiceChanged;
            this.documentService.DocumentOpened -= this.OnDocumentOpened;
            this.documentService.DocumentActivated -= this.OnDocumentActivated;
            this.documentService.DocumentClosed -= this.OnSceneDocumentClosed;
            this.projectClipboardSubscription?.Dispose();
            if (this.interaction is { } service)
            {
                service.StateChanged -= this.OnInteractionStateChanged;
            }

            this.messenger.UnregisterAll(this);

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

    [RelayCommand(CanExecute = nameof(CanRenameSelected))]
    private void RenameSelected()
    {
        var item = this.SelectionModel?.SelectedItem;
        if (item?.IsLocked == false)
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
        using var authoring = this.EnterTreeAuthoring();
        if (authoring is null)
        {
            return;
        }

        this.suppressNodeMessages = true;
        try
        {
            await this.History.UndoAsync(this.loadSceneCts?.Token ?? CancellationToken.None).ConfigureAwait(true);
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
        using var authoring = this.EnterTreeAuthoring();
        if (authoring is null)
        {
            return;
        }

        this.suppressNodeMessages = true;
        try
        {
            await this.History.RedoAsync(this.loadSceneCts?.Token ?? CancellationToken.None).ConfigureAwait(true);
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
        var context = this.CreateCommandContext();
        if (context is null)
        {
            return;
        }

        var target = this.GetSingleSelectionTarget();
        Guid? parentNodeId = target switch { SceneNodeAdapter node => node.AttachedObject.Id, _ => null };
        Guid? parentFolderId = target switch { FolderAdapter folder => folder.Id, _ => null };

        var name = this.GetNextEntityName();
        this.suppressNodeMessages = true;
        try
        {
            var result = await this.commandService.CreateNodeAsync(context, parentNodeId, parentFolderId, name).ConfigureAwait(true);
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

    private ITreeItem? GetSingleSelectionTarget()
        => this.SelectionModel switch
        {
            SingleSelectionModel { SelectedItem: var item } => item,
            MultipleSelectionModel<ITreeItem> { SelectedItems.Count: 1 } multiple => multiple.SelectedItems[0],
            _ => null,
        };

    /// <inheritdoc />
    public override Task CopyItemsAsync(IReadOnlyList<ITreeItem> items)
    {
        if (items.Any(static item => item is not SceneNodeAdapter))
        {
            return this.StageExplorerClipboardAsync(items, cut: false);
        }

        var selectedIds = items.OfType<SceneNodeAdapter>().Select(static row => row.AttachedObject.Id).ToHashSet();
        var adapters = items.OfType<SceneNodeAdapter>().Where(row =>
        {
            for (var parent = row.AttachedObject.Parent; parent is not null; parent = parent.Parent)
            {
                if (selectedIds.Contains(parent.Id))
                {
                    return false;
                }
            }

            return true;
        }).ToList();
        if (adapters.Count == 0)
        {
            return Task.CompletedTask;
        }

        var snapshots = new List<SceneNodeData>(adapters.Count);
        var localSnapshots = new List<SceneNodeData>(adapters.Count);
        this.clipboardWorldPoseAvailable = true;
        foreach (var adapter in adapters)
        {
            var node = adapter.AttachedObject;
            var data = node.Dehydrate();
            localSnapshots.Add(data);
            if (node.Parent is not null && !node.IgnoreParentTransform)
            {
                if (!SceneTransformMath.TryPreserveWorldLocal(node, newParent: null, out var position, out var rotation, out var scale))
                {
                    this.clipboardWorldPoseAvailable = false;
                }
                else
                {
                    data = data with
                    {
                        Components = data.Components.Select(component => component is TransformData transform
                            ? transform with { Position = position, Rotation = rotation, Scale = scale }
                            : component).ToList(),
                    };
                }
            }

            snapshots.Add(data);
        }

        this.clipboardNodeIds.Clear();
        this.explorerClipboard = null;
        this.clipboardLocalSnapshots = localSnapshots;
        this.clipboardSnapshots.Clear();
        this.clipboardNodeIds.AddRange(adapters.Select(adapter => adapter.AttachedObject.Id));
        this.clipboardSnapshots.AddRange(snapshots);

        this.clipboardIsCut = false;
        this.StampClipboardOrigin();

        this.ClipboardItemStore = [.. items];
        this.ClipboardStateStore = ClipboardState.Copied;
        this.ClearCutMarks();
        this.RaiseClipboardChanged();
        return Task.CompletedTask;
    }

    /// <inheritdoc />
    public override Task CutItemsAsync(IReadOnlyList<ITreeItem> items)
    {
        if (items.Any(item => this.FindLockedContextRow(item) is not null))
        {
            return Task.CompletedTask;
        }

        if (items.Any(static item => item is FolderAdapter))
        {
            return this.StageExplorerClipboardAsync(items, cut: true);
        }

        var eligible = items.ToArray();
        var nodeIds = eligible.OfType<SceneNodeAdapter>().Select(adapter => adapter.AttachedObject.Id).ToList();
        if (nodeIds.Count == 0)
        {
            return Task.CompletedTask;
        }

        this.clipboardNodeIds.Clear();
        this.explorerClipboard = null;
        this.clipboardNodeIds.AddRange(nodeIds);
        this.clipboardSnapshots.Clear();
        this.clipboardIsCut = true;
        this.StampClipboardOrigin();

        this.ClipboardStateStore = ClipboardState.Cut;
        this.ClearCutMarks();
        this.CutMarkedStore = eligible;
        foreach (var item in eligible)
        {
            item.IsCut = true;
        }

        this.ClipboardItemStore = eligible;
        this.RaiseClipboardChanged();
        return Task.CompletedTask;
    }

    /// <inheritdoc />
    public override async Task PasteItemsAsync(ITreeItem? targetParent = null, int? insertIndex = null)
    {
        _ = insertIndex;
        if (!this.ClipboardLifetimeIsCurrent())
        {
            this.InvalidateClipboard(
                this.clipboardIsCut
                    ? "The cut was cleared because its source scene is no longer loaded."
                    : "The clipboard was cleared because the project changed.");
            return;
        }

        await this.ExecuteContextActionAsync(SceneExplorerCommandKind.Paste,
            this.CaptureExplorerContext(targetParent, background: false)).ConfigureAwait(true);
    }

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

    /// <inheritdoc />
    protected override void OnClipboardCleared()
    {
        this.clipboardGeneration++;
        this.explorerClipboard = null;
        this.clipboardLocalSnapshots = [];
        this.clipboardNodeIds.Clear();
        this.clipboardSnapshots.Clear();
        this.clipboardIsCut = false;
        this.clipboardProjectId = null;
        this.clipboardProjectRoot = null;
        this.clipboardSceneId = null;
        this.clipboardSceneLifetime = null;
    }

    /// <summary>
    /// Records which project and scene produced the staged payload.
    /// </summary>
    private void StampClipboardOrigin()
    {
        this.clipboardGeneration++;
        this.clipboardProjectId = this.projectManager.CurrentProject?.ProjectInfo.Id;
        this.clipboardProjectRoot = this.projectManager.CurrentProject?.ProjectInfo.Location;
        this.clipboardSceneId = this.Scene?.AttachedObject.Id;
        this.clipboardSceneLifetime = this.Scene is { } root ? new(root.AttachedObject) : null;
    }

    /// <summary>
    /// Tests the staged payload against the ratified clipboard lifetime without changing anything.
    /// </summary>
    /// <remarks>
    ///     The ratified rule (D7) is that a snapshot Copy survives a scene switch inside one
    ///     project, a Cut is bound to the scene that staged it, and nothing survives a project
    ///     switch. An unstamped payload is treated as current, so a test double or a project-less
    ///     session is not falsely invalidated.
    /// </remarks>
    /// <returns><see langword="true"/> when the payload may still be pasted.</returns>
    private bool ClipboardLifetimeIsCurrent()
    {
        var currentProjectId = this.projectManager.CurrentProject?.ProjectInfo.Id;
        if (this.clipboardProjectId is { } stagedProject && stagedProject != currentProjectId)
        {
            return false;
        }

        if (this.clipboardProjectRoot is { } stagedRoot
            && !string.Equals(stagedRoot, this.projectManager.CurrentProject?.ProjectInfo.Location, StringComparison.OrdinalIgnoreCase))
        {
            return false;
        }

        return !this.clipboardIsCut
            || (this.Scene is { } current && this.clipboardSceneId == current.AttachedObject.Id
                && this.clipboardSceneLifetime is { } lifetime && lifetime.TryGetTarget(out var original)
                && ReferenceEquals(original, current.AttachedObject));
    }

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
            new AffectedScope { DocumentId = this.Scene?.AttachedObject.Id });
    }

    private async Task RemoveFromFolderAsync(ITreeItem anchor)
    {
        var context = this.CreateCommandContext();
        if (context is null || anchor is not SceneNodeAdapter node || node.Parent is not FolderAdapter folder)
        {
            return;
        }

        this.suppressNodeMessages = true;
        try
        {
            var result = await this.commandService.RemoveNodesFromFolderAsync(context, [node.AttachedObject.Id], folder.Id).ConfigureAwait(true);
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
        var context = this.CreateCommandContext();
        if (context is null || anchor is not SceneNodeAdapter node)
        {
            return;
        }

        this.suppressNodeMessages = true;
        try
        {
            var result = await this.commandService.ReparentNodesAsync(context, [node.AttachedObject.Id], newParentNodeId: null, preserveWorldTransform: false).ConfigureAwait(true);
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

    private async void OnDocumentActivated(object? sender, DocumentActivatedEventArgs e)
    {
        if (e.WindowId.Value != this.windowId.Value)
        {
            return;
        }

        // Only react to scene documents
        var document = this.documentService.GetOpenDocuments(this.windowId).FirstOrDefault(d => d.DocumentId == e.DocumentId);
        if (document is not SceneDocumentMetadata sceneMetadata || sceneMetadata.IsSceneLoadPending)
        {
            return;
        }

        // If we're already showing this scene, or already loading it, do nothing
        if (this.Scene?.AttachedObject.Id == sceneMetadata.DocumentId || this.loadingDocumentId == sceneMetadata.DocumentId)
        {
            return;
        }

        // Find the scene in the current project
        var scene = this.projectManager.CurrentProject?.Scenes.FirstOrDefault(s => s.Id == sceneMetadata.DocumentId);
        if (scene is null)
        {
            return;
        }

        this.LogDocumentActivated(e.DocumentId);

        // Scene replacement belongs to the document owner; tab activation must not bypass its
        // source staging, close guards or previous-graph retirement.
        var request = this.messenger.Send(new Oxygen.Editor.ContentBrowser.Messages.OpenSceneRequestMessage(scene));
        if (request.HasReceivedResponse)
        {
            _ = await request.Response.ConfigureAwait(true);
        }
    }

    private void OnDocumentOpened(object? sender, DocumentOpenedEventArgs e)
    {
        // Background document opens must not load scenes; OnDocumentActivated drives the
        // active-scene load. Material/inspection tabs and background opens leave the current
        // loaded scene untouched.
        _ = sender;
        _ = e;
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "The authoring operation boundary preserves committed state and reports failures to the editor instead of terminating the command loop.")]
    private async Task InitializeLoadedSceneAsync(Scene loadedScene)
    {
        var previousScene = this.Scene?.AttachedObject;
        _ = Interlocked.Increment(ref this.searchGeneration);

        // A loaded scene starts a fresh view session: a previous scene's transient search
        // expansions and filter predicate must not leak into it, and their adapters are retired
        // anyway, so restore could never find them.
        this.FilterPredicate = null;
        this.SearchText = string.Empty;
        this.SearchResultCount = 0;
        this.searchExpandedFolderIds.Clear();
        this.searchExpandedNodeIds.Clear();

        // Build the scene layout from the loaded scene
        this.Scene = new SceneAdapter(loadedScene)
        {
            IsExpanded = true,
            IsLocked = true,
            IsRoot = true,
            UseLayoutAdapters = true,
        };
        this.NotifyCategoryAvailabilityChanged();

        // Update Undo/Redo stacks for the new scene
        this.UndoStack = this.History.UndoStack;
        this.RedoStack = this.History.RedoStack;

        // The refill clears the previous scene's selected rows; keep that inside a sync window so
        // it cannot publish over the stored document selection, then restore by identity outside it.
        await this.WithProjectionSyncAsync(async () =>
        {
            await this.InitializeRootAsync(this.Scene, skipRoot: false).ConfigureAwait(true);
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
        _ = this.selectionService.Reconcile(loadedScene.Id, loadedScene);
        await this.ApplySelectionContextAsync(
            this.selectionService.GetContext(loadedScene.Id),
            forcePublish: true).ConfigureAwait(true);

        if (!ReferenceEquals(previousScene, loadedScene))
        {
            this.ClearCutMarks();
            this.ClipboardItemStore = [];
        }

        if ((this.clipboardNodeIds.Count > 0 || this.explorerClipboard is not null) && !this.ClipboardLifetimeIsCurrent())
        {
            this.InvalidateClipboard(this.clipboardIsCut
                ? "The staged Cut was cancelled because its scene lifetime ended."
                : "The clipboard was cleared because the project changed.");
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
        var documentMetadata = this.documentService.GetOpenDocuments(this.windowId)
            .OfType<SceneDocumentMetadata>()
            .FirstOrDefault(document => document.DocumentId == scene.Id);
        if (documentMetadata is null)
        {
            return false;
        }

        this.loadingDocumentId = scene.Id;
        try
        {
            var ct = await this.BeginSceneLoadAsync().ConfigureAwait(true);

            var loadedScene = this.sceneEngineSync.GetDocumentScene(documentMetadata)
                ?? await this.projectManager.LoadSceneAsync(scene).ConfigureAwait(true);
            if (loadedScene is null)
            {
                return false;
            }

            // Trace: scene data successfully loaded from project storage
            this.LogSceneLoaded(loadedScene.Id, loadedScene.Name ?? "<unnamed>");

            this.nextEntityIndex = loadedScene.AllNodes.Count();

            return !ct.IsCancellationRequested
                && await this.InstallLoadedSceneAsync(loadedScene, documentMetadata, ct).ConfigureAwait(true);
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
            if (this.loadingDocumentId == scene.Id)
            {
                this.loadingDocumentId = Guid.Empty;
            }
        }
    }

    private void OnSceneSynchronized(object? sender, SceneSynchronizationCompletedEventArgs args)
    {
        if (this.isDisposed || this.loadSceneCts?.IsCancellationRequested == true
            || this.documentService.GetActiveDocumentId(this.windowId) != args.Metadata.DocumentId
            || !ReferenceEquals(this.Scene?.AttachedObject, args.Scene)
            || !this.documentService.GetOpenDocuments(this.windowId).Any(metadata => ReferenceEquals(metadata, args.Metadata)))
        {
            return;
        }

        this.LogSceneLoadedMessageSent(args.Scene.Id, DateTime.UtcNow);
        _ = this.messenger.Send(new SceneLoadedMessage(args.Scene));
    }

    private void OnSceneNodeSelectionRequested(object recipient, SceneNodeSelectionRequestMessage message)
    {
        _ = recipient;

        if (this.Scene is null)
        {
            message.Reply(Array.Empty<SceneNode>());
            return;
        }

        message.Reply([.. this.selectionService.GetSelectedNodes(this.Scene.AttachedObject.Id, this.Scene.AttachedObject)]);
    }

    private async Task<bool> InspectNodeAsync(Guid nodeId)
    {
        if (this.Scene is null || this.projection.GetNode(nodeId) is null)
        {
            return false;
        }

        await this.ClearSearchAsync().ConfigureAwait(true);
        return await this.SetSelectedNodes([nodeId], focusPrimary: true).ConfigureAwait(true);
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
                && node.Name.Contains(query, StringComparison.OrdinalIgnoreCase))
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

            if (item is not SceneNodeAdapter and not FolderAdapter)
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
            this.IsIncludedByCategories(node) && node.Name.Contains(query, StringComparison.OrdinalIgnoreCase));
        var matchingFolderCount = this.projection.LayoutFolders.Count(folder =>
            folder.Name.Contains(query, StringComparison.OrdinalIgnoreCase));
        this.SearchResultCount = matchingNodeCount + matchingFolderCount;
    }

    private bool IsCurrentSearch(int generation, SceneAdapter sceneAdapter)
        => generation == Volatile.Read(ref this.searchGeneration)
            && !this.isDisposed
            && ReferenceEquals(this.Scene, sceneAdapter);

    private async Task ExpandMatchAncestryAsync(
        Guid nodeId,
        Dictionary<Guid, SceneNode> nodeById,
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
        for (var parent = nodeById[nodeId].Parent; parent is not null; parent = parent.Parent)
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
        if (!this.IsCurrentSearch(generation, sceneAdapter) || ancestor.Id is not { } ancestorId)
        {
            return false;
        }

        ITreeItem? item = ancestor.IsFolder ? this.projection.GetFolder(ancestorId) : this.projection.GetNode(ancestorId);
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
        else
        {
            _ = this.searchExpandedNodeIds.Add(ancestorId);
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
        var nodes = items.OfType<SceneNodeAdapter>().Select(adapter => adapter.AttachedObject).ToList();

        this.suppressSelectionSync = true;
        try
        {
            this.selectionService.Publish(sceneAdapter.AttachedObject.Id, context, "SceneExplorer");
        }
        finally
        {
            this.suppressSelectionSync = false;
        }

        this.lastPublishedContext = context;
        _ = this.messenger.Send(new SceneNodeSelectionChangedMessage([.. nodes], context, sceneAdapter.AttachedObject.Id));
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
            || this.Scene is not { } sceneAdapter || sceneAdapter.AttachedObject.Id != args.DocumentId)
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
            _ = await this.ApplySelectionContextAsync(args.Context).ConfigureAwait(true);
        }
        catch (Exception exception)
        {
            LogSelectionApplyFailed(this.logger, exception, args.DocumentId);
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
        var documentId = sceneAdapter.AttachedObject.Id;
        var targets = await this.ResolveSelectionTargetsAsync(sceneAdapter, context).ConfigureAwait(true);

        if (!this.IsCurrentSelectionApplication(generation, sceneAdapter, documentId, context))
        {
            return false;
        }

        var primary = FindPrimaryRow(targets, context);

        // True when every identity the context names resolves to one of the applied rows.
        var fullyResolved =
            context.SelectedNodeIds.Count == targets.OfType<SceneNodeAdapter>().Count()
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

        foreach (var nodeId in context.SelectedNodeIds)
        {
            if (this.projection.GetNode(nodeId) is { } nodeAdapter)
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
        Guid documentId,
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

        if (context.PrimaryNodeId is { } nodeId)
        {
            return targets.FirstOrDefault(target => target is SceneNodeAdapter node && node.AttachedObject.Id == nodeId);
        }

        return null;
    }

    internal static SceneSelectionContext BuildSelectionContext(IReadOnlyList<ITreeItem> items, ITreeItem? activeItem = null)
    {
        if (items.Count == 0)
        {
            return SceneSelectionContext.Empty;
        }

        var hasScene = items.Any(static item => item is SceneAdapter);
        var hasFolder = items.Any(static item => item is FolderAdapter);
        var hasNode = items.Any(static item => item is SceneNodeAdapter);
        var kind = SceneSelectionContext.Classify(hasScene, hasFolder, hasNode);

        var nodeIds = items.OfType<SceneNodeAdapter>().Select(adapter => adapter.AttachedObject.Id).ToList();
        var folderIds = items.OfType<FolderAdapter>().Select(folder => folder.Id).ToList();

        // The explicit active row is the primary identity; selection order is only the fallback
        // when no interactive selection established an active row (programmatic restores).
        var primary = activeItem is not null && items.Contains(activeItem) ? activeItem : items[^1];

        return new SceneSelectionContext(
            kind,
            nodeIds,
            folderIds,
            primary is SceneNodeAdapter primaryNode ? primaryNode.AttachedObject.Id : null,
            primary is FolderAdapter primaryFolder ? primaryFolder.Id : null);
    }

    private List<ITreeItem> GetSelectedItems()
        => this.SelectionModel switch
        {
            null => [],
            MultipleSelectionModel multi2 => [.. multi2.SelectedIndices.Select(this.GetShownItemAt)],
            SingleSelectionModel when this.SelectedItem is not null => [this.SelectedItem],
            _ => [],
        };

    /// <summary>
    /// The scene root row is exclusive: selecting it alone is the scene context, and it can never
    /// share a selection with node or folder rows (Explorer contract, section 4 of the plan).
    /// </summary>
    /// <inheritdoc />
    protected override bool AllowsCoSelection(ITreeItem candidate, IReadOnlyList<ITreeItem> currentlySelected)
        => candidate is not SceneAdapter && !currentlySelected.Any(static item => item is SceneAdapter);

    /// <inheritdoc />
    protected override bool IsIncludedInBulk(ITreeItem item) => item is not SceneAdapter;

    private void OnSceneNodeAdded(object recipient, SceneNodeAddedMessage message)
    {
        _ = recipient;
        this.RefreshCategoryPresentation();
        _ = this.RefreshProjectionForNodesAsync(message.Nodes);
    }

    private void OnSceneNodeRemoved(object recipient, SceneNodeRemovedMessage message)
    {
        _ = recipient;
        this.RefreshCategoryPresentation();
        _ = this.RefreshProjectionForNodesAsync(message.Nodes);
    }

    private void OnComponentAdded(object recipient, ComponentAddedMessage message)
    {
        _ = recipient;
        if (message.Added && this.Scene?.AttachedObject.Id == message.Node.Scene.Id)
        {
            this.RefreshCategoryPresentation();
        }
    }

    private void OnComponentRemoved(object recipient, ComponentRemovedMessage message)
    {
        _ = recipient;
        if (message.Removed && this.Scene?.AttachedObject.Id == message.Node.Scene.Id)
        {
            this.RefreshCategoryPresentation();
        }
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "Messenger callbacks report projection failures instead of losing unobserved task exceptions.")]
    private async Task RefreshProjectionForNodesAsync(IList<SceneNode> nodes)
    {
        if (this.isDisposed || this.suppressNodeMessages || this.Scene is not { } sceneAdapter
            || !nodes.Any(node => ReferenceEquals(node.Scene, sceneAdapter.AttachedObject)))
        {
            return;
        }

        try
        {
            await this.ReconcileProjectionAsync().ConfigureAwait(true);
        }
        catch (Exception ex)
        {
            LogProjectionUpdateFailed(this.logger, ex, sceneAdapter.AttachedObject.Id);
        }
    }

    [RelayCommand(CanExecute = nameof(CanCreateFolder))]
    private async Task CreateFolder()
    {
        var context = this.CreateCommandContext();
        if (context is null)
        {
            return;
        }

        this.LogCreateFolderInvoked(this.SelectionModel?.GetType().Name, this.ShownItemsCount);

        var target = this.GetSingleSelectionTarget();
        Guid? parentFolderId = target switch { FolderAdapter folder => folder.Id, _ => null };
        Guid? parentNodeId = target switch { SceneNodeAdapter node => node.AttachedObject.Id, _ => null };

        this.suppressNodeMessages = true;
        try
        {
            var result = await this.commandService.CreateFolderAsync(context, parentFolderId, parentNodeId, "New Folder").ConfigureAwait(true);
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

    private SceneDocumentCommandContext? CreateCommandContext()
    {
        if (this.Scene is null)
        {
            return null;
        }

        var scene = this.Scene.AttachedObject;
        if (this.documentService.GetOpenDocuments(this.windowId).OfType<SceneDocumentMetadata>()
            .Any(metadata => metadata.DocumentId == scene.Id && metadata.IsSceneLoadPending))
        {
            return null;
        }

        var metadata = this.documentService.GetOpenDocuments(this.windowId)
            .OfType<SceneDocumentMetadata>()
            .FirstOrDefault(document => document.DocumentId == scene.Id);
        return metadata is null
            ? null
            : new SceneDocumentCommandContext(scene.Id, metadata, scene, this.History);
    }

    private SceneAuthoringGate.Operation? EnterTreeAuthoring(ITreeItem? item = null)
    {
        if (this.isDisposed || this.Scene?.AttachedObject is not { } scene)
        {
            return null;
        }

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

        return SceneAuthoringGate.TryEnter(scene);
    }

    private async Task<bool> InstallLoadedSceneAsync(Scene loadedScene, SceneDocumentMetadata documentMetadata, CancellationToken ct)
    {
        if (!this.sceneEngineSync.RegisterDocument(loadedScene, documentMetadata))
        {
            return false;
        }

        await this.InitializeLoadedSceneAsync(loadedScene).ConfigureAwait(true);

        if (ct.IsCancellationRequested)
        {
            return false;
        }

        _ = this.messenger.Send(new SceneAuthoringLoadedMessage(loadedScene, documentMetadata));
        _ = await this.sceneEngineSync.SyncSceneWhenReadyAsync(loadedScene, ct).ConfigureAwait(true);
        return !ct.IsCancellationRequested
            && this.documentService.GetOpenDocuments(this.windowId).Any(metadata => ReferenceEquals(metadata, documentMetadata))
            && ReferenceEquals(this.Scene?.AttachedObject, loadedScene);
    }

    private async Task<bool> ApplyReloadedSceneAsync(SceneReloadedMessage message)
        => ReferenceEquals(this.sceneEngineSync.GetDocumentScene(message.Metadata), message.Scene)
            && await this.LoadSceneAsync(message.Scene).ConfigureAwait(true)
            && ReferenceEquals(this.Scene?.AttachedObject, message.Scene);

    /// <inheritdoc />
    public override async Task<TreeDropResult> CommitDropAsync(TreeDropRequest request)
    {
        ArgumentNullException.ThrowIfNull(request);

        if (request.Items.Count == 0)
        {
            return TreeDropResult.Rejected;
        }

        var context = this.CreateCommandContext();
        if (context is null)
        {
            return TreeDropResult.Rejected;
        }

        var folderIds = request.Items.OfType<FolderAdapter>().Select(folder => folder.Id).ToList();
        var nodeIds = request.Items.OfType<SceneNodeAdapter>().Select(adapter => adapter.AttachedObject.Id).ToList();

        if (request.Operation == TreeDropOperation.Copy)
        {
            // Ctrl-drag duplicates through the same deep-copy command used by Paste without
            // touching the clipboard. Folder copy is not yet supported.
            if (nodeIds.Count == 0)
            {
                return TreeDropResult.Rejected;
            }

            var (copyParentNodeId, copyParentFolderId) = ResolveDropDestination(request.Parent);
            IReadOnlyList<SceneNode>? createdNodes;
            this.suppressNodeMessages = true;
            try
            {
                var duplicate = await this.commandService.DuplicateNodesAsync(context, nodeIds, copyParentNodeId, copyParentFolderId).ConfigureAwait(true);
                if (!duplicate.Succeeded)
                {
                    return TreeDropResult.Rejected;
                }

                createdNodes = duplicate.Value;
                await this.ReconcileProjectionAsync().ConfigureAwait(true);
            }
            finally
            {
                this.suppressNodeMessages = false;
            }

            var created = createdNodes!
                .Select(node => (ITreeItem)this.projection.GetNode(node.Id)!)
                .ToList();
            return TreeDropResult.Committed(created);
        }

        SceneCommandResult result;
        if (folderIds.Count > 0)
        {
            // The command owner resolves destination folder lineage and performs any required reparent.
            if (folderIds.Count != 1 || nodeIds.Count != 0)
            {
                return TreeDropResult.Rejected;
            }

            var newParentFolderId = request.Parent is FolderAdapter targetFolder ? targetFolder.Id : (Guid?)null;
            result = await this.commandService.MoveFolderToParentAsync(context, folderIds[0], newParentFolderId).ConfigureAwait(true);
        }
        else if (nodeIds.Count == 1 && this.IsSameParentReorder(request, nodeIds[0]))
        {
            var (parentNodeId, parentFolderId) = ResolveDropDestination(request.Parent);
            result = await this.commandService.ReorderNodesAsync(context, nodeIds[0], parentFolderId, parentNodeId, request.Index).ConfigureAwait(true);
        }
        else
        {
            result = request.Parent switch
            {
                FolderAdapter folder => await this.commandService.MoveNodesToFolderAsync(context, nodeIds, folder.Id).ConfigureAwait(true),
                SceneNodeAdapter node => await this.commandService.ReparentNodesAsync(context, nodeIds, node.AttachedObject.Id, preserveWorldTransform: false).ConfigureAwait(true),
                _ => await this.commandService.ReparentNodesAsync(context, nodeIds, newParentNodeId: null, preserveWorldTransform: false).ConfigureAwait(true),
            };
        }

        if (!result.Succeeded)
        {
            return TreeDropResult.Rejected;
        }

        await this.ReconcileProjectionAsync().ConfigureAwait(true);

        var moved = nodeIds
            .Where(this.projection.ContainsNode)
            .Select(id => (ITreeItem)this.projection.GetNode(id)!)
            .Concat(folderIds.Where(this.projection.ContainsFolder).Select(id => (ITreeItem)this.projection.GetFolder(id)!))
            .ToList();
        return TreeDropResult.Committed(moved);
    }

    private static (Guid? ParentNodeId, Guid? ParentFolderId) ResolveDropDestination(ITreeItem parent)
        => parent switch
        {
            FolderAdapter folder => (null, folder.Id),
            SceneNodeAdapter node => (node.AttachedObject.Id, null),
            _ => (null, null),
        };

    private bool IsSameParentReorder(TreeDropRequest request, Guid nodeId)
    {
        if (this.projection.GetNode(nodeId) is not { } adapter)
        {
            return false;
        }

        var sceneParentId = adapter.AttachedObject.Parent?.Id;
        return request.Parent switch
        {
            SceneAdapter => sceneParentId is null,
            SceneNodeAdapter node => sceneParentId == node.AttachedObject.Id,
            _ => false,
        };
    }

    /// <summary>Returns the folder adapter for the given folder identity, or <see langword="null"/>.</summary>
    /// <param name="folderId">The folder to look up.</param>
    /// <returns>A task completing with the adapter, or <see langword="null"/> when not realized.</returns>
    public Task<FolderAdapter?> FindFolderAdapterAsync(Guid folderId)
        => Task.FromResult(this.projection.GetFolder(folderId));

    private async Task ReconcileProjectionAsync()
    {
        if (this.Scene is not { } sceneAdapter)
        {
            return;
        }

        // Replacing adapters must not publish intermediate empty selection over the store.
        await this.WithProjectionSyncAsync(async () =>
        {
            var expandedFolderIds = sceneAdapter.GetExpandedFolderIds();
            await sceneAdapter.ReloadChildrenAsync(
                expandedFolderIds,
                preserveNodeExpansion: true,
                transientlyExpandedFolderIds: sceneAdapter.GetExpandedFolderIds(transientOnly: true)).ConfigureAwait(true);

            await this.InitializeRootAsync(sceneAdapter, skipRoot: false).ConfigureAwait(true);
            this.projection.Rebuild(sceneAdapter);
            this.ApplyWorkspaceInteractionState();
            _ = this.selectionService.Reconcile(sceneAdapter.AttachedObject.Id, sceneAdapter.AttachedObject);

            // Re-apply the recorded transient search expansion on the fresh adapters (folders were
            // re-expanded as transient during the reload; node seats need their runtime expansion).
            foreach (var nodeId in this.searchExpandedNodeIds.ToArray())
            {
                if (this.projection.GetNode(nodeId) is { IsExpanded: false } node)
                {
                    await this.ExpandItemAsync(node).ConfigureAwait(true);
                }
            }
        }).ConfigureAwait(true);

        // A foreign writer may change selection while the refill awaits. Restore the latest
        // authoritative context, never the snapshot from before the refill.
        if (ReferenceEquals(this.Scene, sceneAdapter))
        {
            await this.ApplySelectionContextAsync(this.selectionService.GetContext(sceneAdapter.AttachedObject.Id)).ConfigureAwait(true);
        }
    }

    /// <summary>
    /// Loads the stored hide and lock state for a freshly initialized scene and applies it.
    /// </summary>
    /// <param name="loadedScene">The scene model that just became active.</param>
    /// <returns>The task completing once the restored state is visible on the rows.</returns>
    /// <remarks>
    /// Without this the persistence is write-only: state saved by a previous session would never
    /// come back. A project-less explorer (no <see cref="IProjectContextService"/> composed, or no
    /// active project) simply starts clean, and in-session toggles still work.
    /// </remarks>
    private async Task RestoreWorkspaceInteractionAsync(Scene loadedScene)
    {
        if (this.interaction is not { } service)
        {
            return;
        }

        if (this.projectContexts?.ActiveProject is not { } project)
        {
            return;
        }

        await service.RestoreAsync(project, loadedScene.Id).ConfigureAwait(true);
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
            var isExplicitlyHidden = service.IsHidden(nodeId);
            node.IsHiddenInEditor = isExplicitlyHidden;
            node.IsEffectivelyHiddenInEditor = isExplicitlyHidden || this.HasHiddenSceneAncestor(node.AttachedObject, service);
            if (!node.IsRoot)
            {
                node.IsLocked = service.IsLocked(nodeId);
            }
        }

        // Logical folders group nodes; they are never part of the scene-hide closure (contract R1).
        foreach (var folder in this.projection.Folders)
        {
            folder.IsEffectivelyHiddenInEditor = false;
        }
    }

    /// <summary>Walks the actual scene ancestry, which is the only chain that can hide a node.</summary>
    /// <param name="node">The scene node to test.</param>
    /// <param name="service">The workspace interaction owner.</param>
    /// <returns><see langword="true"/> when an ancestor of <paramref name="node"/> is hidden.</returns>
    private bool HasHiddenSceneAncestor(SceneNode node, Workspace.WorkspaceInteractionService service)
    {
        for (var parent = node.Parent; parent is not null; parent = parent.Parent)
        {
            if (service.IsHidden(parent.Id))
            {
                return true;
            }
        }

        return false;
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

    private void ApplyCategoryFilters(Workspace.SceneCategories categories)
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

        var ids = this.ResolveWorkspaceTargetIds(anchor);
        if (ids.Count == 0 || this.CreateCommandContext() is not { } context)
        {
            return;
        }

        // One intent, one undo step: the batch target is decided from the first row so a mixed
        // selection hides or shows uniformly, the way Delete and Reparent batches already do.
        var hide = !service.IsHidden(ids[0]);
        _ = await this.commandService.SetEditorHiddenAsync(context, ids, hide).ConfigureAwait(true);
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

        foreach (var nodeId in this.ResolveWorkspaceTargetIds(anchor))
        {
            service.SetLocked(nodeId, !service.IsLocked(nodeId));
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

        var hidden = service.HiddenNodeIds();
        if (hidden.Count == 0)
        {
            return;
        }

        var context = this.CreateCommandContext();
        if (context is null)
        {
            return;
        }

        _ = await this.commandService.SetEditorHiddenAsync(context, [.. hidden], hidden: false).ConfigureAwait(true);
    }

    /// <summary>
    /// Resolves the nodes a workspace interaction acts on: the invoked row when one is given,
    /// otherwise the current selection. Folder rows carry no node identity and are skipped.
    /// </summary>
    /// <param name="anchor">The invoked row, or <see langword="null"/> for the selection.</param>
    /// <returns>The stable authored ids to toggle.</returns>
    private IReadOnlyList<Guid> ResolveWorkspaceTargetIds(ITreeItem? anchor)
    {
        if (anchor is SceneNodeAdapter single)
        {
            return [single.AttachedObject.Id];
        }

        return [.. this.GetSelectedItems().OfType<SceneNodeAdapter>().Select(node => node.AttachedObject.Id)];
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
            this.logger.LogWarning("{Action} is unavailable: no workspace interaction service is composed.", action);
            return;
        }

        _ = SceneOperationResults.PublishWarning(
            publisher,
            reducer,
            RuntimeOperationKinds.SettingsApply,
            FailureDomain.Settings,
            DiagnosticCodes.SettingsPrefix + "WORKSPACE_STATE_UNAVAILABLE",
            action + " is unavailable",
            "The workspace interaction service is not composed in this window, so " + action.ToLowerInvariant() + " did nothing.",
            new AffectedScope { DocumentId = this.Scene?.AttachedObject.Id });
    }
}
