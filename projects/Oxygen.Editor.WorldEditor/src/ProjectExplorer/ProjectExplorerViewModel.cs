// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Collections.Specialized;
using System.ComponentModel;
using System.Diagnostics;
using CommunityToolkit.Mvvm.Input;
using CommunityToolkit.WinUI;
using DroidNet.Controls;
using DroidNet.Controls.Selection;
using DroidNet.Hosting.WinUI;
using DroidNet.Routing;
using DroidNet.TimeMachine;
using DroidNet.TimeMachine.Changes;
using Microsoft.Extensions.Logging;
using Microsoft.UI.Dispatching;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.WorldEditor.ProjectExplorer;

/// <summary>
/// The ViewModel for the <see cref="ProjectExplorerView" /> view.
/// </summary>
public sealed partial class ProjectExplorerViewModel : DynamicTreeViewModel, IRoutingAware, IDisposable
{
    private readonly ILogger<ProjectExplorerViewModel> logger;
    private readonly IProject currentProject;

    private bool isDisposed;
    private readonly DispatcherQueue dispatcher;
    private readonly IProjectManagerService projectManager;

    /// <summary>
    /// Initializes a new instance of the <see cref="ProjectExplorerViewModel"/> class.
    /// </summary>
    /// <param name="logger"></param>
    public ProjectExplorerViewModel(HostingContext hostingContext, IProjectManagerService projectManager, ILogger<ProjectExplorerViewModel> logger)
    {
        this.logger = logger;
        this.dispatcher = hostingContext.Dispatcher;
        this.projectManager = projectManager;

        Debug.Assert(projectManager.CurrentProject is not null, "must have a current project");
        this.currentProject = projectManager.CurrentProject;
        this.currentProject.PropertyChanged += this.OnCurrentProjectPropertyChangedAsync;

        /* TODO: subscribe to CurrentProject property changes */

        this.UndoStack = UndoRedo.Default[this].UndoStack;
        this.RedoStack = UndoRedo.Default[this].RedoStack;

        this.ItemBeingRemoved += this.OnItemBeingRemoved;
        this.ItemRemoved += this.OnItemRemoved;

        this.ItemBeingAdded += this.OnItemBeingAdded;
        this.ItemAdded += this.OnItemAdded;
    }

    public SceneAdapter? Scene { get; private set; }

    public ReadOnlyObservableCollection<IChange> UndoStack { get; }

    public ReadOnlyObservableCollection<IChange> RedoStack { get; }

    private bool HasUnlockedSelectedItems { get; set; }

    /// <inheritdoc/>
    public async Task OnNavigatedToAsync(IActiveRoute route, INavigationContext navigationContext) => await this.LoadActiveSceneAsync().ConfigureAwait(false);

    /// <inheritdoc/>
    public void Dispose()
    {
        if (this.isDisposed)
        {
            return;
        }

        this.isDisposed = true;

        this.currentProject.PropertyChanged -= this.OnCurrentProjectPropertyChangedAsync;
    }

    private void OnCurrentProjectPropertyChangedAsync(object? sender, PropertyChangedEventArgs args)
    {
        if (args.PropertyName is not nameof(IProject.ActiveScene))
        {
            return;
        }

        _ = this.dispatcher.EnqueueAsync(this.LoadActiveSceneAsync).ConfigureAwait(true);
    }

