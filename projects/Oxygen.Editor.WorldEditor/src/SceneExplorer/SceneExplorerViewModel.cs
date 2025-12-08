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
using Oxygen.Editor.Documents;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.WorldEditor.Messages;
using Oxygen.Editor.WorldEditor.SceneExplorer.Operations;
using Oxygen.Editor.WorldEditor.Services;

namespace Oxygen.Editor.WorldEditor.SceneExplorer;

/// <summary>
///     The ViewModel for the <see cref="SceneExplorer.SceneExplorerView" /> view.
/// </summary>
public partial class SceneExplorerViewModel : DynamicTreeViewModel, IDisposable
{
    private readonly IProject currentProject;
    private readonly ILogger<SceneExplorerViewModel> logger;
    private readonly IMessenger messenger;
    private readonly IRouter router;
    private readonly IProjectManagerService projectManager;
    private readonly IDocumentService documentService;
    private readonly ISceneEngineSync sceneEngineSync;
    private readonly ISceneMutator sceneMutator;
    private readonly ISceneOrganizer sceneOrganizer;
    private readonly Dictionary<SceneNodeAdapter, SceneNodeChangeRecord> pendingSceneChanges = new();
    private readonly Dictionary<SceneNodeAdapter, LayoutChangeRecord> pendingLayoutChanges = new();
    private int nextEntityIndex;
    private bool isPerformingLayoutMove;

    private bool isDisposed;
    // When true we are performing an explicit delete operation and the tree's
    // ItemBeingRemoved handler should remove the corresponding scene nodes.
    // This prevents accidental removal when moving items between folder/groupings.
    private bool isPerformingDelete;

    // When true, model-driven add/remove operations should not create undo entries.
    // This is used to avoid duplicating undo entries when we perform programmatic
    // adapter manipulations during folder create/undo/redo.
    private bool suppressUndoRecording;

    // Transient capture of an adapter's old scene-node parent when it is removed
    // as part of a non-delete operation so the subsequent add can detect a
    // SceneNode -> SceneNode reparent and perform a single model mutation.
    private readonly Dictionary<SceneNodeAdapter, System.Guid> capturedOldParent = new();

    // Track adapters we've deleted so OnItemRemoved can broadcast a node-removed
    // message (we perform engine removal in OnItemBeingRemoved when deleting).
    private readonly HashSet<SceneNodeAdapter> deletedAdapters = new();

    // No cached selection - capture selection at command time to avoid stale state.

    /// <summary>
    ///     Initializes a new instance of the <see cref="SceneExplorerViewModel" /> class.
    /// </summary>
    /// <param name="projectManager">The project manager service.</param>
    /// <param name="messenger">The messenger service used for cross-component communication.</param>
    /// <param name="router">The router service used for navigation events.</param>
    /// <param name="documentService">The document service for handling document operations.</param>
    /// <param name="sceneEngineSync">The scene-engine synchronization service.</param>
    /// <param name="loggerFactory">
    ///     Optional factory for creating loggers. If provided, enables detailed logging of the
    ///     recognition process. If <see langword="null" />, logging is disabled.
    /// </param>
    public SceneExplorerViewModel(
        IProjectManagerService projectManager,
        IMessenger messenger,
        IRouter router,
        IDocumentService documentService,
        ISceneEngineSync sceneEngineSync,
        ISceneMutator sceneMutator,
        ISceneOrganizer sceneOrganizer,
        ILoggerFactory? loggerFactory = null)
    {
        this.logger = loggerFactory?.CreateLogger<SceneExplorerViewModel>() ??
                      NullLoggerFactory.Instance.CreateLogger<SceneExplorerViewModel>();
        this.projectManager = projectManager;
        this.messenger = messenger;
        this.router = router;
        this.documentService = documentService;
        this.sceneEngineSync = sceneEngineSync;
        this.sceneMutator = sceneMutator;
        this.sceneOrganizer = sceneOrganizer;

        Debug.Assert(projectManager.CurrentProject is not null, "must have a current project");
        this.currentProject = projectManager.CurrentProject;

        this.UndoStack = UndoRedo.Default[this].UndoStack;
        this.RedoStack = UndoRedo.Default[this].RedoStack;

        this.ItemBeingRemoved += this.OnItemBeingRemoved;
        this.ItemRemoved += this.OnItemRemoved;

        this.ItemBeingAdded += this.OnItemBeingAdded;
        this.ItemAdded += this.OnItemAdded;

        messenger.Register<SceneNodeSelectionRequestMessage>(this, this.OnSceneNodeSelectionRequested);

        // Subscribe to document events to load scene when a scene document is opened
        documentService.DocumentOpened += this.OnDocumentOpened;
    }

