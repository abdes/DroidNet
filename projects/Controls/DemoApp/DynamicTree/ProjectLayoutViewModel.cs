// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Diagnostics;
using System.Globalization;
using CommunityToolkit.Mvvm.ComponentModel;
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
        this.ItemBeingMoved += this.OnItemBeingMoved;
        this.ItemAdded += this.OnItemAdded;

        // Default the selection mode for the demo app to allow multiple selection.
        this.SelectionMode = SelectionMode.Multiple;

        // Watch clipboard state changes to update paste command state.
        this.ClipboardContentChanged += this.OnClipboardContentChanged;

        // If focus changes within the tree, re-evaluate paste availability
        this.PropertyChanged += (sender, args) =>
        {
            if (args.PropertyName?.Equals(nameof(this.FocusedItem), StringComparison.Ordinal) == true)
            {
                this.PasteCommand.NotifyCanExecuteChanged();
            }
        };
    }

    /// <summary>
    /// Fired when the ViewModel requests a rename operation in the View.
    /// </summary>
    public event EventHandler<RenameRequestedEventArgs?>? RenameRequested;

    [ObservableProperty]
    public partial string? OperationError { get; set; }

    [ObservableProperty]
    public partial bool OperationErrorVisible { get; set; }

    /// <summary>
    /// Gets the undo stack.
    /// </summary>
    public ReadOnlyObservableCollection<IChange> UndoStack { get; }

    /// <summary>
    /// Gets the redo stack.
    /// </summary>
    public ReadOnlyObservableCollection<IChange> RedoStack { get; }

    /// <summary>
    /// Gets the project adapter.
    /// </summary>
    internal ProjectAdapter? Project { get; private set; }

    private bool HasUnlockedSelectedItems { get; set; }

    /// <inheritdoc/>
    [RelayCommand(CanExecute = nameof(HasUnlockedSelectedItems))]
    public override async Task RemoveSelectedItems()
    {
        UndoRedo.Default[this].BeginChangeSet($"Remove {this.SelectionModel}");
        await base.RemoveSelectedItems().ConfigureAwait(false);
        UndoRedo.Default[this].EndChangeSet();
    }

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

    private static string GetNextAvailableSceneName(Project project, string baseName)
    {
        if (project is null)
        {
            return baseName;
        }

        var existing = new HashSet<string>(project.Scenes.Select(s => s.Name), StringComparer.OrdinalIgnoreCase);
        var index = 1;
        while (existing.Contains(string.Create(CultureInfo.InvariantCulture, $"{baseName} {index}")))
        {
            index++;
        }

        return string.Create(CultureInfo.InvariantCulture, $"{baseName} {index}");
    }

    private static string GetNextAvailableEntityName(ITreeItem parent, string baseName)
    {
        var names = Enumerable.Empty<string>();

        // Keep it simple: only ensure sibling uniqueness under the chosen parent.
        if (parent is SceneAdapter sa)
        {
            names = sa.AttachedObject.Entities.Select(e => e.Name);
        }
        else if (parent is EntityAdapter ea)
        {
            names = ea.AttachedObject.Entities.Select(e => e.Name);
        }

        var existing = new HashSet<string>(names, StringComparer.OrdinalIgnoreCase);
        var index = 1;
        while (existing.Contains(string.Create(CultureInfo.InvariantCulture, $"{baseName} {index}")))
        {
            index++;
        }

        return string.Create(CultureInfo.InvariantCulture, $"{baseName} {index}");
    }

    // Note: Do not expose UI-only types such as Visibility in the ViewModel; UI should convert string-based OperationError into visibility via a converter.
    partial void OnOperationErrorChanged(string? value)
    {
        this.OperationErrorVisible = !string.IsNullOrEmpty(value);
        this.LogOperationErrorChanged(value);
    }

    [RelayCommand]
    private void ClearOperationError() => this.OperationError = null;

    private void OnItemAdded(object? sender, TreeItemAddedEventArgs args)
    {
        _ = sender; // unused
        this.OperationError = null;

        // Update the underlying model now that the tree insertion succeeded.
        switch (args.TreeItem)
        {
            case SceneAdapter sceneAdapter:
                {
                    var scene = sceneAdapter.AttachedObject;
                    if (args.Parent is ProjectAdapter parentProject)
                    {
                        var project = parentProject.AttachedObject;
                        var idx = Math.Clamp(args.RelativeIndex, 0, project.Scenes.Count);
                        project.Scenes.Insert(idx, scene);
                    }

                    break;
                }

            case EntityAdapter entityAdapter:
                {
                    var entity = entityAdapter.AttachedObject;
                    switch (args.Parent)
                    {
                        case SceneAdapter parentScene:
                            {
                                var scene = parentScene.AttachedObject;
                                var idx = Math.Clamp(args.RelativeIndex, 0, scene.Entities.Count);
                                scene.Entities.Insert(idx, entity);
                                break;
                            }

                        case EntityAdapter parentEntity:
                            {
                                var pe = parentEntity.AttachedObject;
                                var idx = Math.Clamp(args.RelativeIndex, 0, pe.Entities.Count);
                                pe.Entities.Insert(idx, entity);
                                break;
                            }

                        default:
                            Debug.Fail("Unsupported parent type for EntityAdapter in OnItemAdded");
                            break;
                    }

                    break;
                }

            default:
                // no model update required
                break;
        }

        UndoRedo.Default[this]
            .AddChange(
                $"RemoveItemAsync({args.TreeItem.Label})",
                () => this.RemoveItemAsync(args.TreeItem).GetAwaiter().GetResult());

        this.LogItemAdded(args.TreeItem.Label);
    }

    private void OnClipboardContentChanged(object? sender, ClipboardContentChangedEventArgs args)
        => this.PasteCommand.NotifyCanExecuteChanged();

    private void OnItemRemoved(object? sender, TreeItemRemovedEventArgs args)
    {
        _ = sender; // unused
        this.OperationError = null;

        // Update underlying domain model to reflect the removal from the tree.
        switch (args.TreeItem)
        {
            case SceneAdapter sceneAdapter:
                {
                    var scene = sceneAdapter.AttachedObject;
                    var parentAdapter = args.Parent as ProjectAdapter;
                    Debug.Assert(parentAdapter is not null, "the parent of a SceneAdapter must be a ProjectAdapter");
                    var project = parentAdapter.AttachedObject;
                    _ = project.Scenes.Remove(scene);
                    break;
                }

            case EntityAdapter entityAdapter:
                {
                    var entity = entityAdapter.AttachedObject;
                    switch (args.Parent)
                    {
                        case SceneAdapter parentScene:
                            _ = parentScene.AttachedObject.Entities.Remove(entity);
                            break;

                        case EntityAdapter parentEntity:
                            _ = parentEntity.AttachedObject.Entities.Remove(entity);
                            break;

                        default:
                            Debug.Fail("Unsupported parent type for EntityAdapter");
                            break;
                    }

                    break;
                }

            default:
                // Do nothing
                break;
        }

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

    private bool CanCopy() => this.SelectionModel?.IsEmpty == false;

    /// <summary>
    /// Copies the selected items into the clipboard.
    /// </summary>
    [RelayCommand(CanExecute = nameof(CanCopy))]
    private async Task Copy()
    {
        var items = this.GetSelectedItems();
        if (items.Count == 0)
        {
            return;
        }

        await this.CopyItemsAsync(items).ConfigureAwait(true);
        this.PasteCommand.NotifyCanExecuteChanged();
    }

    private bool CanCut()
    {
        var items = this.GetSelectedItems();
        return items.Count > 0 && items.Exists(i => !i.IsLocked);
    }

    /// <summary>
    /// Cuts the selected items into the clipboard.
    /// </summary>
    [RelayCommand(CanExecute = nameof(CanCut))]
    private async Task Cut()
    {
        var items = this.GetSelectedItems().Where(i => !i.IsLocked).ToList();
        if (items.Count == 0)
        {
            return;
        }

        await this.CutItemsAsync(items).ConfigureAwait(true);
        this.PasteCommand.NotifyCanExecuteChanged();
    }

    private bool CanPaste() => this.CurrentClipboardState != ClipboardState.Empty
        && this.IsClipboardValid
        && ((this.FocusedItem is not null) || (this.SelectionModel?.IsEmpty == false));

    /// <summary>
    /// Pastes clipboard items into the current target.
    /// </summary>
    [RelayCommand(CanExecute = nameof(CanPaste))]
    private async Task Paste()
    {
        // If we have a focused item, use it; otherwise, if there's a selection, use the first selected item.
        var targetParent = this.FocusedItem;
        if (targetParent is null && this.SelectionModel?.IsEmpty == false)
        {
            var selected = this.GetSelectedItems();
            targetParent = selected.FirstOrDefault();
        }

        await this.PasteItemsAsync(targetParent).ConfigureAwait(true);
    }

    private bool CanRename() => this.SelectionModel?.IsEmpty == false;

    /// <summary>
    /// Requests the view to start in-place rename on the currently selected item.
    /// </summary>
    [RelayCommand(CanExecute = nameof(CanRename))]
    private void Rename()
    {
        var item = this.SelectedItem;
        this.RenameRequested?.Invoke(this, new RenameRequestedEventArgs(item));
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Style", "IDE0046:Convert to conditional expression", Justification = "code is more readable like this")]
    private List<ITreeItem> GetSelectedItems()
    {
        if (this.SelectionModel is null)
        {
            return [];
        }

        if (this.SelectionModel is MultipleSelectionModel multi2)
        {
            return [.. multi2.SelectedIndices.Select(i => this.ShownItems[i]).Cast<ITreeItem>()];
        }

        if (this.SelectionModel is SingleSelectionModel && this.SelectedItem is not null)
        {
            return [this.SelectedItem];
        }

        return [];
    }

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
            this.CopyCommand.NotifyCanExecuteChanged();
            this.CutCommand.NotifyCanExecuteChanged();
            this.RenameCommand.NotifyCanExecuteChanged();
        }
        else
        {
            // If the selection changed but unlocked state is the same, still update command availability.
            this.CopyCommand.NotifyCanExecuteChanged();
            this.CutCommand.NotifyCanExecuteChanged();
            this.RenameCommand.NotifyCanExecuteChanged();
            this.PasteCommand.NotifyCanExecuteChanged();
        }
    }

    private void OnItemBeingAdded(object? sender, TreeItemBeingAddedEventArgs args)
    {
        _ = sender; // unused
        this.OperationError = null;

        switch (args.TreeItem)
        {
            case SceneAdapter:
                {
                    if (args.Parent is not ProjectAdapter)
                    {
                        args.Proceed = false;
                        this.OperationError = "Scenes can only be added to a Project.";
                        this.LogSceneAddRejectedParentNotProject(args.Parent?.GetType());
                        return;
                    }

                    break;
                }

            case EntityAdapter:
                {
                    if (args.Parent is not SceneAdapter and not EntityAdapter)
                    {
                        args.Proceed = false;
                        this.OperationError = "Entities can only be added to a Scene or another Entity.";
                        this.LogEntityAddRejectedParentType(args.Parent?.GetType());
                        return;
                    }

                    if (!args.Parent.CanAcceptChildren)
                    {
                        args.Proceed = false;
                        this.OperationError = "Target parent does not accept children.";
                        this.LogEntityAddRejectedParentDoesNotAcceptChildren();
                        return;
                    }

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
        this.OperationError = null; // Reset operation error

        if (args.TreeItem.IsLocked)
        {
            args.Proceed = false;
            this.OperationError = "Cannot remove a locked item.";
            this.LogRemoveRejectedItemIsLocked(args.TreeItem);
            return;
        }

        if (args.TreeItem.Parent is null)
        {
            args.Proceed = false;
            this.OperationError = "Cannot remove an orphan item.";
            this.LogRemoveRejectedOrphanItem(args.TreeItem);
            return;
        }

        // Only validation should occur here; model mutation must occur in OnItemRemoved after the tree actually removed the item.
        switch (args.TreeItem)
        {
            case SceneAdapter:
                if (args.TreeItem.Parent is not ProjectAdapter)
                {
                    args.Proceed = false;
                    this.OperationError = "Scene can only be removed from a Project.";
                    this.LogRemoveRejectedSceneParentNotProject(args.TreeItem.Parent.GetType());
                    return;
                }

                break;

            case EntityAdapter:
                var parentEntity = args.TreeItem.Parent;
                if (parentEntity is not SceneAdapter and not EntityAdapter)
                {
                    args.Proceed = false;
                    this.OperationError = "Entity parent is invalid; cannot remove.";
                    this.LogRemoveRejectedEntityParentInvalid(args.TreeItem.Parent.GetType());
                    return;
                }

                break;
        }
    }

    private void OnItemBeingMoved(object? sender, TreeItemBeingMovedEventArgs args)
    {
        _ = sender; // unused
        this.OperationError = null;

        switch (args.TreeItem)
        {
            case SceneAdapter:
                {
                    if (args.NewParent is not ProjectAdapter)
                    {
                        args.Proceed = false;
                        args.VetoReason = "Scenes can only be moved under a Project.";
                        this.OperationError = args.VetoReason;
                        this.LogMoveRejectedReason(args.VetoReason);
                    }

                    break;
                }

            case EntityAdapter:
                {
                    if (args.NewParent is not SceneAdapter and not EntityAdapter)
                    {
                        args.Proceed = false;
                        args.VetoReason = "Entities can only be moved under a Scene or another Entity.";
                        this.OperationError = args.VetoReason;
                        this.LogMoveRejectedReason(args.VetoReason);
                        return;
                    }

                    if (!args.NewParent.CanAcceptChildren)
                    {
                        args.Proceed = false;
                        args.VetoReason = "Target parent does not accept children.";
                        this.OperationError = args.VetoReason;
                        this.LogMoveRejectedReason(args.VetoReason);
                    }

                    break;
                }

            default:
                // nothing special
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

        // Keep the project expanded by default so the tree displays its children at startup.
        this.Project = new ProjectAdapter(project)
        {
            IsExpanded = true,
        };
        await this.InitializeRootAsync(this.Project, skipRoot: false).ConfigureAwait(false);
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

        var name = GetNextAvailableSceneName(this.Project.AttachedObject, "New Scene");
        var newScene = new SceneAdapter(new Scene(name));
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

        var parent = selectedItem switch
        {
            SceneAdapter s => (ITreeItem)s,
            EntityAdapter e => (ITreeItem)e,
            _ => null,
        };

        if (parent is null)
        {
            return;
        }

        var name = GetNextAvailableEntityName(parent, "New Entity");
        var newEntity = new EntityAdapter(new Entity(name));
        await this.InsertItemAsync(0, parent, newEntity).ConfigureAwait(false);
    }
}
