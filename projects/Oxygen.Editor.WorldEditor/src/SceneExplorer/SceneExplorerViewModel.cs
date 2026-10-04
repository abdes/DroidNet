// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Collections.Specialized;
using System.ComponentModel;
using System.Diagnostics;
using System.Windows.Input;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Controls;
using DroidNet.Controls.Menus;
using DroidNet.Controls.Selection;
using DroidNet.Documents;
using DroidNet.Routing;
using DroidNet.TimeMachine;
using DroidNet.TimeMachine.Changes;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.World.Messages;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Services;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Oxygen.Editor.WorldEditor.Documents.Selection;

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
    private readonly SceneExplorerProjection projection = new();

    // Node-clipboard state: copied node identities plus whether they were cut (moved) rather than copied.
    private readonly List<Guid> clipboardNodeIds = [];
    private readonly List<SceneNodeData> clipboardSnapshots = [];
    private bool clipboardIsCut;

    // Adapters expanded by a transient search so their expansion can be restored when search clears.
    private readonly List<ITreeItem> searchExpandedItems = [];
    private int nextEntityIndex;
    private CancellationTokenSource? loadSceneCts;
    private Guid loadingDocumentId = Guid.Empty;
    private bool suppressNodeMessages;

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
    public SceneExplorerViewModel(
        IProjectManagerService projectManager,
        IMessenger messenger,
        IRouter router,
        IDocumentService documentService,
        WindowId windowId,
        ISceneEngineSync sceneEngineSync,
        ISceneSelectionService selectionService,
        ISceneDocumentCommandService commandService,
        ILoggerFactory? loggerFactory = null)
        : base(loggerFactory)
    {
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
        this.commandService = commandService;

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

    private HistoryKeeper History => this.Scene != null ? UndoRedo.GetHistory(this.Scene.AttachedObject.Id) : UndoRedo.Default[this];

    /// <inheritdoc />
    [RelayCommand(CanExecute = nameof(SceneExplorerViewModel.HasUnlockedSelectedItems))]
    public override async Task RemoveSelectedItems()
    {
        var context = this.CreateCommandContext();
        if (context is null)
        {
            return;
        }

        var selectedItems = this.GetSelectedItems();
        var nodeIds = selectedItems.OfType<SceneNodeAdapter>().Select(adapter => adapter.AttachedObject.Id).ToList();
        var folderIds = selectedItems.OfType<FolderAdapter>().Select(folder => folder.Id).ToList();
        if (nodeIds.Count == 0 && folderIds.Count == 0)
        {
            return;
        }

        this.suppressNodeMessages = true;
        try
        {
            var result = await this.commandService.DeleteItemsAsync(context, nodeIds, folderIds).ConfigureAwait(true);
            if (!result.Succeeded)
            {
                return;
            }

            await this.ReconcileProjectionAsync().ConfigureAwait(true);
        }
        finally
        {
            this.suppressNodeMessages = false;
        }
    }

    /// <inheritdoc />
    public override async Task<TreeItemRenameResult> CommitRenameAsync(ITreeItem item, string newName)
    {
        ArgumentNullException.ThrowIfNull(item);

        var context = this.CreateCommandContext();
        if (context is null)
        {
            return TreeItemRenameResult.Rejected("No scene document is loaded.");
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

        SceneCommandResult result = item switch
        {
            SceneNodeAdapter node => await this.commandService.RenameNodeAsync(context, node.AttachedObject.Id, trimmed).ConfigureAwait(true),
            FolderAdapter folder => await this.RenameFolderAsync(context, folder, trimmed).ConfigureAwait(true),
            _ => new SceneCommandResult(Succeeded: false),
        };

        return result.Succeeded
            ? TreeItemRenameResult.Success
            : TreeItemRenameResult.Rejected(result.ValidationMessage ?? "The rename was rejected.");
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
            this.SelectionModel?.ClearSelection();
            this.PublishSelection([]);
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
            this.sceneEngineSync.SceneSynchronized -= this.OnSceneSynchronized;
            this.documentService.DocumentOpened -= this.OnDocumentOpened;
            this.documentService.DocumentActivated -= this.OnDocumentActivated;
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
        => this.SelectionModel is SingleSelectionModel { SelectedItem.IsLocked: false }
            || (this.SelectionModel is MultipleSelectionModel<ITreeItem> m
            && m.SelectedIndices.Count == 1
            && !m.SelectedItems[0].IsLocked);

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
        var adapters = items.OfType<SceneNodeAdapter>().ToList();
        if (adapters.Count == 0)
        {
            return Task.CompletedTask;
        }

        this.clipboardNodeIds.Clear();
        this.clipboardSnapshots.Clear();
        foreach (var adapter in adapters)
        {
            this.clipboardNodeIds.Add(adapter.AttachedObject.Id);
            this.clipboardSnapshots.Add(adapter.AttachedObject.Dehydrate());
        }

        this.clipboardIsCut = false;

        this.ClipboardItemStore = [.. items];
        this.ClipboardStateStore = ClipboardState.Copied;
        this.ClearCutMarks();
        this.RaiseClipboardChanged();
        return Task.CompletedTask;
    }

    /// <inheritdoc />
    public override Task CutItemsAsync(IReadOnlyList<ITreeItem> items)
    {
        var eligible = items.Where(item => !item.IsLocked).ToArray();
        var nodeIds = eligible.OfType<SceneNodeAdapter>().Select(adapter => adapter.AttachedObject.Id).ToList();
        if (nodeIds.Count == 0)
        {
            return Task.CompletedTask;
        }

        this.clipboardNodeIds.Clear();
        this.clipboardNodeIds.AddRange(nodeIds);
        this.clipboardSnapshots.Clear();
        this.clipboardIsCut = true;

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
        _ = insertIndex; // Oxygen paste appends/regroups via the command owner; the index is not yet honored.
        var context = this.CreateCommandContext();
        if (context is null || this.clipboardNodeIds.Count == 0)
        {
            return;
        }

        // A multi-scope selection has no single destination; reject rather than fall back to the root.
        if (targetParent is null && this.GetSelectedItems().Count > 1)
        {
            return;
        }

        // A single node target pastes as a sibling (after it); a folder target pastes into the
        // folder; the scene root pastes at the root.
        var target = targetParent ?? this.GetSingleSelectionTarget() ?? this.Scene;
        Guid? insertAfterNodeId;
        Guid? parentNodeId;
        Guid? parentFolderId;
        switch (target)
        {
            case SceneNodeAdapter node:
                insertAfterNodeId = node.AttachedObject.Id;
                parentNodeId = node.AttachedObject.Parent?.Id;
                parentFolderId = null;
                break;
            case FolderAdapter folder:
                insertAfterNodeId = null;
                parentNodeId = null;
                parentFolderId = folder.Id;
                break;
            case SceneAdapter:
                insertAfterNodeId = null;
                parentNodeId = null;
                parentFolderId = null;
                break;
            default:
                return;
        }

        this.suppressNodeMessages = true;
        try
        {
            if (this.clipboardIsCut)
            {
                var result = await this.commandService.ReparentNodesAsync(context, this.clipboardNodeIds, parentNodeId, preserveWorldTransform: false, insertAfterNodeId).ConfigureAwait(true);
                if (!result.Succeeded)
                {
                    return;
                }
            }
            else
            {
                var result = await this.commandService.DuplicateNodesFromDataAsync(context, this.clipboardSnapshots, parentNodeId, parentFolderId, insertAfterNodeId).ConfigureAwait(true);
                if (!result.Succeeded)
                {
                    return;
                }
            }

            await this.ReconcileProjectionAsync().ConfigureAwait(true);

            // A completed Cut (move) clears the staging; a Copy payload is retained for repeated Paste.
            if (this.clipboardIsCut)
            {
                this.clipboardNodeIds.Clear();
                this.clipboardIsCut = false;
                this.ClipboardStateStore = ClipboardState.Empty;
                this.ClearCutMarks();
                this.RaiseClipboardChanged();
            }
        }
        finally
        {
            this.suppressNodeMessages = false;
        }
    }

    [RelayCommand(CanExecute = nameof(CanCopy))]
    private Task Copy() => this.CopyItemsAsync(this.GetSelectedItems());

    private bool CanCopy() => this.GetSelectedItems().OfType<SceneNodeAdapter>().Any();

    [RelayCommand(CanExecute = nameof(CanCut))]
    private Task Cut() => this.CutItemsAsync(this.GetSelectedItems());

    private bool CanCut() => this.HasUnlockedSelectedItems;

    [RelayCommand(CanExecute = nameof(CanPaste))]
    private Task Paste() => this.PasteItemsAsync(targetParent: null);

    private bool CanPaste() => this.clipboardNodeIds.Count > 0;

    /// <inheritdoc />
    protected override void OnClipboardCleared()
    {
        this.clipboardNodeIds.Clear();
        this.clipboardSnapshots.Clear();
        this.clipboardIsCut = false;
    }

    /// <summary>
    /// Builds the context menu for a captured anchor row, resolving each shared action to its typed command.
    /// </summary>
    /// <param name="anchor">The row that received the context request.</param>
    /// <returns>The menu source for the captured context.</returns>
    public IMenuSource BuildContextMenuSource(ITreeItem anchor)
    {
        var context = BuildSelectionContext(this.GetSelectedItems());
        var primaryIsInFolder = anchor is SceneNodeAdapter node && node.Parent is FolderAdapter;
        var primaryHasChildren = anchor is LayoutItemAdapter { HasChildren: true };
        var primaryIsUnlocked = !anchor.IsLocked;

        var entries = SceneExplorerContextMenu.Build(context.Kind, primaryIsInFolder, primaryHasChildren, primaryIsUnlocked);

        var builder = new MenuBuilder();
        foreach (var entry in entries)
        {
            _ = builder.AddMenuItem(entry.Label, this.ResolveMenuCommand(entry.Kind, anchor, entry.IsEnabled));
        }

        return builder.Build();
    }

    private ICommand ResolveMenuCommand(SceneExplorerCommandKind kind, ITreeItem anchor, bool isEnabled)
    {
        switch (kind)
        {
            case SceneExplorerCommandKind.NewNode:
                return this.AddEntityCommand;
            case SceneExplorerCommandKind.NewFolder:
                return this.CreateFolderCommand;
            case SceneExplorerCommandKind.Rename:
                return this.RenameSelectedCommand;
            case SceneExplorerCommandKind.Cut:
                return this.CutCommand;
            case SceneExplorerCommandKind.Copy:
                return this.CopyCommand;
            case SceneExplorerCommandKind.Paste:
            case SceneExplorerCommandKind.PasteAsChild:
                return this.PasteCommand;
            case SceneExplorerCommandKind.Delete:
                return this.RemoveSelectedItemsCommand;
            case SceneExplorerCommandKind.RemoveFromFolder:
                return new SceneExplorerCommandAdapter(() => _ = this.RemoveFromFolderAsync(anchor), () => isEnabled);
            case SceneExplorerCommandKind.MoveToSceneRoot:
                return new SceneExplorerCommandAdapter(() => _ = this.MoveToSceneRootAsync(anchor), () => isEnabled);
            case SceneExplorerCommandKind.Expand:
                return new SceneExplorerCommandAdapter(() => _ = this.ExpandItemAsync(anchor), () => anchor is { IsExpanded: false, CanAcceptChildren: true });
            case SceneExplorerCommandKind.Collapse:
                return new SceneExplorerCommandAdapter(() => _ = this.CollapseItemAsync(anchor), () => anchor.IsExpanded);
            default:
                return new SceneExplorerCommandAdapter(static () => { }, static () => false);
        }
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
        if (document is not SceneDocumentMetadata sceneMetadata)
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

        // Load/switch the scene
        await this.HandleDocumentOpenedAsync(scene).ConfigureAwait(true);
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
        // Build the scene layout from the loaded scene
        this.Scene = new SceneAdapter(loadedScene)
        {
            IsExpanded = true,
            IsLocked = true,
            IsRoot = true,
            UseLayoutAdapters = true,
        };

        // Update Undo/Redo stacks for the new scene
        this.UndoStack = this.History.UndoStack;
        this.RedoStack = this.History.RedoStack;

        await this.InitializeRootAsync(this.Scene, skipRoot: false).ConfigureAwait(true);
        this.projection.Rebuild(this.Scene);
        this.PublishSelection(this.selectionService.Reconcile(loadedScene.Id, loadedScene));
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
            || !ReferenceEquals(this.CreateCommandContext()?.Metadata, args.Metadata))
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
        if (this.Scene is null || this.projection.GetNode(nodeId) is not { } adapter)
        {
            return false;
        }

        this.FilterPredicate = null;
        var ancestorPath = new Stack<ITreeItem>();
        for (var parent = adapter.Parent; parent is not null; parent = parent.Parent)
        {
            ancestorPath.Push(parent);
        }

        while (ancestorPath.TryPop(out var ancestor))
        {
            if (ancestor.CanAcceptChildren && !ancestor.IsExpanded)
            {
                await this.ExpandItemAsync(ancestor).ConfigureAwait(true);
            }
        }

        if (!this.ShownItems.Contains(adapter))
        {
            return false;
        }

        this.SelectionModel?.ClearSelection();
        this.SelectionModel?.SelectItem(adapter);
        return this.GetSelectedItems().Contains(adapter);
    }

    /// <summary>Applies a transient name search, revealing collapsed matches by expanding their ancestor paths.</summary>
    /// <param name="query">The search text; empty or whitespace clears the search.</param>
    /// <returns>The number of matching nodes (excluding context-only ancestors).</returns>
    public async Task<int> SearchAsync(string? query)
    {
        if (this.Scene is null)
        {
            return 0;
        }

        var trimmed = (query ?? string.Empty).Trim();
        if (trimmed.Length == 0)
        {
            await this.ClearSearchAsync().ConfigureAwait(true);
            return 0;
        }

        var scene = this.Scene.AttachedObject;
        var nodeById = scene.AllNodes.ToDictionary(node => node.Id);

        var matchingNodeIds = nodeById.Values
            .Where(node => node.Name.Contains(trimmed, StringComparison.OrdinalIgnoreCase))
            .Select(node => node.Id)
            .ToList();

        // Expand scene-graph ancestor paths of matching nodes (top-down) so collapsed descendants
        // become visible to the filter. Nodes use their actual scene ancestry rather than the
        // realized adapter Parent (unset while collapsed). Accumulate across queries;
        // ClearSearchAsync restores everything a search expanded.
        foreach (var nodeId in matchingNodeIds)
        {
            var ancestors = new Stack<SceneNode>();
            for (var parent = nodeById[nodeId].Parent; parent is not null; parent = parent.Parent)
            {
                ancestors.Push(parent);
            }

            while (ancestors.TryPop(out var ancestorNode))
            {
                if (this.projection.GetNode(ancestorNode.Id) is { } ancestor && !ancestor.IsExpanded)
                {
                    await this.ExpandItemAsync(ancestor).ConfigureAwait(true);
                    this.searchExpandedItems.Add(ancestor);
                }
            }
        }

        // Folders are matched over realized adapters; collapsed-folder matching still requires a
        // domain layout index (C26).
        var matchingFolderCount = this.projection.Folders
            .Count(folder => folder.Name.Contains(trimmed, StringComparison.OrdinalIgnoreCase));

        this.FilterPredicate = item => item.Label.Contains(trimmed, StringComparison.OrdinalIgnoreCase);
        return matchingNodeIds.Count + matchingFolderCount;
    }

    /// <summary>Clears the transient search and restores the expansion state it changed.</summary>
    /// <returns>A task that completes when the search has been cleared.</returns>
    public async Task ClearSearchAsync()
    {
        this.FilterPredicate = null;
        foreach (var item in this.searchExpandedItems)
        {
            if (item.IsExpanded)
            {
                await this.CollapseItemAsync(item).ConfigureAwait(true);
            }
        }

        this.searchExpandedItems.Clear();
    }

    private void OnSingleSelectionChanged(object? sender, PropertyChangedEventArgs args)
    {
        var isSelectionChange = string.Equals(args.PropertyName, nameof(SelectionModel<>.SelectedIndex), StringComparison.Ordinal)
                                || string.Equals(args.PropertyName, "SelectedItem", StringComparison.Ordinal);

        if (!isSelectionChange)
        {
            return;
        }

        this.NotifySelectionDependentCommands();
        this.HasUnlockedSelectedItems = this.SelectionModel?.SelectedItem?.IsLocked == false;
        this.PublishCurrentSelection();
    }

    private void OnMultipleSelectionChanged(object? sender, NotifyCollectionChangedEventArgs args)
    {
        if (this.SelectionModel is not MultipleSelectionModel multipleSelectionModel)
        {
            return;
        }

        this.NotifySelectionDependentCommands();

        var unlockedSelectedItems = false;
        foreach (var index in multipleSelectionModel.SelectedIndices)
        {
            var item = this.GetShownItemAt(index);
            unlockedSelectedItems = !item.IsLocked;
            if (unlockedSelectedItems)
            {
                break;
            }
        }

        this.HasUnlockedSelectedItems = unlockedSelectedItems;
        this.PublishCurrentSelection();
    }

    private void PublishSelection(IReadOnlyList<SceneNode> selected)
    {
        if (this.Scene is not { } sceneAdapter)
        {
            return;
        }

        var sceneId = sceneAdapter.AttachedObject.Id;
        this.selectionService.SetSelection(sceneId, selected, "SceneExplorer");
        this.selectionService.SetContext(
            sceneId,
            selected.Count == 0
                ? SceneSelectionContext.Empty
                : new SceneSelectionContext(
                    SceneSelectionKind.Node,
                    selected.Select(node => node.Id).ToList(),
                    [],
                    selected[^1].Id,
                    null),
            "SceneExplorer");
        _ = this.messenger.Send(new SceneNodeSelectionChangedMessage([.. selected]));
    }

    private void PublishCurrentSelection()
    {
        if (this.Scene is not { } sceneAdapter)
        {
            return;
        }

        var items = this.GetSelectedItems();
        var context = BuildSelectionContext(items);
        var nodes = items.OfType<SceneNodeAdapter>().Select(adapter => adapter.AttachedObject).ToList();

        var sceneId = sceneAdapter.AttachedObject.Id;
        this.selectionService.SetContext(sceneId, context, "SceneExplorer");
        this.selectionService.SetSelection(sceneId, nodes, "SceneExplorer");
        _ = this.messenger.Send(new SceneNodeSelectionChangedMessage([.. nodes]));
    }

    internal static SceneSelectionContext BuildSelectionContext(IReadOnlyList<ITreeItem> items)
    {
        if (items.Count == 0)
        {
            return SceneSelectionContext.Empty;
        }

        var hasScene = items.Any(item => item is SceneAdapter);
        var hasFolder = items.Any(item => item is FolderAdapter);
        var hasNode = items.Any(item => item is SceneNodeAdapter);

        var kind = (hasScene, hasFolder, hasNode) switch
        {
            (true, false, false) => SceneSelectionKind.Scene,
            (false, false, true) => SceneSelectionKind.Node,
            (false, true, false) => SceneSelectionKind.Folder,
            (false, false, false) => SceneSelectionKind.Empty,
            _ => SceneSelectionKind.Mixed,
        };

        var nodeIds = items.OfType<SceneNodeAdapter>().Select(adapter => adapter.AttachedObject.Id).ToList();
        var folderIds = items.OfType<FolderAdapter>().Select(folder => folder.Id).ToList();
        var primary = items[^1];

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

    private async void OnSceneNodeAdded(object recipient, SceneNodeAddedMessage message)
    {
        _ = recipient;
        if (this.suppressNodeMessages || this.Scene is null)
        {
            return;
        }

        foreach (var node in message.Nodes.Where(node => ReferenceEquals(node.Scene, this.Scene.AttachedObject)))
        {
            if (this.projection.ContainsNode(node.Id))
            {
                continue;
            }

            var parent = node.Parent is null
                ? this.Scene
                : this.projection.GetNode(node.Parent.Id) as ITreeItem ?? this.Scene;

            var adapter = new SceneNodeAdapter(node);
            await this.ApplyExternalTreeChangeAsync(async () => await this.InsertItemAsync(adapter, parent, 0).ConfigureAwait(true)).ConfigureAwait(true);
            this.projection.Index(adapter);
            this.projection.Track(adapter);
        }
    }

    private async void OnSceneNodeRemoved(object recipient, SceneNodeRemovedMessage message)
    {
        _ = recipient;
        if (this.suppressNodeMessages || this.Scene is null)
        {
            return;
        }

        foreach (var node in message.Nodes.Where(node => ReferenceEquals(node.Scene, this.Scene.AttachedObject)))
        {
            if (this.projection.GetNode(node.Id) is not { } adapter)
            {
                continue;
            }

            await this.ApplyExternalTreeChangeAsync(async () => await this.RemoveItemAsync(adapter).ConfigureAwait(true)).ConfigureAwait(true);
            this.projection.Unindex(adapter);
            this.projection.Untrack(adapter);
        }
    }

    private async Task ApplyExternalTreeChangeAsync(Func<Task> action)
    {
        using var authoring = this.EnterTreeAuthoring();
        if (authoring is not null)
        {
            await action().ConfigureAwait(true);
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
        return true;
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
            // Folder moves are grouping-only. Support a single-folder drop; reject mixed batches.
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

        var expandedFolderIds = sceneAdapter.GetExpandedFolderIds();
        await sceneAdapter.ReloadChildrenAsync(expandedFolderIds, preserveNodeExpansion: true).ConfigureAwait(true);

        await this.InitializeRootAsync(sceneAdapter, skipRoot: false).ConfigureAwait(true);
        this.projection.Rebuild(sceneAdapter);
    }
}
