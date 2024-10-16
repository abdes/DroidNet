// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.WorldEditor.ProjectExplorer;

using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Diagnostics;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Controls;
using DroidNet.TimeMachine;
using DroidNet.TimeMachine.Changes;
using Oxygen.Editor.Projects;

/// <summary>
/// The ViewModel for the <see cref="ProjectExplorerView" /> view.
/// </summary>
public partial class ProjectExplorerViewModel : DynamicTreeViewModel
{
    private readonly IProjectManagerService projectManager;

    public ProjectExplorerViewModel(IProjectManagerService projectManager)
    {
        this.projectManager = projectManager;

        // TODO: subscribe to CurrentProject property changes

        this.UndoStack = UndoRedo.Default[this].UndoStack;
        this.RedoStack = UndoRedo.Default[this].RedoStack;

        this.ItemBeingRemoved += this.OnItemBeingRemoved;
        this.ItemRemoved += this.OnItemRemoved;

        this.ItemBeingAdded += this.OnItemBeingAdded;
        this.ItemAdded += this.OnItemAdded;
    }

    public ProjectAdapter? Project { get; private set; }

    public ReadOnlyObservableCollection<IChange> UndoStack { get; }

    public ReadOnlyObservableCollection<IChange> RedoStack { get; }

    private bool HasUnlockedSelectedItems { get; set; }

    protected override void OnSelectionModelChanged(SelectionModel<ITreeItem>? oldValue)
    {
        base.OnSelectionModelChanged(oldValue);

        if (oldValue is not null)
        {
            oldValue.PropertyChanged -= this.SelectionModel_OnPropertyChanged;
        }

        if (this.SelectionModel is not null)
        {
            this.SelectionModel.PropertyChanged += this.SelectionModel_OnPropertyChanged;
        }
    }

    [RelayCommand(CanExecute = nameof(HasUnlockedSelectedItems))]
    protected override async Task RemoveSelectedItems()
    {
        UndoRedo.Default[this].BeginChangeSet($"Remove {this.SelectionModel}");
        await base.RemoveSelectedItems().ConfigureAwait(false);
        UndoRedo.Default[this].EndChangeSet();
    }

    private void OnItemAdded(object? sender, ItemAddedEventArgs args)
    {
        _ = sender; // unused

        UndoRedo.Default[this]
            .AddChange(
                $"RemoveItem({args.TreeItem.Label})",
                () => this.RemoveItem(args.TreeItem).GetAwaiter().GetResult());
    }

    private void OnItemRemoved(object? sender, ItemRemovedEventArgs args)
    {
        _ = sender; // unused

        UndoRedo.Default[this]
            .AddChange(
                $"Add Item({args.TreeItem.Label})",
                () => this.AddItem(args.Parent, args.TreeItem).GetAwaiter().GetResult());
    }

    [RelayCommand]
    private void Undo() => UndoRedo.Default[this].Undo();

    [RelayCommand]
    private void Redo() => UndoRedo.Default[this].Redo();

    private void SelectionModel_OnPropertyChanged(object? sender, PropertyChangedEventArgs args)
    {
        if (!string.Equals(args.PropertyName, nameof(SelectionModel<ITreeItem>.IsEmpty), StringComparison.Ordinal))
        {
            return;
        }

        this.AddEntityCommand.NotifyCanExecuteChanged();

        var hasUnlockedSelectedItems = false;
        switch (this.SelectionModel)
        {
            case SingleSelectionModel:
                hasUnlockedSelectedItems = this.SelectionModel.SelectedItem?.IsLocked == false;
                break;

            case MultipleSelectionModel multipleSelectionModel:
                foreach (var selectedIndex in multipleSelectionModel.SelectedIndices)
                {
                    var item = this.ShownItems[selectedIndex];
                    hasUnlockedSelectedItems = !item.IsLocked;
                    if (hasUnlockedSelectedItems)
                    {
                        break;
                    }
                }

                break;

            default:
                // Keep it false
                break;
        }

        if (hasUnlockedSelectedItems != this.HasUnlockedSelectedItems)
        {
            this.HasUnlockedSelectedItems = hasUnlockedSelectedItems;
            this.RemoveSelectedItemsCommand.NotifyCanExecuteChanged();
        }
    }

    private void OnItemBeingAdded(object? sender, ItemBeingAddedEventArgs args)
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

    private void OnItemBeingRemoved(object? sender, ItemBeingRemovedEventArgs args)
    {
        _ = sender; // unused

        Debug.Assert(args.TreeItem.Parent is not null, "any item in the tree should have a parent");
        switch (args.TreeItem)
        {
            case SceneAdapter sceneAdapter:
            {
                var scene = sceneAdapter.AttachedObject;
                var parentAdapter = sceneAdapter.Parent as ProjectAdapter;
                Debug.Assert(parentAdapter is not null, "the parent of a SceneAdpater must be a ProjectAdapter");
                var project = parentAdapter.AttachedObject;
                project.Scenes.Remove(scene);
                break;
            }

            case GameEntityAdapter entityAdapter:
            {
                var entity = entityAdapter.AttachedObject;
                var parentAdapter = entityAdapter.Parent as SceneAdapter;
                Debug.Assert(parentAdapter is not null, "the parent of a EntityAdapter must be a SceneAdapter");
                var scene = parentAdapter.AttachedObject;
                scene.Entities.Remove(entity);
                break;
            }

            default:
                // Do nothing
                break;
        }
    }

    [RelayCommand]
    private async Task LoadProjectAsync()
    {
        var currentProject = this.projectManager.CurrentProject;
        if (currentProject is not Projects.Project project)
        {
            this.Project = null;
            return;
        }

        this.Project = new ProjectAdapter(project, this.projectManager);
        await this.InitializeRootAsync(this.Project).ConfigureAwait(false);
    }

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
            },
            this.projectManager);

        await this.AddItem(this.Project, newScene).ConfigureAwait(false);
    }

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

        var scene = selectedItem switch
        {
            SceneAdapter item => item,
            GameEntityAdapter { Parent: SceneAdapter } entity => (SceneAdapter)entity.Parent,

            // Anything else is not a valid item to which we can add an entity
            _ => null,
        };

        if (scene is null)
        {
            return;
        }

        var newEntity
            = new GameEntityAdapter(
                new GameEntity(scene.AttachedObject)
                {
                    Name = $"New Entity {scene.AttachedObject.Entities.Count}",
                });
        await this.AddItem(scene, newEntity).ConfigureAwait(false);
    }
}
