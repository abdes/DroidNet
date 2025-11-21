// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Collections.Specialized;
using System.ComponentModel;
using System.Diagnostics;
using System.Reactive.Linq;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using CommunityToolkit.Mvvm.Messaging;
using CommunityToolkit.WinUI;
using DroidNet.Controls;
using DroidNet.Controls.Selection;
using DroidNet.Hosting.WinUI;
using DroidNet.Routing;
using DroidNet.Routing.Events;
using DroidNet.TimeMachine;
using DroidNet.TimeMachine.Changes;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Dispatching;
using Oxygen.Editor.Projects;
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
    private IDisposable? activationStartedSubscription;

    private bool isDisposed;

    /// <summary>
    ///     Initializes a new instance of the <see cref="SceneExplorerViewModel" /> class.
    /// </summary>
    /// <param name="hostingContext">The hosting context for the application.</param>
    /// <param name="projectManager">The project manager service.</param>
    /// <param name="messenger">The messenger service used for cross-component communication.</param>
    /// <param name="router">The router service used for navigation events.</param>
    /// <param name="loggerFactory">
    ///     Optional factory for creating loggers. If provided, enables detailed logging of the
    ///     recognition process. If <see langword="null" />, logging is disabled.
    /// </param>
    public SceneExplorerViewModel(
        HostingContext hostingContext,
        IProjectManagerService projectManager,
        IMessenger messenger,
        IRouter router,
        ILoggerFactory? loggerFactory = null)
    {
        this.logger = loggerFactory?.CreateLogger<SceneExplorerViewModel>() ??
                      NullLoggerFactory.Instance.CreateLogger<SceneExplorerViewModel>();
        this.dispatcher = hostingContext.Dispatcher;
        this.projectManager = projectManager;
        this.messenger = messenger;
        this.router = router;

        Debug.Assert(projectManager.CurrentProject is not null, "must have a current project");
        this.currentProject = projectManager.CurrentProject;
        this.currentProject.PropertyChanged += this.OnCurrentProjectPropertyChanged;

        this.UndoStack = UndoRedo.Default[this].UndoStack;
        this.RedoStack = UndoRedo.Default[this].RedoStack;

        this.ItemBeingRemoved += this.OnItemBeingRemoved;
        this.ItemRemoved += this.OnItemRemoved;

        this.ItemBeingAdded += this.OnItemBeingAdded;
        this.ItemAdded += this.OnItemAdded;

        messenger.Register<SceneNodeSelectionRequestMessage>(this, this.OnSceneNodeSelectionRequested);
        this.activationStartedSubscription = this.router.Events.OfType<ActivationStarted>().Subscribe(this.OnActivationStarted);
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

        this.currentProject.PropertyChanged -= this.OnCurrentProjectPropertyChanged;

        this.messenger.UnregisterAll(this);
        this.activationStartedSubscription?.Dispose();

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

    private void OnActivationStarted(ActivationStarted e)
        => _ = this.dispatcher.EnqueueAsync(this.LoadActiveSceneAsync);

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
                new SceneNode(parent.AttachedObject) { Name = $"New Entity {parent.AttachedObject.Nodes.Count}" });

        await this.InsertItemAsync(relativeIndex, parent, newEntity).ConfigureAwait(false);
    }

    private void OnCurrentProjectPropertyChanged(object? sender, PropertyChangedEventArgs args)
    {
        if (args.PropertyName is not nameof(IProject.ActiveScene))
        {
            return;
        }

        _ = this.dispatcher.EnqueueAsync(this.LoadActiveSceneAsync).ConfigureAwait(true);
    }

    private async Task LoadActiveSceneAsync()
    {
        var scene = this.currentProject.ActiveScene;
        if (scene is null)
        {
            return;
        }

        if (!await this.projectManager.LoadSceneAsync(scene).ConfigureAwait(true))
        {
            return;
        }

        // Request the document host to open the scene editor
        _ = this.messenger.Send(new OpenSceneRequestMessage(scene));

        this.Scene = new SceneAdapter(scene) { IsExpanded = true, IsLocked = true, IsRoot = true };
        await this.InitializeRootAsync(this.Scene, false).ConfigureAwait(true);
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
        scene.Nodes.Add(entity);
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
        _ = scene.Nodes.Remove(entity);
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
