// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Collections.Specialized;
using System.ComponentModel;
using System.Diagnostics;
using System.Linq;
using System.Numerics;
using System.Reactive.Linq;
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
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World;
using Oxygen.Editor.WorldEditor.Messages;

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
    private readonly IEngineService engineService;

    private bool isDisposed;


    /// <summary>
    ///     Initializes a new instance of the <see cref="SceneExplorerViewModel" /> class.
    /// </summary>
    /// <param name="hostingContext">The hosting context for the application.</param>
    /// <param name="projectManager">The project manager service.</param>
    /// <param name="messenger">The messenger service used for cross-component communication.</param>
    /// <param name="router">The router service used for navigation events.</param>
    /// <param name="documentService">The document service for handling document operations.</param>
    /// <param name="engineService">The engine service for interop with the rendering engine.</param>
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
        IEngineService engineService,
        ILoggerFactory? loggerFactory = null)
    {
        this.logger = loggerFactory?.CreateLogger<SceneExplorerViewModel>() ??
                      NullLoggerFactory.Instance.CreateLogger<SceneExplorerViewModel>();
        this.dispatcher = hostingContext.Dispatcher;
        this.projectManager = projectManager;
        this.messenger = messenger;
        this.router = router;
        this.documentService = documentService;
        this.engineService = engineService;

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
        UndoRedo.Default[this].BeginChangeSet($"Remove {this.SelectionModel}");
        await base.RemoveSelectedItems().ConfigureAwait(false);
        UndoRedo.Default[this].EndChangeSet();
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

        // Sync scene with the engine
        await this.SyncSceneWithEngineAsync(scene).ConfigureAwait(true);
    }

    private async Task SyncSceneWithEngineAsync(Scene scene)
    {
        var world = this.engineService.World;
        if (world is null)
        {
            this.logger.LogWarning("OxygenWorld is not available; skipping scene sync");
            return;
        }

        // Diagnostic: log transforms from the managed Scene model to verify
        // that hydratation produced the expected values before we call into
        // the native engine. This helps detect mismatches between the on-disk
        // JSON and the in-memory model used for sync.
        try
        {
            foreach (var root in scene.RootNodes)
            {
                this.LogNodeTransformRecursive(root, parentName: null);
            }
        }
        catch (Exception ex)
        {
            this.logger.LogWarning(ex, "Failed to dump scene transforms for debug");
        }

        try
        {
            // Create (or recreate) the scene in the engine
            world.CreateScene(scene.Name);
            this.logger.LogInformation("Created scene '{SceneName}' in engine", scene.Name);

            // Phase 1: create all nodes without parenting and collect handles
            var createTasks = new List<Task<bool>>();

            void EnqueueCreateRecursive(SceneNode node)
            {
                var tcs = new TaskCompletionSource<bool>(TaskCreationOptions.RunContinuationsAsynchronously);
                createTasks.Add(tcs.Task);

                // Enqueue creation with no parent so we obtain a stable handle
                // and initialize the node's world matrix as root to ensure the
                // engine's cached world matrix is valid if anything reads it
                // before the transform propagation step.
                world.CreateSceneNode(
                    node.Name,
                    node.Id,
                    parentGuid: null,
                    handle =>
                    {
                        try
                        {
                            node.IsActive = true;
                            tcs.SetResult(true);
                        }
                        catch (Exception ex)
                        {
                            tcs.SetException(ex);
                        }
                    },
                    initializeWorldAsRoot: true);

                foreach (var child in node.Children)
                {
                    EnqueueCreateRecursive(child);
                }
            }

            foreach (var root in scene.RootNodes)
            {
                EnqueueCreateRecursive(root);
            }

            // Await completion of all create operations without blocking the UI thread
            await Task.WhenAll(createTasks).ConfigureAwait(true);

            // Phase 2: resolve parent-child links deterministically and apply
            // transforms/geometry now that all handles are available.
            void ResolveAndApply(SceneNode node, Guid? parentGuid)
            {
                try
                {
                    if (!node.IsActive)
                    {
                        this.logger.LogError("Node '{NodeName}' creation was not successful", node.Name);
                        scene.RootNodes.Remove(node);
                    }
                    else
                    {
                        // Reparent to the desired parent (or make root when parent is null)
                        try
                        {
                            // Do not attempt to preserve world transform here —
                            // nodes were created detached and their world-space
                            // transform state may be stale. We'll apply the desired
                            // local transform after reparenting.
                            world.ReparentSceneNode(node.Id, parentGuid, preserveWorldTransform: false);
                        }
                        catch (Exception ex)
                        {
                            this.logger.LogError(ex, "Failed to reparent node '{NodeName}'", node.Name);
                        }

                        // Apply transform if present
                        var transform = node.Components.OfType<Transform>().FirstOrDefault();
                        if (transform is not null)
                        {
                            try
                            {
                                world.SetLocalTransform(
                                    node.Id,
                                    new Vector3(transform.LocalPosition.X, transform.LocalPosition.Y, transform.LocalPosition.Z),
                                    new Quaternion(transform.LocalRotation.X, transform.LocalRotation.Y, transform.LocalRotation.Z, transform.LocalRotation.W),
                                    new Vector3(transform.LocalScale.X, transform.LocalScale.Y, transform.LocalScale.Z));
                            }
                            catch (Exception ex)
                            {
                                this.logger.LogError(ex, "Failed to set transform for node '{NodeName}'", node.Name);
                            }
                        }

                        // Attach geometry if present
                        var geometryComp = node.Components.OfType<GeometryComponent>().FirstOrDefault();
                        if (geometryComp is not null)
                        {
                            try
                            {
                                var uri = geometryComp.Geometry.Uri?.ToString();
                                if (!string.IsNullOrEmpty(uri))
                                {
                                    var meshType = uri.Split('/', StringSplitOptions.RemoveEmptyEntries).LastOrDefault();
                                    if (!string.IsNullOrEmpty(meshType))
                                    {
                                        world.CreateBasicMesh(node.Id, meshType);
                                    }
                                }
                            }
                            catch (Exception ex)
                            {
                                this.logger.LogError(ex, "Failed to attach geometry for node '{NodeName}'", node.Name);
                            }
                        }
                    }
                }
                catch (Exception ex)
                {
                    this.logger.LogError(ex, "Error while resolving node '{NodeName}'", node.Name);
                }

                foreach (var child in node.Children)
                {
                    ResolveAndApply(child, node.Id);
                }
            }

            foreach (var root in scene.RootNodes)
            {
                ResolveAndApply(root, parentGuid: null);
            }

            // After applying parenting/transforms, request a targeted transform
            // propagation for all created root nodes so the engine's cached
            // world matrices are up-to-date before any renderer-side reads.
            try
            {
                var handles = new List<Guid>();
                void CollectChildren(SceneNode n)
                {
                    if (n.IsActive)
                    {
                        handles.Add(n.Id);
                    }

                    foreach (var c in n.Children)
                    {
                        CollectChildren(c);
                    }
                }

                foreach (var root in scene.RootNodes)
                {
                    CollectChildren(root);
                }

                if (handles.Count > 0)
                {
                    world.UpdateTransformsForNodes(handles.ToArray());
                }
            }
            catch (Exception ex)
            {
                this.logger.LogWarning(ex, "Failed to request targeted transform propagation");
            }
        }
        catch (Exception ex)
        {
            this.logger.LogError(ex, "Failed to sync scene '{SceneName}' with engine", scene.Name);
        }
    }

    private void LogNodeTransformRecursive(SceneNode node, string? parentName)
    {
        try
        {
            var t = node.Components.OfType<Transform>().FirstOrDefault();
            if (t is not null)
            {
                this.logger.LogInformation("SceneTransform: node='{NodeName}' parent='{ParentName}' pos=({X:0.00},{Y:0.00},{Z:0.00}) scale=({SX:0.00},{SY:0.00},{SZ:0.00}) rot=({RX:0.00},{RY:0.00},{RZ:0.00},{RW:0.00})",
                    node.Name,
                    parentName ?? "root",
                    t.LocalPosition.X, t.LocalPosition.Y, t.LocalPosition.Z,
                    t.LocalScale.X, t.LocalScale.Y, t.LocalScale.Z,
                    t.LocalRotation.X, t.LocalRotation.Y, t.LocalRotation.Z, t.LocalRotation.W);
            }
            else
            {
                this.logger.LogInformation("SceneTransform: node='{NodeName}' parent='{ParentName}' has NO Transform component", node.Name, parentName ?? "root");
            }

            foreach (var child in node.Children)
            {
                this.LogNodeTransformRecursive(child, node.Name);
            }
        }
        catch (Exception ex)
        {
            this.logger.LogWarning(ex, "Failed to log transform for node '{NodeName}'", node.Name);
        }
    }

    private void OnItemAdded(object? sender, TreeItemAddedEventArgs args)
    {
        _ = sender; // unused

        UndoRedo.Default[this]
            .AddChange(
                $"RemoveItem({args.TreeItem.Label})",
                () => this.RemoveItemAsync(args.TreeItem).GetAwaiter().GetResult());

        this.LogItemAdded(args.TreeItem.Label);
    }

    private void OnItemRemoved(object? sender, TreeItemRemovedEventArgs args)
    {
        _ = sender; // unused

        UndoRedo.Default[this]
            .AddChange(
                $"Add Item({args.TreeItem.Label})",
                () => this.InsertItemAsync(args.RelativeIndex, args.Parent, args.TreeItem).GetAwaiter().GetResult());

        this.LogItemRemoved(args.TreeItem.Label);
    }

    private void OnSceneNodeSelectionRequested(object recipient, SceneNodeSelectionRequestMessage message)
    {
        switch (this.SelectionModel)
        {
            case SingleSelectionModel singleSelection:
                var selectedItem = singleSelection.SelectedItem;
                message.Reply(selectedItem is null ? [] : [((SceneNodeAdapter)selectedItem).AttachedObject]);
                break;

            case MultipleSelectionModel<ITreeItem> multipleSelection:
                var selection = multipleSelection.SelectedItems.Where(item => item is SceneNodeAdapter)
                    .Select(adapter => ((SceneNodeAdapter)adapter).AttachedObject).ToList();
                message.Reply(selection);
                break;

            default:
                message.Reply([]);
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

        _ = this.messenger.Send(
            new SceneNodeSelectionChangedMessage(
                this.SelectionModel?.SelectedItem is not SceneNodeAdapter adapter
                    ? []
                    : [adapter.AttachedObject]));
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

    private void OnItemBeingAdded(object? sender, TreeItemBeingAddedEventArgs args)
    {
        _ = sender; // unused

        if (args.TreeItem is not SceneNodeAdapter entityAdapter)
        {
            return;
        }

        var entity = entityAdapter.AttachedObject;
        var parentAdapter = args.Parent as SceneAdapter;
        Debug.Assert(parentAdapter is not null, "the parent of a EntityAdapter must be a SceneAdapter");
        var scene = parentAdapter.AttachedObject;
        scene.RootNodes.Add(entity);

        // Sync with engine
        var world = this.engineService.World;
        if (world is not null)
        {
            try
            {
                // Create the node with callback to receive handle
                var tcs = new TaskCompletionSource<bool>();
                world.CreateSceneNode(
                    entity.Name,
                    entity.Id,
                    parentGuid: null,
                    handle =>
                    {
                        entity.IsActive = true;
                        tcs.SetResult(true);
                    },
                    initializeWorldAsRoot: true);

                // Wait for handle
                var nodeHandle = tcs.Task.Result;

                // Set initial transform if available
                var transform = entity.Components.OfType<Transform>().FirstOrDefault();
                if (transform is not null)
                {
                    world.SetLocalTransform(
                        entity.Id,
                        new Vector3(transform.LocalPosition.X, transform.LocalPosition.Y, transform.LocalPosition.Z),
                        new Quaternion(transform.LocalRotation.X, transform.LocalRotation.Y, transform.LocalRotation.Z, transform.LocalRotation.W),
                        new Vector3(transform.LocalScale.X, transform.LocalScale.Y, transform.LocalScale.Z));
                }

                this.logger.LogDebug("Synced new node '{NodeName}' with engine", entity.Name);
            }
            catch (Exception ex)
            {
                this.logger.LogError(ex, "Failed to sync new node '{NodeName}' with engine", entity.Name);
            }
        }
    }

    private void OnItemBeingRemoved(object? sender, TreeItemBeingRemovedEventArgs args)
    {
        _ = sender; // unused

        if (args.TreeItem is not SceneNodeAdapter entityAdapter)
        {
            return;
        }

        var entity = entityAdapter.AttachedObject;
        var parentAdapter = entityAdapter.Parent as SceneAdapter;
        Debug.Assert(parentAdapter is not null, "the parent of a EntityAdapter must be a SceneAdapter");
        var scene = parentAdapter.AttachedObject;
        _ = scene.RootNodes.Remove(entity);

        // Sync with engine
        var world = this.engineService.World;
        if (world is not null)
        {
            try
            {
                if (entity.IsActive)
                {
                    world.RemoveSceneNode(entity.Id);
                    this.logger.LogDebug("Removed node '{NodeName}' from engine", entity.Name);
                }
                else
                {
                    this.logger.LogWarning("Cannot remove node '{NodeName}' from engine: no handle available", entity.Name);
                }
            }
            catch (Exception ex)
            {
                this.logger.LogError(ex, "Failed to remove node '{NodeName}' from engine", entity.Name);
            }
        }
    }

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