    /// <summary>
    ///     Gets or sets a value indicating whether there are unlocked items in the current selection.
    /// </summary>
    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(SceneExplorerViewModel.RemoveSelectedItemsCommand))]
    [NotifyCanExecuteChangedFor(nameof(SceneExplorerViewModel.CreateFolderFromSelectionCommand))]
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
    public void Dispose()
    {
        if (this.isDisposed)
        {
            return;
        }

        this.documentService.DocumentOpened -= this.OnDocumentOpened;
        this.messenger.UnregisterAll(this);

        this.isDisposed = true;
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
                ((INotifyCollectionChanged)oldSelectionModel.SelectedItems).CollectionChanged -=
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
    [RelayCommand(CanExecute = nameof(SceneExplorerViewModel.HasUnlockedSelectedItems))]
    protected override async Task RemoveSelectedItems()
    {
        // When users delete items, the tree triggers ItemBeingRemoved/ItemRemoved events.
        // Those same events are also raised when items are moved in the tree (drag/drop).
        // To avoid removing scene nodes from the underlying Scene.RootNodes during moves,
        // guard deletion using a flag so OnItemBeingRemoved only removes nodes from the
        // scene when a deliberate delete operation is in progress.
        UndoRedo.Default[this].BeginChangeSet($"Remove {this.SelectionModel}");
        try
        {
            this.isPerformingDelete = true;
            await base.RemoveSelectedItems().ConfigureAwait(false);
        }
        finally
        {
            this.isPerformingDelete = false;
            UndoRedo.Default[this].EndChangeSet();
        }
    }

    [RelayCommand]
    private void Undo() => UndoRedo.Default[this].Undo();

    [RelayCommand]
    private void Redo() => UndoRedo.Default[this].Redo();

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
                return item is null || item is SceneAdapter || AsSceneNodeAdapter(item) is not null;

            case MultipleSelectionModel<ITreeItem> multiple:
                if (multiple.SelectedIndices.Count == 0)
                {
                    return true; // no selection: create at root
                }

                if (multiple.SelectedIndices.Count == 1)
                {
                    var selected = multiple.SelectedItems.FirstOrDefault();
                    return selected is null || selected is SceneAdapter || AsSceneNodeAdapter(selected) is not null;
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

        if (selectedItem is FolderAdapter)
        {
            selectedItem = null; // folders are layout-only; create at root instead
        }

        var relativeIndex = 0;

        var selectedNode = AsSceneNodeAdapter(selectedItem);
        if (selectedNode is not null)
        {
            // Create as a child of the selected node
            var parentNode = selectedNode;
            var childCount = await parentNode.Children.ConfigureAwait(false);
            relativeIndex = childCount.Count;
            var newEntity = new SceneNodeAdapter(
                new SceneNode(parentNode.AttachedObject.Scene)
                {
                    Name = this.GetNextEntityName(),
                });

            await this.InsertItemAsync(relativeIndex, parentNode, newEntity).ConfigureAwait(false);
            return;
        }

        SceneAdapter parent = sceneAdapter;
        if (selectedItem is SceneAdapter sa)
        {
            parent = sa;
        }
        else if (selectedItem is not null)
        {
            parent = selectedItem.Parent as SceneAdapter ?? sceneAdapter;
        }

        var parentChildren = await parent.Children.ConfigureAwait(false);
        relativeIndex = selectedItem is null ? parentChildren.Count : parentChildren.IndexOf(selectedItem) + 1;

        var newRootEntity = new SceneNodeAdapter(
            new SceneNode(parent.AttachedObject) { Name = this.GetNextEntityName() });

        await this.InsertItemAsync(relativeIndex, parent, newRootEntity).ConfigureAwait(false);
    }

    private async void OnDocumentOpened(object? sender, DroidNet.Documents.DocumentOpenedEventArgs e)
    {
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

    /// <summary>
    /// Handles document-open actions once the target scene has been resolved.
    /// Kept protected for testability; does no UI-thread dispatching.
    /// </summary>
    /// <param name="scene">Scene to load.</param>
    /// <returns>Task that completes after the scene has been loaded.</returns>
    protected internal virtual async Task HandleDocumentOpenedAsync(Scene scene)
    {
        await this.LoadSceneAsync(scene).ConfigureAwait(true);
    }

    private async Task LoadSceneAsync(Scene scene)
    {
        if (!await this.projectManager.LoadSceneAsync(scene).ConfigureAwait(true))
        {
            return;
        }

        this.nextEntityIndex = scene.AllNodes.Count();

        this.Scene = SceneAdapter.BuildLayoutTree(scene);
        await this.InitializeRootAsync(this.Scene, skipRoot: false).ConfigureAwait(true);

        // Delegate scene synchronization to the service
        await this.sceneEngineSync.SyncSceneAsync(scene).ConfigureAwait(true);
    }

    private async void OnItemAdded(object? sender, TreeItemAddedEventArgs args)
    {
        _ = sender; // unused

        if (this.isPerformingLayoutMove)
        {
            return;
        }

        if (!this.suppressUndoRecording)
        {
            // Add a safe undo action that only attempts removal if the item is still
            // present in the tree. This avoids assert failures when multiple programmatic
            // operations modify the tree before the undo is applied.
            var item = args.TreeItem;
            UndoRedo.Default[this]
                .AddChange(
                    $"RemoveItem({item.Label})",
                    () =>
                    {
                        try
                        {
                            // If item has a parent, verify the parent currently contains the item
                            if (item.Parent is TreeItemAdapter pa)
                            {
                                var children = pa.Children.ConfigureAwait(false).GetAwaiter().GetResult();
                                if (!children.Contains(item))
                                {
                                    // Nothing to do
                                    return;
                                }
                            }

                            // If not shown and no parent we cannot remove
                            if (item.Parent is null && !this.ShownItems.Contains(item))
                            {
                                return;
                            }

                            this.RemoveItemAsync(item).GetAwaiter().GetResult();
                        }
                        catch (Exception ex)
                        {
                            // Swallow exceptions during undo to keep undo system robust;
                            // log for diagnostics.
                            this.LogUndoRemoveFailed(ex, item.Label);
                        }
                    });
        }

        this.LogItemAdded(args.TreeItem.Label);

        var addedAdapter = AsSceneNodeAdapter(args.TreeItem);
        if (addedAdapter is null)
        {
            return;
        }

        if (this.pendingSceneChanges.TryGetValue(addedAdapter, out var sceneChange))
        {
            _ = this.pendingSceneChanges.Remove(addedAdapter);
            await this.ApplySceneAdditionAsync(sceneChange).ConfigureAwait(false);
        }

        this.TryApplyLayoutChange(addedAdapter);
    }

    private async void OnItemRemoved(object? sender, TreeItemRemovedEventArgs args)
    {
        _ = sender; // unused

        if (this.isPerformingLayoutMove)
        {
            return;
        }

        if (!this.suppressUndoRecording)
        {
            var item = args.TreeItem;
            var parent = args.Parent;
            UndoRedo.Default[this]
                .AddChange(
                    $"Add Item({item.Label})",
                    () =>
                    {
                        try
                        {
                            if (parent is null)
                            {
                                this.LogCannotUndoAddOriginalParentNull(item.Label);
                                return;
                            }

                            // If the parent already contains the item, nothing to do
                            if (item.Parent == parent)
                            {
                                return;
                            }

                            // If parent is a TreeItemAdapter, ensure the child collection does not contain the item
                            if (parent is TreeItemAdapter pa)
                            {
                                var children = pa.Children.ConfigureAwait(false).GetAwaiter().GetResult();
                                if (children.Contains(item))
                                {
                                    return;
                                }
                            }

                            this.InsertItemAsync(args.RelativeIndex, parent, item).GetAwaiter().GetResult();
                        }
                        catch (Exception ex)
                        {
                            this.LogUndoAddFailed(ex, item.Label);
                        }
                    });
        }

        this.LogItemRemoved(args.TreeItem.Label);

        var removedAdapter = AsSceneNodeAdapter(args.TreeItem);
        if (removedAdapter is null)
        {
            return;
        }

        var wasDeletion = this.deletedAdapters.Remove(removedAdapter);

        if (this.pendingSceneChanges.TryGetValue(removedAdapter, out var sceneChange))
        {
            _ = this.pendingSceneChanges.Remove(removedAdapter);
            await this.ApplySceneRemovalAsync(sceneChange, wasDeletion).ConfigureAwait(false);
        }
        else if (wasDeletion)
        {
            _ = this.messenger.Send(new SceneNodeRemovedMessage(new[] { removedAdapter.AttachedObject }));
        }

        this.TryApplyLayoutChange(removedAdapter);

        if (wasDeletion)
        {
            _ = this.capturedOldParent.Remove(removedAdapter);
        }
    }

    private void OnSceneNodeSelectionRequested(object recipient, SceneNodeSelectionRequestMessage message)
    {
        switch (this.SelectionModel)
        {
            case SingleSelectionModel singleSelection:
                var selectedItem = singleSelection.SelectedItem;
                var selectedAdapter = AsSceneNodeAdapter(selectedItem);
                message.Reply(selectedAdapter is null
                    ? Array.Empty<SceneNode>()
                    : new[] { selectedAdapter.AttachedObject });
                break;

            case MultipleSelectionModel<ITreeItem> multipleSelection:
                var selection = multipleSelection.SelectedItems
                    .Select(AsSceneNodeAdapter)
                    .Where(adapter => adapter is not null)
                    .Select(adapter => adapter!.AttachedObject)
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

        var selectedAdapter = AsSceneNodeAdapter(this.SelectionModel?.SelectedItem as ITreeItem);
        var selected = selectedAdapter is null
            ? Array.Empty<SceneNode>()
            : new[] { selectedAdapter.AttachedObject };

        _ = this.messenger.Send(new SceneNodeSelectionChangedMessage(selected));
    }

    private void OnMultipleSelectionChanged(object? sender, NotifyCollectionChangedEventArgs args)
    {
        if (this.SelectionModel is not MultipleSelectionModel<ITreeItem> multipleSelectionModel)
        {
            return;
        }

        this.NotifySelectionDependentCommands();

        var unlockedSelectedItems = false;
        foreach (var selectedIndex in multipleSelectionModel.SelectedIndices)
        {
            var item = this.ShownItems[selectedIndex];
            unlockedSelectedItems = !item.IsLocked;
            if (unlockedSelectedItems)
            {
                break;
            }
        }

        this.HasUnlockedSelectedItems = unlockedSelectedItems;

        var selection = multipleSelectionModel.SelectedItems
            .Select(AsSceneNodeAdapter)
            .Where(adapter => adapter is not null)
            .Select(adapter => adapter!.AttachedObject)
            .ToList();

        _ = this.messenger.Send(new SceneNodeSelectionChangedMessage(selection));
    }

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

    /// <summary>
    /// Core logic for handling an item being added. Separated from the event handler for testability.
    /// </summary>
    /// <param name="scene">The scene owning the adapter.</param>
    /// <param name="entityAdapter">The node adapter being added.</param>
    /// <param name="parent">The parent tree item.</param>
    /// <param name="args">Original event args (used to set <see cref="TreeItemBeingAddedEventArgs.Proceed"/> when needed).</param>
    protected internal virtual void HandleItemBeingAdded(Scene scene, SceneNodeAdapter entityAdapter, ITreeItem parent, TreeItemBeingAddedEventArgs args)
    {
        var entity = entityAdapter.AttachedObject;

        this.pendingSceneChanges.Remove(entityAdapter);
        this.pendingLayoutChanges.Remove(entityAdapter);

        if (this.isPerformingLayoutMove)
        {
            return;
        }

        if (parent is SceneNodeAdapter newNodeParent)
        {
            var nodeExistsInScene = SceneContainsNode(scene, entity.Id);

            if (!nodeExistsInScene)
            {
                var creationChange = this.sceneMutator.CreateNodeUnderParent(entity, newNodeParent.AttachedObject, scene);
                this.pendingSceneChanges[entityAdapter] = creationChange;
                return;
            }

            var oldParentId = this.capturedOldParent.TryGetValue(entityAdapter, out var captured)
                ? captured
                : entity.Parent?.Id;

            var change = this.sceneMutator.ReparentNode(entity.Id, oldParentId, newNodeParent.AttachedObject.Id, scene);
            _ = this.capturedOldParent.Remove(entityAdapter);
            this.pendingSceneChanges[entityAdapter] = change;
            return;
        }

        if (parent is SceneAdapter)
        {
            if (this.capturedOldParent.TryGetValue(entityAdapter, out var oldParentId))
            {
                var change = this.sceneMutator.ReparentNode(entity.Id, oldParentId, null, scene);
                _ = this.capturedOldParent.Remove(entityAdapter);
                this.pendingSceneChanges[entityAdapter] = change;
            }
            else
            {
                var change = this.sceneMutator.CreateNodeAtRoot(entity, scene);
                this.pendingSceneChanges[entityAdapter] = change;
            }

            return;
        }

        if (parent is FolderAdapter folderAdapter)
        {
            try
            {
                var change = this.sceneOrganizer.MoveNodeToFolder(entity.Id, folderAdapter.Id, scene);
                this.pendingLayoutChanges[entityAdapter] = change;
            }
            catch (InvalidOperationException ex)
            {
                args.Proceed = false;
                this.LogMoveNodeToFolderRejected(ex, entity.Id, folderAdapter.Id);
            }
        }
    }

    private void OnItemBeingRemoved(object? sender, TreeItemBeingRemovedEventArgs args)
    {
        _ = sender; // unused

        var entityAdapter = AsSceneNodeAdapter(args.TreeItem);
        if (entityAdapter is null)
        {
            return;
        }

        var entity = entityAdapter.AttachedObject;

        if (!this.isPerformingDelete)
        {
            this.CaptureOldParentForMove(entityAdapter, entityAdapter.Parent);
            return;
        }

        if (this.isPerformingLayoutMove)
        {
            return;
        }

        // Prefer the adapter's owning scene first; fall back to walking the tree if needed.
        var scene = entity.Scene ?? ResolveSceneFromTree(args.TreeItem.Parent ?? entityAdapter.Parent);

        if (scene is null)
        {
            this.LogUnableResolveSceneForRemovedItem(entity.Name);
            return;
        }

        this.HandleItemBeingRemoved(scene, entityAdapter, args);
    }

    /// <summary>
    /// Captures the current scene parent id for a node adapter during move operations so that
    /// subsequent add handlers can reparent using the recorded lineage instead of the live tree.
    /// </summary>
    /// <param name="entityAdapter">Adapter being moved.</param>
    /// <param name="currentParent">Current tree parent (may be <see langword="null" />).</param>
    protected internal virtual void CaptureOldParentForMove(SceneNodeAdapter entityAdapter, ITreeItem? currentParent)
    {
        if (currentParent is SceneNodeAdapter oldParentNode)
        {
            this.capturedOldParent[entityAdapter] = oldParentNode.AttachedObject.Id;
        }
    }

    /// <summary>
    /// Core logic for handling an item being removed during delete flows. Separated for testability.
    /// </summary>
    /// <param name="scene">The scene owning the adapter.</param>
    /// <param name="entityAdapter">Adapter being removed.</param>
    /// <param name="args">Original removal event args.</param>
    protected internal virtual void HandleItemBeingRemoved(Scene scene, SceneNodeAdapter entityAdapter, TreeItemBeingRemovedEventArgs args)
    {
        _ = args; // currently unused
        var entity = entityAdapter.AttachedObject;

        var change = this.sceneMutator.RemoveNode(entity.Id, scene);
        this.pendingSceneChanges[entityAdapter] = change;
        _ = this.deletedAdapters.Add(entityAdapter);
    }

    protected internal readonly record struct SelectionCapture(HashSet<System.Guid> NodeIds, bool UsedShownItemsFallback);

    protected internal readonly record struct FolderCreationContext(
        LayoutChangeRecord LayoutChange,
        FolderAdapter FolderAdapter,
        ExplorerEntryData FolderEntry);

    [RelayCommand(CanExecute = nameof(CanCreateFolderFromSelection))]
    private async Task CreateFolderFromSelection()
    {
        this.LogCreateFolderInvoked(this.SelectionModel?.GetType().Name, this.ShownItems.Count);

        var selection = this.CaptureSelectionForFolderCreation();
        if (selection.NodeIds.Count == 0)
        {
            if (selection.UsedShownItemsFallback)
            {
                this.LogCreateFolderUsingShownItems(selection.NodeIds.Count);
            }

            this.LogCreateFolderNoSelection();
            return;
        }

        var idsCsv = string.Join(',', selection.NodeIds.Select(static g => g.ToString()));
        this.LogCreateFolderCapturedSelection(idsCsv);

        var sceneAdapter = this.Scene;
        if (sceneAdapter is null)
        {
            return;
        }

        var scene = sceneAdapter.AttachedObject;
        var topLevelSelection = FilterTopLevelSelectedNodeIds(selection.NodeIds, scene);

        var creationContext = this.CreateFolderCreationContext(sceneAdapter, scene, topLevelSelection);
        if (creationContext is null)
        {
            return;
        }

        await this.ApplyFolderCreationAsync(sceneAdapter, scene, creationContext.Value).ConfigureAwait(true);
    }

    /// <summary>
    /// Captures the set of selected node ids for folder creation, including a fallback
    /// pass over shown items when the selection model is empty.
    /// </summary>
    /// <returns>Selection capture describing node ids and whether shown-items fallback was used.</returns>
    protected internal virtual SelectionCapture CaptureSelectionForFolderCreation()
    {
        var selectedIds = new HashSet<System.Guid>();
        var usedShownItemsFallback = false;

        void TryAddFromItem(ITreeItem? item)
        {
            var nodeAdapter = AsSceneNodeAdapter(item);
            if (nodeAdapter is not null)
            {
                _ = selectedIds.Add(nodeAdapter.AttachedObject.Id);
            }
        }

        switch (this.SelectionModel)
        {
            case SingleSelectionModel { SelectedItem: var item }:
                TryAddFromItem(item as ITreeItem);
                break;

            case MultipleSelectionModel<ITreeItem> multiple:
                foreach (var item in multiple.SelectedItems)
                {
                    TryAddFromItem(item);
                }

                break;
        }

        if (selectedIds.Count == 0)
        {
            foreach (var shown in this.ShownItems)
            {
                if (!shown.IsSelected)
                {
                    continue;
                }

                TryAddFromItem(shown);
                usedShownItemsFallback = true;
            }
        }

        return new SelectionCapture(selectedIds, usedShownItemsFallback);
    }

    /// <summary>
    /// Creates folder layout changes and constructs the new folder adapter.
    /// </summary>
    /// <param name="sceneAdapter">Active scene adapter.</param>
    /// <param name="scene">Scene backing the explorer.</param>
    /// <param name="selectedIds">Top-level selected node ids.</param>
    /// <returns>Creation context or <see langword="null" /> when organizer rejects creation.</returns>
    protected internal virtual FolderCreationContext? CreateFolderCreationContext(
        SceneAdapter sceneAdapter,
        Scene scene,
        HashSet<System.Guid> selectedIds)
    {
        // Only pre-materialize layout entries when a layout already exists; keep null untouched for tests that expect it.
        if (scene.ExplorerLayout is not null)
        {
            EnsureLayoutContainsSelectedNodes(scene, selectedIds);
        }

        var layoutChange = this.sceneOrganizer.CreateFolderFromSelection(selectedIds, scene, sceneAdapter);
        var folder = layoutChange.NewFolder;

        if (folder?.FolderId is null)
        {
            this.LogOrganizerReturnedNullFolder();
            return null;
        }

        scene.ExplorerLayout = layoutChange.NewLayout;
        var folderAdapter = new FolderAdapter(folder.FolderId.Value, folder.Name ?? "New Folder");

        return new FolderCreationContext(layoutChange, folderAdapter, folder);
    }

    /// <summary>
    /// Applies the organizer result to the adapter tree, including moving existing adapters,
    /// expanding/selection, and undo registration.
    /// </summary>
    protected internal virtual async Task ApplyFolderCreationAsync(
        SceneAdapter sceneAdapter,
        Scene scene,
        FolderCreationContext context)
    {
        this.suppressUndoRecording = true;
        this.isPerformingLayoutMove = true;
        try
        {
            await this.InsertItemAsync(0, sceneAdapter, context.FolderAdapter).ConfigureAwait(true);

            var movedAdapterCount = await this.MoveAdaptersIntoFolderAsync(sceneAdapter, context.FolderAdapter, context.FolderEntry)
                .ConfigureAwait(true);
            this.LogCreateFolderMovedAdaptersCount(movedAdapterCount);
        }
        finally
        {
            this.suppressUndoRecording = false;
            this.isPerformingLayoutMove = false;
        }

        await this.ExpandAndSelectFolderAsync(sceneAdapter, context.FolderAdapter, context.FolderEntry).ConfigureAwait(true);
        this.LogCreateFolderCreated(context.FolderEntry.Name!, context.FolderEntry.Children?.Count ?? 0, scene.Id);

        this.RegisterCreateFolderUndo(sceneAdapter, scene, context.LayoutChange, context.FolderEntry.Name ?? "Folder");
    }

    /// <summary>
    /// Moves adapters corresponding to folder children into the created folder adapter.
    /// </summary>
    protected internal virtual async Task<int> MoveAdaptersIntoFolderAsync(
        SceneAdapter sceneAdapter,
        FolderAdapter folderAdapter,
        ExplorerEntryData folderEntry)
    {
        if (folderEntry.Children is null || folderEntry.Children.Count == 0)
        {
            return 0;
        }

        // Ensure the target folder is visible/expanded before inserting children
        if (!folderAdapter.IsExpanded)
        {
            try
            {
                await this.ExpandItemAsync(folderAdapter).ConfigureAwait(true);
            }
            catch (InvalidOperationException)
            {
                // If the folder is not yet visible (e.g., not in ShownItems), fall back to marking it expanded
                folderAdapter.IsExpanded = true;
            }
        }

        var movedAdapterCount = 0;
        foreach (var moved in folderEntry.Children)
        {
            if (moved.NodeId is null)
            {
                continue;
            }

            var nodeAdapter = await FindAdapterForNodeIdAsync(sceneAdapter, moved.NodeId.Value).ConfigureAwait(true);
            if (nodeAdapter is null)
            {
                this.LogCreateFolderNodeAdapterNotFound(moved.NodeId.Value);
                continue;
            }

            await this.RemoveItemAsync(nodeAdapter, updateSelection: false).ConfigureAwait(true);
            await this.InsertItemAsync(folderAdapter.ChildrenCount, folderAdapter, nodeAdapter).ConfigureAwait(true);

            ++movedAdapterCount;
            this.LogCreateFolderMovedAdapter(nodeAdapter.Label, folderAdapter.Label);
        }

        return movedAdapterCount;
    }

    /// <summary>
    /// Expands and selects the newly created folder, logging failures if the adapter cannot be found.
    /// </summary>
    protected internal virtual async Task ExpandAndSelectFolderAsync(
        SceneAdapter sceneAdapter,
        FolderAdapter folderAdapter,
        ExplorerEntryData folderEntry)
    {
        if (!folderAdapter.IsExpanded)
        {
            await this.ExpandItemAsync(folderAdapter).ConfigureAwait(true);
        }

        this.ClearAndSelectItem(folderAdapter);

        try
        {
            var children = await sceneAdapter.Children.ConfigureAwait(true);
            var foundFolderAdapter = children.OfType<FolderAdapter>().FirstOrDefault(f => f.Id == folderEntry.FolderId);
            if (foundFolderAdapter is not null && !ReferenceEquals(foundFolderAdapter, folderAdapter))
            {
                if (!foundFolderAdapter.IsExpanded)
                {
                    await this.ExpandItemAsync(foundFolderAdapter).ConfigureAwait(true);
                }

                this.ClearAndSelectItem(foundFolderAdapter);
            }
            else if (foundFolderAdapter is null)
            {
                this.LogCreatedFolderAdapterNotFound(folderEntry.Name);
            }
            else
            {
                this.LogAutoExpandedSelectedFolder(folderEntry.Name, folderEntry.FolderId);
            }
        }
        catch (Exception ex)
        {
            this.LogFailedApplySceneChange(ex, "AutoExpandSelectCreatedFolder", folderEntry.Name, folderEntry.FolderId);
        }
    }

    /// <summary>
    /// Registers undo/redo actions for folder creation using organizer-provided layouts.
    /// </summary>
    protected internal virtual void RegisterCreateFolderUndo(
        SceneAdapter sceneAdapter,
        Scene scene,
        LayoutChangeRecord layoutChange,
        string folderName)
    {
        if (layoutChange.NewFolder?.Children is null || layoutChange.NewFolder.Children.Count == 0)
        {
            var previousLayoutSingle = layoutChange.PreviousLayout;
            var newLayoutSingle = layoutChange.NewLayout;

            this.LogLayoutState("RegisterCreateFolderUndo(single).previous", previousLayoutSingle);
            this.LogLayoutState("RegisterCreateFolderUndo(single).new", newLayoutSingle);

            UndoRedo.Default[this].AddChange(
                new LayoutUndoRedoChange(
                    this,
                    sceneAdapter,
                    scene,
                    previousLayoutSingle,
                    newLayoutSingle)
                {
                    Key = $"Create Folder ({folderName})",
                });

            return;
        }

        var folderOnlyLayout = BuildFolderOnlyLayout(layoutChange);
        var previousLayout = layoutChange.PreviousLayout;
        var newLayout = layoutChange.NewLayout;

        this.LogLayoutState("RegisterCreateFolderUndo(folderOnly).previous", previousLayout);
        this.LogLayoutState("RegisterCreateFolderUndo(folderOnly).folderOnly", folderOnlyLayout);
        this.LogLayoutState("RegisterCreateFolderUndo(folderOnly).new", newLayout);

        // Order: top = move-items change (its undo pushes two redo entries), below = create/remove folder
        UndoRedo.Default[this].AddChange(
            new LayoutUndoRedoChange(
                this,
                sceneAdapter,
                scene,
                previousLayout,
                folderOnlyLayout)
            {
                Key = $"Create Folder ({folderName})",
            });

        // Top of stack: move items change (undo -> folderOnly, redo -> newLayout)
        UndoRedo.Default[this].AddChange(
            new MoveItemsWithFolderRemovalUndoChange(
                this,
                sceneAdapter,
                scene,
                folderOnlyLayout,
                previousLayout,
                newLayout,
                folderName)
            {
                Key = $"Move Items To Folder ({folderName})",
            });
    }

    private static IList<ExplorerEntryData> BuildFolderOnlyLayout(LayoutChangeRecord layoutChange)
    {
        // Start from previous layout (so nodes remain outside the folder for first undo)
        var baseLayout = layoutChange.PreviousLayout is null
            ? new List<ExplorerEntryData>()
            : new List<ExplorerEntryData>(layoutChange.PreviousLayout);
        if (baseLayout.Count == 0 && layoutChange.NewLayout is not null)
        {
            baseLayout = new List<ExplorerEntryData>(layoutChange.NewLayout);
        }

        var folder = layoutChange.NewFolder ?? new ExplorerEntryData
        {
            Type = "Folder",
            FolderId = Guid.NewGuid(),
            Name = "Folder",
        };

        var emptyFolder = new ExplorerEntryData
        {
            Type = "Folder",
            FolderId = folder.FolderId,
            Name = folder.Name,
            Children = [],
        };

        // Insert folder at the same position it appears in the new layout (best-effort), else append
        var insertIndex = 0;
        if (layoutChange.NewLayout is not null)
        {
            var found = layoutChange.NewLayout.Select((entry, index) => (entry, index))
                .FirstOrDefault(tuple => tuple.entry.FolderId == folder.FolderId);
            insertIndex = found.entry is null ? baseLayout.Count : Math.Min(found.index, baseLayout.Count);
        }
        else
        {
            insertIndex = baseLayout.Count;
        }

        baseLayout.Insert(insertIndex, emptyFolder);

        // Ensure original node entries remain present (if missing due to cloning source)
        if (layoutChange.PreviousLayout is not null)
        {
            foreach (var entry in layoutChange.PreviousLayout)
            {
                // avoid duplicating folder we just inserted
                if (string.Equals(entry.Type, "Folder", StringComparison.OrdinalIgnoreCase))
                {
                    continue;
                }

                if (!baseLayout.Any(e => e.NodeId == entry.NodeId))
                {
                    baseLayout.Add(entry);
                }
            }
        }

        return baseLayout;
    }

    private sealed class LayoutUndoRedoChange : Change
    {
        private readonly SceneExplorerViewModel owner;
        private readonly SceneAdapter sceneAdapter;
        private readonly Scene scene;
        private readonly IList<ExplorerEntryData>? applyLayout;
        private readonly IList<ExplorerEntryData>? oppositeLayout;

        public LayoutUndoRedoChange(
            SceneExplorerViewModel owner,
            SceneAdapter sceneAdapter,
            Scene scene,
            IList<ExplorerEntryData>? applyLayout,
            IList<ExplorerEntryData>? oppositeLayout)
        {
            this.owner = owner;
            this.sceneAdapter = sceneAdapter;
            this.scene = scene;
            this.applyLayout = applyLayout;
            this.oppositeLayout = oppositeLayout;
        }

        public override void Apply()
        {
            this.owner.ApplyLayoutRestore(this.sceneAdapter, this.scene, this.applyLayout);

            UndoRedo.Default[this.owner].AddChange(
                new LayoutUndoRedoChange(
                    this.owner,
                    this.sceneAdapter,
                    this.scene,
                    this.oppositeLayout,
                    this.applyLayout)
                {
                    Key = this.Key,
                });
        }
    }

    private sealed class MoveItemsWithFolderRemovalUndoChange : Change
    {
        private readonly SceneExplorerViewModel owner;
        private readonly SceneAdapter sceneAdapter;
        private readonly Scene scene;
        private readonly IList<ExplorerEntryData>? moveUndoLayout;      // folder only, items out
        private readonly IList<ExplorerEntryData>? createUndoLayout;    // previous layout (no folder)
        private readonly IList<ExplorerEntryData>? moveRedoLayout;      // folder with items
        private readonly string folderName;

        public MoveItemsWithFolderRemovalUndoChange(
            SceneExplorerViewModel owner,
            SceneAdapter sceneAdapter,
            Scene scene,
            IList<ExplorerEntryData>? moveUndoLayout,
            IList<ExplorerEntryData>? createUndoLayout,
            IList<ExplorerEntryData>? moveRedoLayout,
            string folderName)
        {
            this.owner = owner;
            this.sceneAdapter = sceneAdapter;
            this.scene = scene;
            this.moveUndoLayout = CloneLayout(moveUndoLayout);
            this.createUndoLayout = CloneLayout(createUndoLayout);
            this.moveRedoLayout = CloneLayout(moveRedoLayout);
            this.folderName = folderName;
        }

        public override void Apply()
        {
            this.owner.ApplyLayoutRestore(this.sceneAdapter, this.scene, this.moveUndoLayout);

            // When undoing move-items, push a single redo change to move them back in
            UndoRedo.Default[this.owner].AddChange(
                new LayoutUndoRedoChange(
                    this.owner,
                    this.sceneAdapter,
                    this.scene,
                    this.moveRedoLayout,
                    this.moveUndoLayout)
                {
                    Key = $"Move Items To Folder ({this.folderName})",
                });
        }
    }

    // Build a top-level ExplorerLayout that mirrors the current Scene.RootNodes.
    private static IList<ExplorerEntryData> BuildLayoutFromRootNodes(Scene scene)
    {
        var list = new List<ExplorerEntryData>();
        foreach (var node in scene.RootNodes)
        {
            list.Add(new ExplorerEntryData { Type = "Node", NodeId = node.Id });
        }

        return list;
    }

    // Clone layout deeply (simple recursive copy)
    private static IList<ExplorerEntryData>? CloneLayout(IList<ExplorerEntryData>? layout)
    {
        if (layout is null)
            return null;

        IList<ExplorerEntryData> CopyList(IList<ExplorerEntryData> src)
        {
            var res = new List<ExplorerEntryData>();
            foreach (var e in src)
            {
                res.Add(new ExplorerEntryData
                {
                    Type = e.Type,
                    NodeId = e.NodeId,
                    FolderId = e.FolderId,
                    Name = e.Name,
                    Children = e.Children is null ? null : CopyList(e.Children)
                });
            }

            return res;
        }

        return CopyList(layout);
    }

    // Remove entries representing the provided node ids from layout recursively.
    // Removed node entries are added to collectedEntries (as new Node entries).
    private static void RemoveEntriesForNodeIds(IList<ExplorerEntryData> layout, HashSet<System.Guid> nodeIds, List<ExplorerEntryData> collectedEntries)
    {
        for (int i = layout.Count - 1; i >= 0; --i)
        {
            var entry = layout[i];
            if (string.Equals(entry.Type, "Node", StringComparison.OrdinalIgnoreCase) && entry.NodeId is not null && nodeIds.Contains(entry.NodeId.Value))
            {
                // collect and remove
                collectedEntries.Add(new ExplorerEntryData { Type = "Node", NodeId = entry.NodeId });
                layout.RemoveAt(i);
                continue;
            }

            if (string.Equals(entry.Type, "Folder", StringComparison.OrdinalIgnoreCase) && entry.Children is not null)
            {
                RemoveEntriesForNodeIds(entry.Children, nodeIds, collectedEntries);
                // Optionally remove empty folders — keep them for now (could be cleaned up later)
            }
        }
    }

    private bool CanCreateFolderFromSelection()
    {
        if (!this.HasUnlockedSelectedItems)
        {
            return false;
        }

        // Disallow when any folder is selected; allow scene root or node selections.
        return this.SelectionModel switch
        {
            null => true,
            SingleSelectionModel { SelectedItem: var item } => item is null || item is SceneAdapter || AsSceneNodeAdapter(item) is not null,
            MultipleSelectionModel<ITreeItem> multiple when multiple.SelectedIndices.Count == 0 => true,
            MultipleSelectionModel<ITreeItem> multiple => multiple.SelectedItems.All(i => AsSceneNodeAdapter(i) is not null),
            _ => false,
        };
    }

    private void NotifySelectionDependentCommands()
    {
        this.AddEntityCommand.NotifyCanExecuteChanged();
        this.CreateFolderFromSelectionCommand.NotifyCanExecuteChanged();
    }

    /// <summary>
    ///     Recursively searches the adapter tree starting at <paramref name="root"/> to find a
    ///     SceneNodeAdapter whose AttachedObject.Id matches <paramref name="nodeId"/>.
    /// </summary>
    private static async Task<TreeItemAdapter?> FindAdapterForNodeIdAsync(ITreeItem root, System.Guid nodeId)
    {
        // Check if the root itself matches the requested node id
        if (root is LayoutNodeAdapter lna && lna.AttachedObject.AttachedObject.Id == nodeId)
        {
            return lna;
        }

        if (root is SceneNodeAdapter sna && sna.AttachedObject.Id == nodeId)
        {
            return sna;
        }

        // Search through children recursively
        var children = await root.Children.ConfigureAwait(true);
        foreach (var child in children)
        {
            if (child is LayoutNodeAdapter childLayout && childLayout.AttachedObject.AttachedObject.Id == nodeId)
            {
                return childLayout;
            }

            if (child is SceneNodeAdapter childNode && childNode.AttachedObject.Id == nodeId)
            {
                return childNode;
            }

            var nested = await FindAdapterForNodeIdAsync(child, nodeId).ConfigureAwait(true);
            if (nested is not null)
            {
                return nested;
            }
        }

        return null;
    }

    private string GetNextEntityName()
    {
        var index = Interlocked.Increment(ref this.nextEntityIndex);
        return $"New Entity {index}";
    }

    private static SceneNodeAdapter? AsSceneNodeAdapter(ITreeItem? item)
        => item switch
        {
            SceneNodeAdapter sna => sna,
            LayoutNodeAdapter lna => lna.AttachedObject,
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
                case SceneNodeAdapter nodeAdapter:
                    return nodeAdapter.AttachedObject.Scene;
                case LayoutNodeAdapter layoutAdapter:
                    return layoutAdapter.AttachedObject.AttachedObject.Scene;
            }

            cursor = cursor.Parent;
        }

        return null;
    }

    private void TryApplyLayoutChange(SceneNodeAdapter adapter)
    {
        if (!this.pendingLayoutChanges.TryGetValue(adapter, out var layoutChange))
        {
            return;
        }

        _ = this.pendingLayoutChanges.Remove(adapter);
        this.ApplyLayoutChange(layoutChange);
    }

    private void ApplyLayoutRestore(SceneAdapter sceneAdapter, Scene scene, IList<ExplorerEntryData>? layout)
    {
        this.suppressUndoRecording = true;
        try
        {
            if (this.Scene is null)
            {
                this.Scene = sceneAdapter;
            }

            // Ensure the adapter is initialized before reloading children
            this.InitializeRootAsync(sceneAdapter, skipRoot: false).GetAwaiter().GetResult();

            var layoutClone = CloneLayout(layout);
            scene.ExplorerLayout = layoutClone;
            this.LogLayoutState("ApplyLayoutRestore", layoutClone);
            sceneAdapter.ReloadChildrenAsync().GetAwaiter().GetResult();
            this.InitializeRootAsync(sceneAdapter, skipRoot: false).GetAwaiter().GetResult();
        }
        finally
        {
            this.suppressUndoRecording = false;
        }
    }

    private void LogLayoutState(string label, IList<ExplorerEntryData>? layout)
    {
        if (!this.logger.IsEnabled(LogLevel.Debug))
        {
            return;
        }

        var entries = layout is null
            ? "<null>"
            : string.Join(", ", layout.Select(e =>
                e is null
                    ? "<null-entry>"
                    : $"{e.Type}:{e.NodeId?.ToString() ?? "-"}/F:{e.FolderId?.ToString() ?? "-"}/ChildCount:{e.Children?.Count ?? 0}"));

        this.LogLayoutEntries(label, entries);
    }

    private static HashSet<System.Guid> FilterTopLevelSelectedNodeIds(HashSet<System.Guid> selectedIds, Scene scene)
    {
        var topLevel = new HashSet<System.Guid>();
        foreach (var id in selectedIds)
        {
            if (HasAncestorInSelection(id, selectedIds, scene))
            {
                continue; // skip children when parent is also selected
            }

            _ = topLevel.Add(id);
        }

        return topLevel;
    }

    private static void EnsureLayoutContainsSelectedNodes(Scene scene, IEnumerable<System.Guid> selectedIds)
    {
        // Ensure layout exists
        scene.ExplorerLayout ??= BuildLayoutFromRootNodes(scene);

        foreach (var id in selectedIds)
        {
            if (LayoutContainsNode(scene.ExplorerLayout, id))
            {
                continue;
            }

            scene.ExplorerLayout.Add(new ExplorerEntryData { Type = "Node", NodeId = id });
        }

        static bool LayoutContainsNode(IList<ExplorerEntryData> layout, System.Guid nodeId)
        {
            foreach (var entry in layout)
            {
                if (string.Equals(entry.Type, "Node", StringComparison.OrdinalIgnoreCase) && entry.NodeId == nodeId)
                {
                    return true;
                }

                if (entry.Children is not null && LayoutContainsNode(entry.Children, nodeId))
                {
                    return true;
                }
            }

            return false;
        }
    }

    private static bool HasAncestorInSelection(System.Guid nodeId, HashSet<System.Guid> selectedIds, Scene scene)
    {
        var node = FindNodeById(scene, nodeId);
        var current = node?.Parent;
        while (current is not null)
        {
            if (selectedIds.Contains(current.Id))
            {
                return true;
            }

            current = current.Parent;
        }

        return false;
    }

    private static bool SceneContainsNode(Scene scene, System.Guid nodeId)
    {
        foreach (var root in scene.RootNodes)
        {
            if (root.Id == nodeId)
            {
                return true;
            }

            if (root.Descendants().Any(child => child.Id == nodeId))
            {
                return true;
            }
        }

        return false;
    }

    private static SceneNode? FindNodeById(Scene scene, System.Guid nodeId)
    {
        foreach (var root in scene.RootNodes)
        {
            var found = FindInTree(root);
            if (found is not null)
            {
                return found;
            }
        }

        return null;

        SceneNode? FindInTree(SceneNode node)
        {
            if (node.Id == nodeId)
            {
                return node;
            }

            foreach (var child in node.Children)
            {
                var found = FindInTree(child);
                if (found is not null)
                {
                    return found;
                }
            }

            return null;
        }
    }

    private async Task ApplySceneAdditionAsync(SceneNodeChangeRecord change)
    {
        ArgumentNullException.ThrowIfNull(change);

        if (!change.RequiresEngineSync)
        {
            return;
        }

        try
        {
            switch (change.OperationName)
            {
                case "CreateNodeAtRoot":
                case "CreateNodeUnderParent":
                    await this.sceneEngineSync.CreateNodeAsync(change.AffectedNode, change.NewParentId)
                        .ConfigureAwait(false);
                    change.AffectedNode.IsActive = true;
                    _ = this.messenger.Send(new SceneNodeAddedMessage(new[] { change.AffectedNode }));
                    break;

                case "ReparentNode":
                    if (!change.AffectedNode.IsActive)
                    {
                        this.LogCannotReparentNodeNotActive(change.AffectedNode.Name);
                        _ = this.messenger.Send(
                            new SceneNodeReparentedMessage(change.AffectedNode, change.OldParentId, change.NewParentId));
                        break;
                    }

                    await this.sceneEngineSync.ReparentNodeAsync(change.AffectedNode.Id, change.NewParentId)
                        .ConfigureAwait(false);
                    _ = this.messenger.Send(
                        new SceneNodeReparentedMessage(change.AffectedNode, change.OldParentId, change.NewParentId));
                    break;

                default:
                    this.LogNoSceneAdditionAction(change.OperationName);
                    break;
            }
        }
        catch (Exception ex)
        {
            this.LogFailedApplySceneChange(ex, change.OperationName, change.AffectedNode.Name, change.AffectedNode.Id);
        }
    }

    private async Task ApplySceneRemovalAsync(SceneNodeChangeRecord change, bool broadcastRemoval)
    {
        ArgumentNullException.ThrowIfNull(change);

        if (!string.Equals(change.OperationName, "RemoveNode", StringComparison.Ordinal))
        {
            this.LogSkippingRemovalHandler(change.OperationName);
            return;
        }

        try
        {
            if (change.RequiresEngineSync)
            {
                if (!change.AffectedNode.IsActive)
                {
                    this.LogCannotRemoveNodeNotActive(change.AffectedNode.Name);
                }
                else
                {
                    await this.sceneEngineSync.RemoveNodeAsync(change.AffectedNode.Id).ConfigureAwait(false);
                    change.AffectedNode.IsActive = false;
                }
            }

            if (broadcastRemoval)
            {
                _ = this.messenger.Send(new SceneNodeRemovedMessage(new[] { change.AffectedNode }));
            }
        }
        catch (Exception ex)
        {
            this.LogFailedFinalizeRemoval(ex, change.AffectedNode.Name, change.AffectedNode.Id);
        }
    }

    private void ApplyLayoutChange(LayoutChangeRecord change)
    {
        ArgumentNullException.ThrowIfNull(change);

        var scene = this.Scene?.AttachedObject;
        if (scene is null)
        {
            this.LogCannotApplyLayoutNoActiveScene();
            return;
        }

        scene.ExplorerLayout = change.NewLayout;
    }
}
