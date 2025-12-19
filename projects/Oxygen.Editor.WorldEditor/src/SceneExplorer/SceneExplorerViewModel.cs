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
using Oxygen.Editor.Documents;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Oxygen.Editor.WorldEditor.Messages;
using Oxygen.Editor.WorldEditor.SceneExplorer.Services;
using Oxygen.Editor.WorldEditor.Services;

namespace Oxygen.Editor.WorldEditor.SceneExplorer;

/// <summary>
///     The ViewModel for the <see cref="SceneExplorer.SceneExplorerView" /> view.
/// </summary>
public partial class SceneExplorerViewModel : DynamicTreeViewModel
{
    private readonly IProject currentProject;
    private readonly ILogger<SceneExplorerViewModel> logger;
    private readonly IMessenger messenger;
    private readonly IRouter router;
    private readonly IProjectManagerService projectManager;
    private readonly IDocumentService documentService;
    private readonly WindowId windowId;
    private readonly ISceneEngineSync sceneEngineSync;
    private readonly ISceneExplorerService sceneExplorerService;
    private readonly List<ITreeItem> clipboard = [];

    // Fast lookup of adapters by SceneNode.Id to avoid traversing/initializing the tree during reconciliation.
    private readonly Dictionary<Guid, SceneNodeAdapter> nodeAdapterIndex = [];
    private int nextEntityIndex;
    private CancellationTokenSource? loadSceneCts;

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
    /// <param name="sceneExplorerService">The scene explorer service.</param>
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
        ISceneExplorerService sceneExplorerService,
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
        this.sceneExplorerService = sceneExplorerService;

        Debug.Assert(projectManager.CurrentProject is not null, "must have a current project");
        this.currentProject = projectManager.CurrentProject;

        this.UndoStack = UndoRedo.Default[this].UndoStack;
        this.RedoStack = UndoRedo.Default[this].RedoStack;

        this.ItemBeingRemoved += this.OnItemBeingRemoved;
        this.ItemRemoved += this.OnItemRemoved;

        this.ItemBeingAdded += this.OnItemBeingAdded;
        this.ItemAdded += this.OnItemAdded;
        this.ItemMoved += this.OnItemMoved;

        messenger.Register<SceneNodeSelectionRequestMessage>(this, this.OnSceneNodeSelectionRequested);

        // Default selection mode for Scene Explorer is multiple selection.
        this.SelectionMode = SelectionMode.Multiple;

        // Subscribe to document events to load scene when a scene document is opened
        documentService.DocumentOpened += this.OnDocumentOpened;
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
    public ReadOnlyObservableCollection<IChange> UndoStack { get; }

    /// <summary>
    ///     Gets the redo stack.
    /// </summary>
    public ReadOnlyObservableCollection<IChange> RedoStack { get; }

    /// <inheritdoc />
    [RelayCommand(CanExecute = nameof(SceneExplorerViewModel.HasUnlockedSelectedItems))]
    public override async Task RemoveSelectedItems()
    {
        UndoRedo.Default[this].BeginChangeSet("Remove Selected Items");
        try
        {
            // Delegate to base to update UI (this will trigger OnItemRemoved)
            await base.RemoveSelectedItems().ConfigureAwait(false);
        }
        finally
        {
            UndoRedo.Default[this].EndChangeSet();
        }
    }

    /// <summary>
    ///     Renames the specified <paramref name="item"/> to <paramref name="newName"/> and
    ///     records an undo change so the operation can be undone.
    /// </summary>
    /// <param name="item">The tree item to rename.</param>
    /// <param name="newName">The new name for the item.</param>
    /// <returns>A <see cref="Task"/> that completes when the rename has finished.</returns>
    public async Task RenameItemAsync(ITreeItem item, string newName)
    {
        var oldName = item.Label;
        if (string.Equals(oldName, newName, StringComparison.Ordinal))
        {
            return;
        }

        await this.sceneExplorerService.RenameItemAsync(item, newName).ConfigureAwait(false);

        UndoRedo.Default[this].AddChange(
            $"Rename({oldName} -> {newName})",
            async () => await this.RenameItemAsync(item, oldName).ConfigureAwait(false));
    }

