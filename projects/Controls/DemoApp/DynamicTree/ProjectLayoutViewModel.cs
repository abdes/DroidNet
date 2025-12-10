// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Diagnostics;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Controls.Demo.Model;
using DroidNet.Controls.Demo.Services;
using DroidNet.Controls.Selection;
using DroidNet.TimeMachine;
using DroidNet.TimeMachine.Changes;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;

namespace DroidNet.Controls.Demo.DynamicTree;

/// <summary>
/// The ViewModel for the <see cref="ProjectLayoutView"/> view.
/// </summary>
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "must be public for source generated MVVM")]
public partial class ProjectLayoutViewModel : DynamicTreeViewModel
{
    private readonly ILogger<ProjectLayoutViewModel> logger;

    /// <summary>
    /// Initializes a new instance of the <see cref="ProjectLayoutViewModel"/> class.
    /// </summary>
    /// <param name="loggerFactory">
    ///     The <see cref="ILoggerFactory" /> used to obtain an <see cref="ILogger" />. If the logger
    ///     cannot be obtained, a <see cref="NullLogger" /> is used silently.
    /// </param>
    public ProjectLayoutViewModel(ILoggerFactory? loggerFactory = null)
        : base(loggerFactory)
    {
        this.logger = loggerFactory?.CreateLogger<ProjectLayoutViewModel>() ?? NullLogger<ProjectLayoutViewModel>.Instance;

        this.UndoStack = UndoRedo.Default[this].UndoStack;
        this.RedoStack = UndoRedo.Default[this].RedoStack;

        this.ItemBeingRemoved += this.OnItemBeingRemoved;
        this.ItemRemoved += this.OnItemRemoved;

        this.ItemBeingAdded += this.OnItemBeingAdded;
        this.ItemAdded += this.OnItemAdded;

        // Default the selection mode for the demo app to allow multiple selection.
        this.SelectionMode = SelectionMode.Multiple;
    }

    /// <summary>
    /// Gets the project adapter.
    /// </summary>
    public ProjectAdapter? Project { get; private set; }

    /// <summary>
    /// Gets the undo stack.
    /// </summary>
    public ReadOnlyObservableCollection<IChange> UndoStack { get; }

    /// <summary>
    /// Gets the redo stack.
    /// </summary>
    public ReadOnlyObservableCollection<IChange> RedoStack { get; }

    private bool HasUnlockedSelectedItems { get; set; }

    /// <inheritdoc/>
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

    /// <inheritdoc/>
    [RelayCommand(CanExecute = nameof(HasUnlockedSelectedItems))]
    public override async Task RemoveSelectedItems()
    {
        UndoRedo.Default[this].BeginChangeSet($"Remove {this.SelectionModel}");
        await base.RemoveSelectedItems().ConfigureAwait(false);
        UndoRedo.Default[this].EndChangeSet();
    }

    private void OnItemAdded(object? sender, TreeItemAddedEventArgs args)
    {
        _ = sender; // unused

        UndoRedo.Default[this]
            .AddChange(
                $"RemoveItemAsync({args.TreeItem.Label})",
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
    }

    /// <summary>
    /// Undoes the last change.
    /// </summary>
    [RelayCommand]
    private void Undo() => UndoRedo.Default[this].Undo();

    /// <summary>
    /// Redoes the last undone change.
    /// </summary>
    [RelayCommand]
    private void Redo() => UndoRedo.Default[this].Redo();

    private void SelectionModel_OnPropertyChanged(object? sender, PropertyChangedEventArgs args)
    {
        if (!string.Equals(args.PropertyName, nameof(SelectionModel<>.IsEmpty), StringComparison.Ordinal))
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

    private void OnItemBeingAdded(object? sender, TreeItemBeingAddedEventArgs args)
    {
        _ = sender; // unused

        switch (args.TreeItem)
        {
            case SceneAdapter sceneAdapter:
                {
                    var scene = sceneAdapter.AttachedObject;
                    var parentAdapter = args.Parent as ProjectAdapter;
                    Debug.Assert(parentAdapter is not null, "the parent of a SceneAdapter must be a ProjectAdapter");
                    var project = parentAdapter.AttachedObject;
                    project.Scenes.Add(scene);
                    break;
                }

            case EntityAdapter entityAdapter:
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

            case EntityAdapter entityAdapter:
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

    /// <summary>
    /// Loads the project asynchronously.
    /// </summary>
    [RelayCommand]
    private async Task LoadProjectAsync()
    {
        var project = new Project("Sample Project");
        await ProjectLoaderService.LoadProjectAsync(project).ConfigureAwait(false);

        this.Project = new ProjectAdapter(project);
        await this.InitializeRootAsync(this.Project).ConfigureAwait(false);
    }

    /// <summary>
    /// Adds a new scene to the project.
    /// </summary>
    [RelayCommand]
    private async Task AddScene()
    {
        if (this.Project is null)
        {
            return;
        }

        var newScene
            = new SceneAdapter(new Scene($"New Scene {this.Project.AttachedObject.Scenes.Count}"));

        await this.InsertItemAsync(0, this.Project, newScene).ConfigureAwait(false);
    }

    /// <summary>
    /// Determines whether an entity can be added.
    /// </summary>
    /// <returns><see langword="true"/> if an entity can be added; otherwise, <see langword="false"/>.</returns>
    private bool CanAddEntity()
        => (this.SelectionModel is SingleSelectionModel && this.SelectionModel.SelectedIndex != -1) ||
           this.SelectionModel is MultipleSelectionModel { SelectedIndices.Count: 1 };

    /// <summary>
    /// Adds a new entity to the selected scene.
    /// </summary>
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
            EntityAdapter { Parent: SceneAdapter } entity => (SceneAdapter)entity.Parent,

            // Anything else is not a valid item to which we can add an entity
            _ => null,
        };

        if (scene is null)
        {
            return;
        }

        var newEntity
            = new EntityAdapter(new Entity($"New Entity {scene.AttachedObject.Entities.Count}"));
        await this.InsertItemAsync(0, scene, newEntity).ConfigureAwait(false);
    }

    /// <summary>
    /// Logs that an item was added.
    /// </summary>
    /// <param name="itemName">The name of the item that was added.</param>
    [LoggerMessage(
        EventName = $"ui-{nameof(ProjectLayoutViewModel)}-ItemRemoved",
        Level = LogLevel.Information,
        Message = "Item added: `{ItemName}`")]
    private partial void LogItemAdded(string itemName);
}
