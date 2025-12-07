// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Collections.Specialized;
using System.ComponentModel;
using System.Diagnostics;
using System.Linq;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using CommunityToolkit.Mvvm.Messaging;
using CommunityToolkit.WinUI;
using DroidNet.Controls;
using DroidNet.Controls.Selection;
using DroidNet.Documents;
using DroidNet.Hosting.WinUI;
using DroidNet.Routing;
using DroidNet.TimeMachine;
using DroidNet.TimeMachine.Changes;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Dispatching;
using Oxygen.Editor.Core;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Oxygen.Editor.WorldEditor.Messages;
using Oxygen.Editor.WorldEditor.Services;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.WorldEditor.SceneExplorer;

/// <summary>
///     The ViewModel for the <see cref="SceneExplorer.SceneExplorerView" /> view.
/// </summary>
public sealed partial class SceneExplorerViewModel : DynamicTreeViewModel, IDisposable
{
    private readonly IProject currentProject;
    private readonly DispatcherQueue dispatcher;
    private readonly ILogger<SceneExplorerViewModel> logger;
    private readonly IMessenger messenger;
    private readonly IRouter router;
    private readonly IProjectManagerService projectManager;
    private readonly IDocumentService documentService;
    private readonly ISceneEngineSync sceneEngineSync;

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
    /// <param name="hostingContext">The hosting context for the application.</param>
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
        HostingContext hostingContext,
        IProjectManagerService projectManager,
        IMessenger messenger,
        IRouter router,
        IDocumentService documentService,
        ISceneEngineSync sceneEngineSync,
        ILoggerFactory? loggerFactory = null)
    {
        this.logger = loggerFactory?.CreateLogger<SceneExplorerViewModel>() ??
                      NullLoggerFactory.Instance.CreateLogger<SceneExplorerViewModel>();
        this.dispatcher = hostingContext.Dispatcher;
        this.projectManager = projectManager;
        this.messenger = messenger;
        this.router = router;
        this.documentService = documentService;
        this.sceneEngineSync = sceneEngineSync;

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
        => (this.SelectionModel is SingleSelectionModel && this.SelectionModel.SelectedIndex != -1) ||
           this.SelectionModel is MultipleSelectionModel { SelectedIndices.Count: 1 };

    [RelayCommand(CanExecute = nameof(CanAddEntity))]
    private async Task AddEntity()
    {
        var selectedItem = this.SelectionModel?.SelectedItem;
        if (selectedItem is null)
        {
            return;
        }

        var relativeIndex = 0;
        SceneAdapter? parent;
        if (selectedItem is SceneAdapter sceneAdapter)
        {
            parent = sceneAdapter;
        }
        else
        {
            parent = selectedItem.Parent as SceneAdapter;

            // As of new, game entities are only created under a scene parent
            Debug.Assert(parent is not null, "every entity must have a scene parent");
            relativeIndex = (await parent.Children.ConfigureAwait(false)).IndexOf(selectedItem) + 1;
        }

        var newEntity
            = new SceneNodeAdapter(
                new SceneNode(parent.AttachedObject) { Name = $"New Entity {parent.AttachedObject.RootNodes.Count}" });

        await this.InsertItemAsync(relativeIndex, parent, newEntity).ConfigureAwait(false);
    }

    private void OnDocumentOpened(object? sender, DroidNet.Documents.DocumentOpenedEventArgs e)
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
        _ = this.dispatcher.EnqueueAsync(() => this.LoadSceneAsync(scene)).ConfigureAwait(true);
    }

    private async Task LoadSceneAsync(Scene scene)
    {
        if (!await this.projectManager.LoadSceneAsync(scene).ConfigureAwait(true))
        {
            return;
        }

        this.Scene = new SceneAdapter(scene) { IsExpanded = true, IsLocked = true, IsRoot = true };
        await this.InitializeRootAsync(this.Scene, skipRoot: false).ConfigureAwait(true);

        // Delegate scene synchronization to the service
        await this.sceneEngineSync.SyncSceneAsync(scene).ConfigureAwait(true);
    }

    private void OnItemAdded(object? sender, TreeItemAddedEventArgs args)
    {
        _ = sender; // unused

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
                            this.logger.LogWarning(ex, "Undo RemoveItem failed for '{Item}'", item.Label);
                        }
                    });
        }

        this.LogItemAdded(args.TreeItem.Label);

        // No extra model messages required here — creation/reparent is handled
        // in the BeingAdded handler where the model mutation and engine sync occur.
    }

    private void OnItemRemoved(object? sender, TreeItemRemovedEventArgs args)
    {
        _ = sender; // unused

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
                                this.logger.LogWarning("Cannot undo add: original parent was null for '{Item}'", item.Label);
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
                            this.logger.LogWarning(ex, "Undo AddItem failed for '{Item}'", item.Label);
                        }
                    });
        }

        this.LogItemRemoved(args.TreeItem.Label);

        // Only broadcast removals that represent actual deletions from the scene.
        // Suppress removal notifications for moves and reparent operations.
        if (args.TreeItem is SceneNodeAdapter removedAdapter)
        {
            if (this.deletedAdapters.Remove(removedAdapter))
            {
                _ = this.messenger.Send(new SceneNodeRemovedMessage(new[] { removedAdapter.AttachedObject }));
            }

            // If we had previously captured an old parent for a move, clean it up.
            _ = this.capturedOldParent.Remove(removedAdapter);
        }
    }

    private void OnSceneNodeSelectionRequested(object recipient, SceneNodeSelectionRequestMessage message)
    {
        switch (this.SelectionModel)
        {
            case SingleSelectionModel singleSelection:
                var selectedItem = singleSelection.SelectedItem;
                message.Reply(selectedItem is null
                    ? Array.Empty<SceneNode>()
                    : new[] { ((SceneNodeAdapter)selectedItem).AttachedObject });
                break;

            case MultipleSelectionModel<ITreeItem> multipleSelection:
                var selection = multipleSelection.SelectedItems.Where(item => item is SceneNodeAdapter)
                    .Select(adapter => ((SceneNodeAdapter)adapter).AttachedObject).ToList();
                message.Reply(selection);
                break;

            default:
                message.Reply(Array.Empty<SceneNode>());
                break;
        }
    }

    private void OnSingleSelectionChanged(object? sender, PropertyChangedEventArgs args)
    {
        if (!string.Equals(args.PropertyName, nameof(SelectionModel<>.SelectedIndex), StringComparison.Ordinal))
        {
            return;
        }

        this.AddEntityCommand.NotifyCanExecuteChanged();

        this.HasUnlockedSelectedItems = this.SelectionModel?.SelectedItem?.IsLocked == false;

        var selected = this.SelectionModel?.SelectedItem is not SceneNodeAdapter adapter
            ? Array.Empty<SceneNode>()
            : new[] { adapter.AttachedObject };

        _ = this.messenger.Send(new SceneNodeSelectionChangedMessage(selected));
    }

    private void OnMultipleSelectionChanged(object? sender, NotifyCollectionChangedEventArgs args)
    {
        if (this.SelectionModel is not MultipleSelectionModel<ITreeItem> multipleSelectionModel)
        {
            return;
        }

        this.AddEntityCommand.NotifyCanExecuteChanged();

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
            .Where(item => item is SceneNodeAdapter)
            .Select(adapter => ((SceneNodeAdapter)adapter).AttachedObject)
            .ToList();

        _ = this.messenger.Send(new SceneNodeSelectionChangedMessage(selection));
    }

    private async void OnItemBeingAdded(object? sender, TreeItemBeingAddedEventArgs args)
    {
        _ = sender; // unused

        if (args.TreeItem is not SceneNodeAdapter entityAdapter)
        {
            return;
        }

        var entity = entityAdapter.AttachedObject;

        // If this addition follows a 'remove' that was not a delete, we may have
        // recorded the previous parent so we can detect a scene-node -> scene-node
        // reparent. Handle reparent and create operations here (mutate model/engine
        // once) — layout-only moves (folders) do not touch the model.

        // Evaluate an adapter added under a scene node (reparent) or scene root (create).
        var newParent = args.Parent;
        if (newParent is SceneNodeAdapter newNodeParent)
        {
            // Determine old parent id (prefer the captured value from a prior removal)
            var oldParentId = this.capturedOldParent.TryGetValue(entityAdapter, out var captured)
                ? captured as System.Guid?
                : entity.Parent?.Id;

            var newParentId = newNodeParent.AttachedObject.Id;
            if (oldParentId != newParentId)
            {
                try
                {
                    // Update the scene model, then tell the engine
                    entity.SetParent(newNodeParent.AttachedObject);
                    _ = this.capturedOldParent.Remove(entityAdapter);

                    if (entity.IsActive)
                    {
                        await this.sceneEngineSync.ReparentNodeAsync(entity.Id, newParentId).ConfigureAwait(false);
                        _ = this.messenger.Send(new SceneNodeReparentedMessage(entity, oldParentId, newParentId));
                    }
                    else
                    {
                        // Node wasn't active in the engine yet — create it with the parent
                        await this.sceneEngineSync.CreateNodeAsync(entity, parentGuid: newParentId).ConfigureAwait(false);
                        _ = this.messenger.Send(new SceneNodeAddedMessage(new[] { entity }));
                    }
                }
                catch (Exception ex)
                {
                    this.logger.LogError(ex, "Failed to reparent node '{NodeName}'", entity.Name);
                }
            }

            // Reparent completed or no-op; do not treat this as a create.
            return;
        }

        // If the item is being added directly under the scene adapter, this is a runtime
        // entity creation: ensure the Entity is present in Scene.RootNodes and sync with
        // the engine. Otherwise the item is being inserted under a FolderAdapter and we
        // should only update the ExplorerLayout (if present) — do NOT alter the runtime
        // scene graph or call the engine in that case.
        if (args.Parent is SceneAdapter directSceneParent)
        {
            // If the adapter wasn't already a member of RootNodes then this is a creation.
                var scene = directSceneParent.AttachedObject;

                // If we previously captured an old parent for this adapter, this is
                // a node->root reparent rather than a pure creation. Apply the model
                // mutation and notify the engine.
                if (this.capturedOldParent.TryGetValue(entityAdapter, out var oldParentId))
                {
                    try
                    {
                        entity.SetParent(newParent: null);
                        _ = this.capturedOldParent.Remove(entityAdapter);
                        await this.sceneEngineSync.ReparentNodeAsync(entity.Id, null).ConfigureAwait(false);
                        _ = this.messenger.Send(new SceneNodeReparentedMessage(entity, oldParentId, null));
                    }
                    catch (Exception ex)
                    {
                        this.logger.LogError(ex, "Failed to reparent node '{NodeName}' to root", entity.Name);
                    }
                }

                if (!scene.RootNodes.Contains(entity))
            {
                scene.RootNodes.Add(entity);

                // Keep ExplorerLayout in sync if it already exists. If ExplorerLayout is
                // null we intentionally leave it null (the tree falls back to RootNodes when
                // displaying). If a layout exists make sure the new node has a corresponding
                // Node entry so commands which operate on layout (folders) can reference it.
                if (scene.ExplorerLayout is not null)
                {
                    // Avoid duplicates
                    static bool ContainsNode(IList<ExplorerEntryData>? entries, System.Guid id)
                    {
                        if (entries is null) return false;
                        foreach (var e in entries)
                        {
                            if (string.Equals(e.Type, "Node", StringComparison.OrdinalIgnoreCase) && e.NodeId == id)
                                return true;

                            if (e.Children is not null && ContainsNode(e.Children, id))
                                return true;
                        }

                        return false;
                    }

                    if (!ContainsNode(scene.ExplorerLayout, entity.Id))
                    {
                        scene.ExplorerLayout.Add(new ExplorerEntryData { Type = "Node", NodeId = entity.Id });
                        this.logger.LogDebug("Updated ExplorerLayout: added node entry for '{NodeName}' ({NodeId})", entity.Name, entity.Id);
                    }
                }
            }

            // Delegate to service for engine synchronization for newly-created runtime nodes
            try
            {
                if (!entity.IsActive)
                {
                    await this.sceneEngineSync.CreateNodeAsync(entity).ConfigureAwait(false);
                    this.logger.LogDebug("Synced new node '{NodeName}' with engine", entity.Name);
                    _ = this.messenger.Send(new SceneNodeAddedMessage(new[] { entity }));
                }
                else
                {
                    // Node already active in engine: nothing to create here.
                }
            }
            catch (Exception ex)
            {
                this.logger.LogError(ex, "Failed to sync new node '{NodeName}' with engine", entity.Name);
            }

            // Clean up any transient old-parent capture for this adapter (folder moves)
            _ = this.capturedOldParent.Remove(entityAdapter);

            return;
        }

        // Otherwise, this is an adapter added under a folder (or nested under folders).
        // If the scene has a persisted layout, ensure we add a Node entry under the
        // corresponding folder entry so layout-driven behaviors can find the node later.
        // We attempt to find the scene via ancestors.
        var ancestor = args.Parent;
        SceneAdapter? sceneAncestor = null;
        while (ancestor is not null && sceneAncestor is null)
        {
            if (ancestor is SceneAdapter sa)
            {
                sceneAncestor = sa;
                break;
            }

            ancestor = ancestor.Parent;
        }

        if (sceneAncestor is not null && sceneAncestor.AttachedObject.ExplorerLayout is not null)
        {
            var scene = sceneAncestor.AttachedObject;

            // Find nearest folder ancestor and ensure a Node entry is added if missing
            var p = args.Parent;
            Guid? folderId = null;
            while (p is not null)
            {
                if (p is FolderAdapter fa)
                {
                    folderId = fa.Id;
                    break;
                }

                p = p.Parent;
            }

            if (folderId is not null)
            {
                (ExplorerEntryData? entry, IList<ExplorerEntryData>? parent) FindFolderEntryWithParent(IList<ExplorerEntryData>? entries, Guid id, IList<ExplorerEntryData>? parent = null)
                {
                    if (entries is null) return (null, null);
                    foreach (var e in entries)
                    {
                        if (string.Equals(e.Type, "Folder", StringComparison.OrdinalIgnoreCase) && e.FolderId == id)
                            return (e, parent ?? entries);

                        var found = FindFolderEntryWithParent(e.Children, id, e.Children);
                        if (found.entry is not null) return found;
                    }

                    return (null, null);
                }

                var (target, parentList) = FindFolderEntryWithParent(scene.ExplorerLayout, folderId.Value);
                if (target is not null && parentList is not null)
                {
                    var already = target.Children?.Any(c => string.Equals(c.Type, "Node", StringComparison.OrdinalIgnoreCase) && c.NodeId == entity.Id) ?? false;
                    if (!already)
                    {
                        var newNode = new ExplorerEntryData { Type = "Node", NodeId = entity.Id };
                        if (target.Children is null)
                        {
                            // create a new ExplorerEntryData with a fresh children list and replace the item
                            var replacement = target with { Children = new List<ExplorerEntryData> { newNode } };
                            var idx = parentList.IndexOf(target);
                            if (idx >= 0)
                            {
                                parentList[idx] = replacement;
                                target = replacement; // update local reference
                            }
                        }
                        else
                        {
                            // safe to mutate existing children list
                            target.Children.Add(newNode);
                        }

                        this.logger.LogDebug("Updated ExplorerLayout: added node entry for '{NodeName}' ({NodeId}) under folder {FolderId}", entity.Name, entity.Id, folderId);
                    }
                }
            }
        }

        // Delegate to service for engine synchronization
        // Using async void for event handler is acceptable here as we handle all exceptions
        try
        {
            await this.sceneEngineSync.CreateNodeAsync(entity).ConfigureAwait(false);
            this.logger.LogDebug("Synced new node '{NodeName}' with engine", entity.Name);
        }
        catch (Exception ex)
        {
            this.logger.LogError(ex, "Failed to sync new node '{NodeName}' with engine", entity.Name);
        }
    }

    private async void OnItemBeingRemoved(object? sender, TreeItemBeingRemovedEventArgs args)
    {
        _ = sender; // unused

        if (args.TreeItem is not SceneNodeAdapter entityAdapter)
        {
            return;
        }

        var entity = entityAdapter.AttachedObject;

        // If this is a non-delete removal it may be part of a move. Capture the
        // old parent id so the subsequent add can detect a node->node reparent.
        if (!this.isPerformingDelete)
        {
            if (entityAdapter.Parent is SceneNodeAdapter oldParentNode)
            {
                this.capturedOldParent[entityAdapter] = oldParentNode.AttachedObject.Id;
            }
        }

        // Find the nearest SceneAdapter ancestor so we can operate on the Scene model
        ITreeItem? ancestor = entityAdapter.Parent;
        SceneAdapter? sceneAdapter = null;
        while (ancestor is not null && sceneAdapter is null)
        {
            if (ancestor is SceneAdapter sa)
            {
                sceneAdapter = sa;
                break;
            }

            ancestor = ancestor.Parent;
        }

        // Only remove the node from scene.RootNodes when we're performing an actual delete
        // operation. Moving a node to/from folders should not affect the runtime scene graph.
        if (this.isPerformingDelete)
        {
            // Record that we deleted this adapter so OnItemRemoved can broadcast
            // the removal message after the tree finishes its own work.
            _ = this.deletedAdapters.Add(entityAdapter);

            if (sceneAdapter is not null)
            {
            _ = sceneAdapter.AttachedObject.RootNodes.Remove(entity);

                // Keep ExplorerLayout in sync: remove any ExplorerEntryData that references
                // this node id so future layout-based operations won't reference a deleted node.
                if (sceneAdapter.AttachedObject.ExplorerLayout is not null)
                {
                    var removed = new List<ExplorerEntryData>();
                    RemoveEntriesForNodeIds(sceneAdapter.AttachedObject.ExplorerLayout, new HashSet<System.Guid> { entity.Id }, removed);
                    if (removed.Count > 0)
                    {
                        this.logger.LogDebug("Updated ExplorerLayout: removed {Count} entries for deleted node {NodeName} ({NodeId})", removed.Count, entity.Name, entity.Id);
                    }
                }
            }

            // Delegate to service for engine synchronization
            if (!entity.IsActive)
            {
                this.logger.LogWarning("Cannot remove node '{NodeName}' from engine: no handle available", entity.Name);
                return;
            }

            try
            {
                await this.sceneEngineSync.RemoveNodeAsync(entity.Id).ConfigureAwait(false);
                this.logger.LogDebug("Removed node '{NodeName}' from engine", entity.Name);
            }
            catch (Exception ex)
            {
                this.logger.LogError(ex, "Failed to remove node '{NodeName}' from engine", entity.Name);
            }
        }
    }

    [RelayCommand(CanExecute = nameof(SceneExplorerViewModel.HasUnlockedSelectedItems))]
    private async Task CreateFolderFromSelection()
    {
        this.LogCreateFolderInvoked(this.SelectionModel?.GetType().Name, this.ShownItems.Count);
        // Collect selected scene node ids by capturing the live selection right at
        // command invocation. If that yields nothing, fall back to the shown-items
        // (adapters flagged IsSelected).
        var selectedIds = new HashSet<System.Guid>();

        if (this.SelectionModel is SingleSelectionModel single)
        {
            if (single.SelectedItem is SceneNodeAdapter sna)
            {
                selectedIds.Add(sna.AttachedObject.Id);
            }
        }
        else if (this.SelectionModel is MultipleSelectionModel<ITreeItem> multiple)
        {
            foreach (var item in multiple.SelectedItems)
            {
                if (item is SceneNodeAdapter nodeAdapter)
                {
                    selectedIds.Add(nodeAdapter.AttachedObject.Id);
                }
            }
        }

        // Fallback: if cached/SelectionModel didn't give us anything, fall back to inspecting
        // the shown items collection for adapters that are flagged IsSelected.
        var usedShownItemsFallback = false;
        if (selectedIds.Count == 0)
        {
            foreach (var shown in this.ShownItems)
            {
                if (!shown.IsSelected)
                {
                    continue;
                }

                if (shown is SceneNodeAdapter shownNode)
                {
                    selectedIds.Add(shownNode.AttachedObject.Id);
                    usedShownItemsFallback = true;
                }
            }
        }

        // Log captured ids (useful for debugging why folders end up empty)
        var idsCsv = string.Join(',', selectedIds.Select(g => g.ToString()));
        this.LogCreateFolderCapturedSelection(idsCsv);

        if (selectedIds.Count == 0)
        {
            // If we inspected shown items, log that fallback produced zero results
            if (usedShownItemsFallback)
            {
                this.LogCreateFolderUsingShownItems(selectedIds.Count);
            }

            this.LogCreateFolderNoSelection();
            return; // nothing to group
        }

        var sceneAdapter = this.Scene;
        if (sceneAdapter is null)
        {
            return;
        }

        var scene = sceneAdapter.AttachedObject;

        // Clone previous layout for undo
        var previousLayout = CloneLayout(scene.ExplorerLayout);

        // Ensure we have a working, mutable layout to mutate
        var layout = scene.ExplorerLayout?.ToList() ?? BuildLayoutFromRootNodes(scene);

        // Log layout info before mutating it
        var topCount = layout.Count;
        var totalCount = 0;
        void CountEntries(IList<ExplorerEntryData>? entries)
        {
            if (entries is null)
                return;

            totalCount += entries.Count;
            foreach (var e in entries)
            {
                CountEntries(e.Children);
            }
        }

        CountEntries(layout);
        this.LogCreateFolderLayoutInfo(topCount, totalCount);

        // Remove selected nodes from the layout and collect entries for them
        var movedEntries = new List<ExplorerEntryData>();
        RemoveEntriesForNodeIds(layout, selectedIds, movedEntries);

        this.LogCreateFolderRemovedEntriesCount(movedEntries.Count);

        // Create a folder entry and insert it at the beginning of top-level
        var folder = new ExplorerEntryData
        {
            Type = "Folder",
            FolderId = System.Guid.NewGuid(),
            Name = "New Folder",
            Children = movedEntries.Count > 0 ? movedEntries : []
        };

        layout.Insert(0, folder);

        // Persist layout and update adapters in-place so we preserve expand/collapse state
        scene.ExplorerLayout = layout;

        // Insert folder adapter at top-level using tree APIs so shown items / parents are updated.
        var folderAdapter = new FolderAdapter(folder.FolderId!.Value, folder.Name ?? "New Folder");

        // Insert folder at index 0 under the scene adapter. Suppress undo recording
        // while we perform the programmatic adapter modifications so we don't create
        // duplicate change entries for every individual insert/remove event.
        this.suppressUndoRecording = true;
        try
        {
            await this.InsertItemAsync(0, sceneAdapter, folderAdapter).ConfigureAwait(true);

        // Find adapters corresponding to moved node ids and move them into the new folder adapter.
        var movedAdapterCount = 0;
        foreach (var moved in movedEntries)
        {
            if (moved.NodeId is null)
                continue;

            // Locate the scene node adapter in the current adapter tree
            var nodeAdapter = await FindAdapterForNodeIdAsync(sceneAdapter, moved.NodeId.Value).ConfigureAwait(true);
            if (nodeAdapter is null)
            {
                this.LogCreateFolderNodeAdapterNotFound(moved.NodeId.Value);
                continue;
            }

            // Remove the adapter from its current location and insert under the folder
            await this.RemoveItemAsync(nodeAdapter, updateSelection: false).ConfigureAwait(true);
            await this.InsertItemAsync(folderAdapter.ChildrenCount, folderAdapter, nodeAdapter).ConfigureAwait(true);
            ++movedAdapterCount;
        }

        this.LogCreateFolderMovedAdaptersCount(movedAdapterCount);
        }
        finally
        {
            this.suppressUndoRecording = false;
        }

        // Ensure folder is expanded and selected for immediate visibility
        if (!folderAdapter.IsExpanded)
            await this.ExpandItemAsync(folderAdapter).ConfigureAwait(true);
        this.ClearAndSelectItem(folderAdapter);

        // Try to find the newly created folder adapter and expand it so the user
        // immediately sees the moved items. Also select the folder for feedback.
            try
            {
                var children = await sceneAdapter.Children.ConfigureAwait(true);
                var foundFolderAdapter = children.OfType<FolderAdapter>().FirstOrDefault(f => f.Id == folder.FolderId);
                if (foundFolderAdapter is not null)
            {
                // Expand and select the new folder so children become visible
                    if (!foundFolderAdapter.IsExpanded)
                {
                        await this.ExpandItemAsync(foundFolderAdapter).ConfigureAwait(true);
                }

                    // Select the new folder to give immediate visual feedback
                    this.ClearAndSelectItem(foundFolderAdapter);
                this.logger.LogDebug("Auto-expanded and selected new folder {FolderName} ({FolderId})", folder.Name, folder.FolderId);
            }
            else
            {
                    this.logger.LogDebug("Created folder '{FolderName}' inserted into layout but no matching FolderAdapter was found in scene adapter children", folder.Name);
            }
        }
        catch (Exception ex)
        {
            this.logger.LogWarning(ex, "Failed to auto-expand/select created folder");
        }

        this.LogCreateFolderCreated(folder.Name!, folder.Children?.Count ?? 0, scene.Id);

        // Add undo action to restore previous layout, and push a redo action when undo runs
        UndoRedo.Default[this].AddChange(
            $"Create Folder ({folder.Name})",
            () =>
            {
                // Undo: restore previous layout and refresh adapters. Suppress creating
                // undo entries from the per-item events while we do this programmatically.
                this.suppressUndoRecording = true;
                try
                {
                    scene.ExplorerLayout = previousLayout;
                    sceneAdapter.ReloadChildrenAsync().GetAwaiter().GetResult();
                    this.InitializeRootAsync(sceneAdapter, skipRoot: false).GetAwaiter().GetResult();
                }
                finally
                {
                    this.suppressUndoRecording = false;
                }

                // When undo is executed, push a redo operation that reapplies the created layout
                UndoRedo.Default[this].AddChange(
                    $"Redo Create Folder ({folder.Name})",
                    () =>
                    {
                        this.suppressUndoRecording = true;
                        try
                        {
                            scene.ExplorerLayout = layout;
                            sceneAdapter.ReloadChildrenAsync().GetAwaiter().GetResult();
                            this.InitializeRootAsync(sceneAdapter, skipRoot: false).GetAwaiter().GetResult();
                        }
                        finally
                        {
                            this.suppressUndoRecording = false;
                        }
                    });
            });

        // No cached selection to clear when capturing selection at command time
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

    /// <summary>
    ///     Recursively searches the adapter tree starting at <paramref name="root"/> to find a
    ///     SceneNodeAdapter whose AttachedObject.Id matches <paramref name="nodeId"/>.
    /// </summary>
    private static async Task<SceneNodeAdapter?> FindAdapterForNodeIdAsync(ITreeItem root, System.Guid nodeId)
    {
        // Check if the root itself is the scene node adapter we want
        if (root is SceneNodeAdapter sna && sna.AttachedObject.Id == nodeId)
        {
            return sna;
        }

        // Search through children recursively
        var children = await root.Children.ConfigureAwait(true);
        foreach (var child in children)
        {
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

    // Helper to obtain SceneNode id for parent adapters
    private static System.Guid? GetSceneNodeId(ITreeItem? item)
        => item is SceneNodeAdapter sna ? sna.AttachedObject.Id : null;

    // Previous internal scheduling helpers removed — operations are handled
    // directly and semantically in the BeingAdded/BeingRemoved handlers.

    // Scheduling operations — express intent in semantic methods
    // (previous schedule/commit helpers removed in favor of explicit, semantic
    // model operations handled in the 'Being' handlers above)

    [LoggerMessage(
        EventName = $"ui-{nameof(SceneExplorerViewModel)}-ItemAdded",
        Level = LogLevel.Information,
        Message = "Item added: `{ItemName}`")]
    private partial void LogItemAdded(string itemName);

    [LoggerMessage(
        EventName = $"ui-{nameof(SceneExplorerViewModel)}-ItemRemoved",
        Level = LogLevel.Information,
        Message = "Item removed: `{ItemName}`")]
    private partial void LogItemRemoved(string itemName);
}