    /// <summary>
    ///     Returns the <see cref="TreeItemAdapter"/> associated with the given scene node <paramref name="nodeId"/>,
    ///     or <see langword="null"/> if no adapter is registered for that id.
    /// </summary>
    /// <param name="nodeId">The id of the scene node to look up.</param>
    /// <returns>A <see cref="Task"/> that returns the adapter or <see langword="null"/>.</returns>
    public Task<TreeItemAdapter?> FindAdapterByNodeIdAsync(Guid nodeId)
        => Task.FromResult<TreeItemAdapter?>(this.nodeAdapterIndex.TryGetValue(nodeId, out var a) ? a : null);

    /// <summary>
    /// Handles document-open actions once the target scene has been resolved.
    /// Kept protected for testability; does no UI-thread dispatching.
    /// </summary>
    /// <param name="scene">Scene to load.</param>
    /// <returns>Task that completes after the scene has been loaded.</returns>
    protected internal virtual async Task HandleDocumentOpenedAsync(Scene scene)
        => await this.LoadSceneAsync(scene).ConfigureAwait(true);

    /// <summary>
    /// Core logic for handling an item added event. Separated for testability.
    /// </summary>
    /// <param name="args">Event arguments.</param>
    /// <returns>A <see cref="Task"/> that completes when the item has been handled.</returns>
    protected internal virtual async Task HandleItemAddedAsync(TreeItemAddedEventArgs args)
    {
        // Add Undo
        // NOTE: We add the undo change *before* any async operations to ensure that if this method
        // is called within an Undo context (which sets the UndoRedo state to Undoing), the new change
        // is correctly pushed to the Redo stack. If we await first, the Undo context might be disposed
        // by the time we reach this line, causing the change to be pushed to the Undo stack instead.
        UndoRedo.Default[this]
            .AddChange(
                $"RemoveItem({args.TreeItem.Label})",
                async () =>
                {
                    try
                    {
                        await this.RemoveItemAsync(args.TreeItem).ConfigureAwait(false);
                    }
                    catch (Exception ex)
                    {
                        this.LogUndoRemoveFailed(ex, args.TreeItem.Label);
                    }
                });

        var addedAdapter = AsSceneNodeAdapter(args.TreeItem);

        // Sync with Backend
        if (addedAdapter != null)
        {
            await this.sceneExplorerService.AddNodeAsync(args.Parent, addedAdapter.AttachedObject).ConfigureAwait(false);
        }
        else if (args.TreeItem is FolderAdapter folderAdapter)
        {
            await this.sceneExplorerService.CreateFolderAsync(args.Parent, folderAdapter.Label, folderAdapter.Id).ConfigureAwait(false);
        }

        this.LogItemAdded(args.TreeItem.Label);

        if (addedAdapter is null)
        {
            return;
        }

        // Register adapter for quick lookup
        try
        {
            this.nodeAdapterIndex[addedAdapter.AttachedObject.Id] = addedAdapter;
        }
        catch
        {
            // FIXME: ignore indexing errors
        }
    }

    /// <summary>
    /// Core logic for handling an item being added. Separated from the event handler for testability.
    /// </summary>
    /// <param name="scene">The scene owning the adapter.</param>
    /// <param name="entityAdapter">The node adapter being added.</param>
    /// <param name="parent">The parent tree item.</param>
    /// <param name="args">Original event args (used to set <see cref="TreeItemBeingAddedEventArgs.Proceed"/> when needed).</param>
    protected internal virtual void HandleItemBeingAdded(Scene scene, SceneNodeAdapter entityAdapter, ITreeItem parent, TreeItemBeingAddedEventArgs args)
    {
        // Validation logic only. The actual move/add is handled by the caller (DynamicTreeViewModel)
        // and then synced in OnItemAdded/OnItemMoved.

        // We can check if the move is valid here.
        // For now, we assume it is valid if the types match (checked in OnItemBeingAdded event handler).
    }