    /// <inheritdoc/>
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
                ((INotifyCollectionChanged)oldSelectionModel.SelectedItems).CollectionChanged -= this.OnMultipleSelectionChanged;
            }

            if (this.SelectionModel is MultipleSelectionModel<ITreeItem> currentSelectionModel)
            {
                ((INotifyCollectionChanged)currentSelectionModel.SelectedIndices).CollectionChanged += this.OnMultipleSelectionChanged;
            }
        }
    }

    /// <inheritdoc/>
    [RelayCommand(CanExecute = nameof(HasUnlockedSelectedItems))]
    protected override async Task RemoveSelectedItems()
    {
        UndoRedo.Default[this].BeginChangeSet($"Remove {this.SelectionModel}");
        await base.RemoveSelectedItems().ConfigureAwait(false);
        UndoRedo.Default[this].EndChangeSet();
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

        this.Scene = new SceneAdapter(scene) { IsExpanded = true, IsLocked = true, IsRoot = true };
        await this.InitializeRootAsync(this.Scene, skipRoot: false).ConfigureAwait(true);
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

    [RelayCommand]
    private void Undo() => UndoRedo.Default[this].Undo();

    [RelayCommand]
    private void Redo() => UndoRedo.Default[this].Redo();

    private void OnSingleSelectionChanged(object? sender, PropertyChangedEventArgs args)
    {
        if (!string.Equals(args.PropertyName, nameof(SelectionModel<ITreeItem>.SelectedIndex), StringComparison.Ordinal))
        {
            return;
        }

        this.AddEntityCommand.NotifyCanExecuteChanged();

        var hasUnlockedSelectedItems = this.SelectionModel?.SelectedItem?.IsLocked == false;
        if (hasUnlockedSelectedItems == this.HasUnlockedSelectedItems)
        {
            return;
        }

        this.HasUnlockedSelectedItems = hasUnlockedSelectedItems;
        this.RemoveSelectedItemsCommand.NotifyCanExecuteChanged();
    }

    private void OnMultipleSelectionChanged(object? sender, NotifyCollectionChangedEventArgs args)
    {
        if (this.SelectionModel is not MultipleSelectionModel<ITreeItem> multipleSelectionModel)
        {
            return;
        }

        this.AddEntityCommand.NotifyCanExecuteChanged();

        var hasUnlockedSelectedItems = false;
        foreach (var selectedIndex in multipleSelectionModel.SelectedIndices)
        {
            var item = this.ShownItems[selectedIndex];
            hasUnlockedSelectedItems = !item.IsLocked;
            if (hasUnlockedSelectedItems)
            {
                break;
            }
        }

        if (hasUnlockedSelectedItems == this.HasUnlockedSelectedItems)
        {
            return;
        }

        this.HasUnlockedSelectedItems = hasUnlockedSelectedItems;
        this.RemoveSelectedItemsCommand.NotifyCanExecuteChanged();
    }

    private void OnItemBeingAdded(object? sender, TreeItemBeingAddedEventArgs args)
    {
        _ = sender; // unused

        switch (args.TreeItem)
        {
            case SceneAdapter sceneAdapter:
                {
                    var scene = sceneAdapter.AttachedObject;
                    var parentAdapter = args.Parent as ProjectAdapter;
                    Debug.Assert(parentAdapter is not null, "the parent of a SceneAdpater must be a ProjectAdapter");
                    var project = parentAdapter.AttachedObject;
                    project.Scenes.Add(scene);
                    break;
                }

            case GameEntityAdapter entityAdapter:
                {
                    var entity = entityAdapter.AttachedObject;
                    var parentAdapter = args.Parent as SceneAdapter;
                    Debug.Assert(parentAdapter is not null, "the parent of a EntityAdapter must be a SceneAdapter");
                    var scene = parentAdapter.AttachedObject;
                    scene.Entities.Add(entity);
                    break;
                }

            default:
                // Do nothing
                break;
        }
    }

    private void OnItemBeingRemoved(object? sender, TreeItemBeingRemovedEventArgs args)
    {
        _ = sender; // unused

        Debug.Assert(args.TreeItem.Parent is not null, "any item in the tree should have a parent");
        switch (args.TreeItem)
        {
            case SceneAdapter sceneAdapter:
                {
                    var scene = sceneAdapter.AttachedObject;
                    var parentAdapter = sceneAdapter.Parent as ProjectAdapter;
                    Debug.Assert(parentAdapter is not null, "the parent of a SceneAdapter must be a ProjectAdapter");
                    var project = parentAdapter.AttachedObject;
                    _ = project.Scenes.Remove(scene);
                    break;
                }

            case GameEntityAdapter entityAdapter:
                {
                    var entity = entityAdapter.AttachedObject;
                    var parentAdapter = entityAdapter.Parent as SceneAdapter;
                    Debug.Assert(parentAdapter is not null, "the parent of a EntityAdapter must be a SceneAdapter");
                    var scene = parentAdapter.AttachedObject;
                    _ = scene.Entities.Remove(entity);
                    break;
                }

            default:
                // Do nothing
                break;
        }
    }

#if false
    [RelayCommand]
    private async Task AddScene()
    {
        if (this.Project is null)
        {
            return;
        }

        var newScene = new SceneAdapter(
            new Scene(this.Project.AttachedObject)
            {
                Name = $"New Scene {this.Project.AttachedObject.Scenes.Count}",
            });

        var selectedItem = this.SelectionModel?.SelectedItem;
        while (selectedItem is not null && selectedItem is not SceneAdapter && selectedItem.Parent is not null)
        {
            selectedItem = selectedItem.Parent;
        }

        if (selectedItem is null)
        {
            await this.InsertItemAsync(0, this.Project, newScene).ConfigureAwait(false);
            return;
        }

        Debug.Assert(
            selectedItem.Parent == this.Project,
            "if we reach here, we must have selected a scene or crawled up to a scene");

        var selectedItemRelativeIndex = (await this.Project.Children.ConfigureAwait(false)).IndexOf(selectedItem) + 1;
        await this.InsertItemAsync(selectedItemRelativeIndex, this.Project, newScene).ConfigureAwait(false);
    }
#endif

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
            = new GameEntityAdapter(
                new GameEntity(parent.AttachedObject)
                {
                    Name = $"New Entity {parent.AttachedObject.Entities.Count}",
                });

        await this.InsertItemAsync(relativeIndex, parent, newEntity).ConfigureAwait(false);
    }

    [LoggerMessage(
        EventName = $"ui-{nameof(ProjectExplorerViewModel)}-ItemAdded",
        Level = LogLevel.Information,
        Message = "Item added: `{ItemName}`")]
    private partial void LogItemAdded(string itemName);

    [LoggerMessage(
        EventName = $"ui-{nameof(ProjectExplorerViewModel)}-ItemRemoved",
        Level = LogLevel.Information,
        Message = "Item removed: `{ItemName}`")]
    private partial void LogItemRemoved(string itemName);
}