    /// <summary>
    ///     Called when an item is in the process of being removed from the tree.
    ///     This implementation is a no-op because deletion is handled by
    ///     <see cref="RemoveSelectedItems"/> and moves are handled by
    ///     <see cref="OnItemBeingAdded"/> (which covers the add part of a move).
    /// </summary>
    /// <param name="sender">Event sender.</param>
    /// <param name="args">The event arguments describing the removal.</param>
    protected internal virtual void OnItemBeingRemoved(object? sender, TreeItemBeingRemovedEventArgs args)
    {
        // No-op: Deletion is handled by RemoveSelectedItemsCommand.
        // Moves are handled by OnItemBeingAdded (which covers the "Add" part of the move).
    }

    /// <inheritdoc />
    protected override void Dispose(bool disposing)
    {
        if (this.isDisposed)
        {
            return;
        }

        if (disposing)
        {
            this.documentService.DocumentOpened -= this.OnDocumentOpened;
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
            if (oldValue is not null)
            {
                oldValue.PropertyChanged -= this.OnSingleSelectionChanged;
            }

            if (this.SelectionModel is not null)
            {
                this.SelectionModel.PropertyChanged += this.OnSingleSelectionChanged;
            }
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

    private static async Task<FolderAdapter?> FindFolderAdapterAsync(SceneAdapter sceneAdapter, Guid folderId)
    {
        var children = await sceneAdapter.Children.ConfigureAwait(true);
        var stack = new Stack<ITreeItem>((IEnumerable<ITreeItem>?)children ?? []);

        while (stack.Count > 0)
        {
            var item = stack.Pop();
            if (item is FolderAdapter folder && folder.Id == folderId)
            {
                return folder;
            }

            if (item is TreeItemAdapter adapter)
            {
                var nested = await adapter.Children.ConfigureAwait(true);
                if (nested is not null)
                {
                    foreach (var child in nested)
                    {
                        stack.Push(child);
                    }
                }
            }
        }

        return null;
    }

    private static SceneNode? AsSceneNode(ITreeItem? item)
        => item switch
        {
            SceneNodeAdapter lna => lna.AttachedObject,
            _ => null,
        };

    private static Scene? ResolveSceneFromTree(ITreeItem? start)
    {
        var cursor = start;
        while (cursor is not null)
        {
            switch (cursor)
            {
                case SceneAdapter sceneAdapter:
                    return sceneAdapter.AttachedObject;
                case SceneNodeAdapter layoutAdapter:
                    return layoutAdapter.AttachedObject.Scene;
            }

            cursor = cursor.Parent;
        }

        return null;
    }

    [RelayCommand(CanExecute = nameof(CanCut))]
    private void Cut()
    {
        this.Copy();
        _ = this.RemoveSelectedItemsCommand.ExecuteAsync(parameter: null);
    }

    private bool CanCut() => this.HasUnlockedSelectedItems;

    [RelayCommand(CanExecute = nameof(CanCopy))]
    private void Copy()
    {
        this.clipboard.Clear();
        var items = this.GetSelectedItems();
        foreach (var item in items)
        {
            if (item is ICanBeCloned cloneable)
            {
                this.clipboard.Add(cloneable.CloneSelf());
            }
        }

        this.PasteCommand.NotifyCanExecuteChanged();
    }

    private bool CanCopy() => this.GetSelectedItems().Count > 0;

    [RelayCommand(CanExecute = nameof(CanPaste))]
    private async Task Paste()
    {
        if (this.clipboard.Count == 0)
        {
            return;
        }

        var parent = this.GetPasteTarget();
        if (parent == null)
        {
            return;
        }

        foreach (var item in this.clipboard)
        {
            if (item is ICanBeCloned cloneable)
            {
                var clone = cloneable.CloneSelf();

                // If pasting a node, ensure it has a unique name if needed?
                // For now, just insert.
                await this.InsertItemAsync(clone, parent, 0).ConfigureAwait(false);
            }
        }
    }

    private bool CanPaste() => this.clipboard.Count > 0;

    private ITreeItem? GetPasteTarget()
        => this.Scene is null
            ? null
            : this.SelectionModel switch
            {
                SingleSelectionModel { SelectedItem: var item } => item ?? this.Scene,
                MultipleSelectionModel<ITreeItem> { SelectedItems.Count: 1 } multiple => multiple.SelectedItems[0],
                _ => this.Scene,
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
        => await UndoRedo.Default[this].UndoAsync().ConfigureAwait(false);

    [RelayCommand]
    private async Task Redo()
        => await UndoRedo.Default[this].RedoAsync().ConfigureAwait(false);

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
        var sceneAdapter = this.Scene;
        if (sceneAdapter is null)
        {
            return;
        }

        ITreeItem? selectedItem = null;
        switch (this.SelectionModel)
        {
            case SingleSelectionModel { SelectedItem: var singleSelected }:
                selectedItem = singleSelected;
                break;
            case MultipleSelectionModel<ITreeItem> multiple when multiple.SelectedIndices.Count == 1:
                selectedItem = multiple.SelectedItems.FirstOrDefault();
                break;
        }

        var parent = selectedItem ?? sceneAdapter;
        var name = this.GetNextEntityName();

        // Create the model and adapter here to update UI immediately
        var scene = this.Scene?.AttachedObject;
        if (scene == null)
        {
            return;
        }

        var newNode = new SceneNode(scene) { Name = name };
        var newAdapter = new SceneNodeAdapter(newNode);

        // Update UI (this will trigger OnItemAdded)
        await this.InsertItemAsync(newAdapter, parent, 0).ConfigureAwait(false);
    }

    private async void OnDocumentOpened(object? sender, DroidNet.Documents.DocumentOpenedEventArgs e)
    {
        if (e.WindowId.Value != this.windowId.Value)
        {
            return;
        }

        // Only react to scene documents
        if (e.Metadata is not SceneDocumentMetadata sceneMetadata)
        {
            return;
        }

        // Find the scene in the current project
        var scene = this.currentProject.Scenes.FirstOrDefault(s => s.Id == sceneMetadata.DocumentId);
        if (scene is null)
        {
            return;
        }

        // Update the active scene on the project
        this.currentProject.ActiveScene = scene;

        // Load the scene data and display it
        await this.HandleDocumentOpenedAsync(scene).ConfigureAwait(true);
    }

    private async Task LoadSceneAsync(Scene scene)
    {
        // Cancel any previous loading operation
        if (this.loadSceneCts is { IsCancellationRequested: false })
        {
            await this.loadSceneCts.CancelAsync().ConfigureAwait(false);
            this.loadSceneCts.Dispose();
        }

        this.loadSceneCts = new CancellationTokenSource();
        var ct = this.loadSceneCts.Token;

        var loadedScene = await this.projectManager.LoadSceneAsync(scene).ConfigureAwait(true);
        if (loadedScene is null)
        {
            return;
        }

        // Trace: scene data successfully loaded from project storage
        this.LogSceneLoaded(loadedScene.Id, loadedScene.Name ?? "<unnamed>");

        this.nextEntityIndex = loadedScene.AllNodes.Count();

        if (ct.IsCancellationRequested)
        {
            return;
        }

        // Build the scene layout from the loaded scene
        this.Scene = new SceneAdapter(loadedScene)
        {
            IsExpanded = true,
            IsLocked = true,
            IsRoot = true,
            UseLayoutAdapters = true,
        };
        await this.InitializeRootAsync(this.Scene, skipRoot: false).ConfigureAwait(true);

        if (ct.IsCancellationRequested)
        {
            return;
        }

        // Delegate scene synchronization to the service (wait until engine is running).
        await this.sceneEngineSync.SyncSceneWhenReadyAsync(loadedScene, ct).ConfigureAwait(true);

        // Notify other components (e.g. viewport/view lifecycle) that the scene
        // has been created/synchronized in the engine and is ready for rendering.
        // This allows views to defer creating engine views until the scene exists.
        this.LogSceneLoadedMessageSent(loadedScene.Id, System.DateTime.UtcNow);
        _ = this.messenger.Send(new SceneLoadedMessage(loadedScene));
    }

    private async void OnItemAdded(object? sender, TreeItemAddedEventArgs args)
    {
        _ = sender; // unused
        await this.HandleItemAddedAsync(args).ConfigureAwait(false);
    }

    private void OnItemRemoved(object? sender, TreeItemRemovedEventArgs args)
    {
        _ = sender; // unused

        // Sync with Backend
        _ = this.RemoveItemFromBackendAsync(args.TreeItem);

        UndoRedo.Default[this].AddChange(
            $"InsertItemAsync({args.TreeItem.Label})",
            async () => await this.InsertItemAsync(args.TreeItem, args.Parent, args.RelativeIndex).ConfigureAwait(false));

        this.LogItemRemoved(args.TreeItem.Label);
    }

    private async Task RemoveItemFromBackendAsync(ITreeItem item)
    {
        await this.sceneExplorerService.DeleteItemsAsync([item]).ConfigureAwait(false);

        if (AsSceneNodeAdapter(item) is { } adapter)
        {
            _ = this.nodeAdapterIndex.Remove(adapter.AttachedObject.Id);
        }
    }

    private void OnItemMoved(object? sender, TreeItemsMovedEventArgs args)
    {
        _ = sender; // unused

        if (args.Moves.Count == 0)
        {
            return;
        }

        if (args.IsBatch)
        {
            UndoRedo.Default[this].BeginChangeSet($"Move {args.Moves.Count} item(s)");
        }

        try
        {
            // Sync with Backend
            _ = this.sceneExplorerService.UpdateMovedItemsAsync(args);

            this.RecordMoveUndo(args);
        }
        finally
        {
            if (args.IsBatch)
            {
                UndoRedo.Default[this].EndChangeSet();
            }
        }
    }

    private void RecordMoveUndo(TreeItemsMovedEventArgs args)
    {
        foreach (var group in args.Moves.GroupBy(m => m.PreviousParent))
        {
            foreach (var move in group.OrderBy(m => m.PreviousIndex))
            {
                var item = move.Item;
                UndoRedo.Default[this].AddChange(
                    $"MoveItemAsync({item.Label})",
                    async () => await this.MoveItemAsync(item, move.PreviousParent, move.PreviousIndex).ConfigureAwait(false));
            }
        }
    }

    private void OnSceneNodeSelectionRequested(object recipient, SceneNodeSelectionRequestMessage message)
    {
        switch (this.SelectionModel)
        {
            case SingleSelectionModel singleSelection:
                var selectedItem = singleSelection.SelectedItem;
                var selectedNode = AsSceneNode(selectedItem);
                message.Reply(selectedNode is null
                    ? Array.Empty<SceneNode>()
                    : [selectedNode]);
                break;

            case MultipleSelectionModel<ITreeItem> multipleSelection:
                var selection = multipleSelection.SelectedItems
                    .Select(AsSceneNode)
                    .Where(node => node is not null)
                    .Select(node => node!)
                    .ToList();
                message.Reply(selection);
                break;

            default:
                message.Reply(Array.Empty<SceneNode>());
                break;
        }
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

        var selectedNode = AsSceneNode(this.SelectionModel?.SelectedItem as ITreeItem);
        var selected = selectedNode is null
            ? Array.Empty<SceneNode>()
            : [selectedNode];

        _ = this.messenger.Send(new SceneNodeSelectionChangedMessage(selected));
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

        var selection = multipleSelectionModel.SelectedIndices
            .Select(this.GetShownItemAt)
            .Select(AsSceneNode)
            .Where(node => node is not null)
            .Select(node => node!)
            .ToList();

        _ = this.messenger.Send(new SceneNodeSelectionChangedMessage(selection));
    }

    private List<ITreeItem> GetSelectedItems()
        => this.SelectionModel switch
        {
            null => [],
            MultipleSelectionModel multi2 => [.. multi2.SelectedIndices.Select(this.GetShownItemAt)],
            SingleSelectionModel when this.SelectedItem is not null => [this.SelectedItem],
            _ => [],
        };

    private void OnItemBeingAdded(object? sender, TreeItemBeingAddedEventArgs args)
    {
        _ = sender; // unused

        var entityAdapter = AsSceneNodeAdapter(args.TreeItem);
        if (entityAdapter is null)
        {
            return;
        }

        var entity = entityAdapter.AttachedObject;

        var scene = ResolveSceneFromTree(args.Parent);

        if (scene is null)
        {
            this.LogUnableResolveSceneForAddedItem(entity.Name);
            return;
        }

        this.HandleItemBeingAdded(scene, entityAdapter, args.Parent, args);
    }

    [RelayCommand(CanExecute = nameof(CanCreateFolder))]
    private async Task CreateFolder()
    {
        this.LogCreateFolderInvoked(this.SelectionModel?.GetType().Name, this.ShownItemsCount);

        var sceneAdapter = this.Scene;
        if (sceneAdapter is null)
        {
            return;
        }

        // Default to creating under the Scene Root
        ITreeItem parent = sceneAdapter;

        // If a single item is selected, try to use it as the parent
        var selectedItems = this.GetSelectedItems();
        if (selectedItems.Count == 1)
        {
            var selected = selectedItems[0];

            // We can create folders under Scene, Folders, or Nodes.
            if (selected is SceneAdapter or FolderAdapter or SceneNodeAdapter)
            {
                parent = selected;
            }
        }

        var newFolderId = Guid.NewGuid();
        var newFolder = new FolderAdapter(newFolderId, "New Folder");

        // Update UI (triggers OnItemAdded -> Service.CreateFolderAsync)
        await this.InsertItemAsync(newFolder, parent, 0).ConfigureAwait(false);
    }

    private bool CanCreateFolder()
    {
        // Allow creation if nothing is selected (root), or if a single item is selected.
        // We don't check types strictly here to avoid disabling the button when the user expects it to work.
        // The CreateFolder command will default to Root if the selection is invalid.
        if (this.SelectionModel is null)
        {
            return true;
        }

        if (this.SelectionModel is MultipleSelectionModel<ITreeItem> multiple)
        {
            return multiple.SelectedIndices.Count <= 1;
        }

        // SingleSelectionModel always has 0 or 1 item selected.
        return true;
    }

    private void NotifySelectionDependentCommands()
    {
        this.AddEntityCommand.NotifyCanExecuteChanged();
        this.CreateFolderCommand.NotifyCanExecuteChanged();
    }

    private string GetNextEntityName()
    {
        var index = Interlocked.Increment(ref this.nextEntityIndex);
        return $"New Entity {index}";
    }

    private async Task IndexAdaptersForSceneAsync(SceneAdapter sceneAdapter)
    {
        if (sceneAdapter is null)
        {
            return;
        }

        var roots = await sceneAdapter.Children.ConfigureAwait(true);
        var stack = new Stack<ITreeItem>((IEnumerable<ITreeItem>?)roots ?? []);
        while (stack.Count > 0)
        {
            var item = stack.Pop();
            if (item is SceneNodeAdapter lna)
            {
                this.nodeAdapterIndex[lna.AttachedObject.Id] = lna;
            }

            if (item is TreeItemAdapter adapter)
            {
                var children = await adapter.Children.ConfigureAwait(true);
                if (children is not null)
                {
                    foreach (var c in children)
                    {
                        stack.Push(c);
                    }
                }
            }
        }
    }
}
