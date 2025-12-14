// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Collections.Specialized;
using System.ComponentModel;
using System.Globalization;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Controls.Demo.Tree.Model;
using DroidNet.Controls.Demo.Tree.Services;
using DroidNet.Controls.Selection;
using DroidNet.TimeMachine;
using DroidNet.TimeMachine.Changes;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;

namespace DroidNet.Controls.Demo.Tree;

/// <summary>
/// The ViewModel for the <see cref="ProjectLayoutView"/> view.
/// </summary>
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "must be public for source generated MVVM")]
public partial class ProjectLayoutViewModel : DynamicTreeViewModel, IDisposable
{
    private readonly ILogger<ProjectLayoutViewModel> logger;
    private readonly DomainModelService domainModelService;
    private bool isDisposed;
    private ProjectAdapter? projectAdapter;

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

        this.UndoStack = this.History.UndoStack;
        this.RedoStack = this.History.RedoStack;

        ((INotifyCollectionChanged)this.UndoStack).CollectionChanged += this.OnUndoStackCollectionChanged;
        ((INotifyCollectionChanged)this.RedoStack).CollectionChanged += this.OnRedoStackCollectionChanged;

        this.ItemBeingRemoved += this.OnItemBeingRemoved;
        this.ItemRemoved += this.OnItemRemoved;

        this.ItemBeingAdded += this.OnItemBeingAdded;
        this.ItemBeingMoved += this.OnItemBeingMoved;
        this.ItemMoved += this.OnItemMoved;
        this.ItemAdded += this.OnItemAdded;

        // Default the selection mode for the demo app to allow multiple selection.
        this.SelectionMode = SelectionMode.Multiple;

        // Watch clipboard state changes to update paste command state.
        this.ClipboardContentChanged += this.OnClipboardContentChanged;

        // If focus changes within the tree, re-evaluate paste availability
        this.PropertyChanged += this.ViewModel_PropertyChanged;

        this.domainModelService = new DomainModelService(loggerFactory);
    }

    /// <summary>
    /// Fired when the ViewModel requests a rename operation in the View.
    /// </summary>
    internal event EventHandler<RenameRequestedEventArgs?>? RenameRequested;

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
    internal ProjectAdapter? Project
    {
        get => this.projectAdapter;
        private set
        {
            if (this.SetProperty(ref this.projectAdapter, value))
            {
                this.AddSceneCommand.NotifyCanExecuteChanged();
            }
        }
    }

    private HistoryKeeper History => UndoRedo.Default[this];

    private bool HasUnlockedSelectedItems { get; set; }

    /// <summary>
    /// Dispose the view-model and unsubscribe from events.
    /// </summary>
    public void Dispose()
    {
        this.Dispose(disposing: true);
        GC.SuppressFinalize(this);
    }

    /// <inheritdoc/>
    [RelayCommand(CanExecute = nameof(HasUnlockedSelectedItems))]
    public override async Task RemoveSelectedItems()
    {
        var eligibleCount = this.GetSelectedItems().Count(item => !item.IsLocked);
        if (eligibleCount <= 1)
        {
            await base.RemoveSelectedItems().ConfigureAwait(false);
            return;
        }

        this.History.BeginChangeSet(string.Create(CultureInfo.InvariantCulture, $"Remove {eligibleCount} item(s)"));
        try
        {
            await base.RemoveSelectedItems().ConfigureAwait(false);
        }
        finally
        {
            this.History.EndChangeSet();
        }
    }

    /// <summary>
    /// Renames the specified <paramref name="item"/> to the given <paramref name="newName"/>.
    /// </summary>
    /// <param name="item">The tree item to rename.</param>
    /// <param name="newName">The new name to assign to the item.</param>
    public void RenameItem(ITreeItem item, string newName)
    {
        ArgumentNullException.ThrowIfNull(item);

        var trimmed = (newName ?? string.Empty).Trim();
        if (!item.ValidateItemName(trimmed))
        {
            return;
        }

        var oldName = item.Label;
        if (string.Equals(oldName, trimmed, StringComparison.Ordinal))
        {
            return;
        }

        item.Label = trimmed;
        if (!this.domainModelService.TryRename(item, trimmed, out var renameErr))
        {
            this.OperationError = renameErr;
        }

        this.History.AddChange(
            $"Rename({oldName} → {trimmed})",
            () => this.RenameItem(item, oldName));
    }

    /// <summary>
    /// Dispose managed resources. Called by <see cref="Dispose()"/>.
    /// </summary>
    /// <param name="disposing">Indicator whether disposing is in progress.</param>
    protected virtual void Dispose(bool disposing)
    {
        if (this.isDisposed || !disposing)
        {
            return;
        }

        if (this.UndoStack is INotifyCollectionChanged undo)
        {
            undo.CollectionChanged -= this.OnUndoStackCollectionChanged;
        }

        if (this.RedoStack is INotifyCollectionChanged redo)
        {
            redo.CollectionChanged -= this.OnRedoStackCollectionChanged;
        }

        this.ItemBeingAdded -= this.OnItemBeingAdded;
        this.ItemBeingMoved -= this.OnItemBeingMoved;
        this.ItemAdded -= this.OnItemAdded;
        this.ItemBeingRemoved -= this.OnItemBeingRemoved;
        this.ItemRemoved -= this.OnItemRemoved;
        this.ItemMoved -= this.OnItemMoved;

        this.ClipboardContentChanged -= this.OnClipboardContentChanged;
        this.SelectionModel?.PropertyChanged -= this.SelectionModel_OnPropertyChanged;
        this.PropertyChanged -= this.ViewModel_PropertyChanged;

        this.isDisposed = true;
    }

    /// <inheritdoc/>
    protected override void OnSelectionModelChanged(SelectionModel<ITreeItem>? oldValue)
    {
        base.OnSelectionModelChanged(oldValue);
        oldValue?.PropertyChanged -= this.SelectionModel_OnPropertyChanged;
        this.SelectionModel?.PropertyChanged += this.SelectionModel_OnPropertyChanged;

        // Re-evaluate any commands that depend on selection to ensure UI updates correctly.
        this.AddEntityCommand.NotifyCanExecuteChanged();
        this.RemoveSelectedItemsCommand.NotifyCanExecuteChanged();
        this.CopyCommand.NotifyCanExecuteChanged();
        this.CutCommand.NotifyCanExecuteChanged();
        this.RenameCommand.NotifyCanExecuteChanged();
        this.PasteCommand.NotifyCanExecuteChanged();
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

    private bool RemoveFromModel(ITreeItem item, ITreeItem parent)
    {
        var ok = this.domainModelService.TryRemove(item, parent, out var err);
        if (!ok)
        {
            this.OperationError = err;
        }

        return ok;
    }

    private bool InsertIntoModel(ITreeItem item, ITreeItem parent, int index)
    {
        var ok = this.domainModelService.TryInsert(item, parent, index, out var err);
        if (!ok)
        {
            this.OperationError = err;
        }

        return ok;
    }

    partial void OnOperationErrorChanged(string? value)
        => this.OperationErrorVisible = !string.IsNullOrEmpty(value);

    [RelayCommand]
    private void ClearOperationError() => this.OperationError = null;

    /// <summary>
    /// Undoes the last change.
    /// </summary>
    private bool CanUndo() => this.History.CanUndo;

    [RelayCommand(CanExecute = nameof(CanUndo))]
    private async Task Undo()
        => await this.History.UndoAsync().ConfigureAwait(false);

    /// <summary>
    /// Redoes the last undone change.
    /// </summary>
    private bool CanRedo() => this.History.CanRedo;

    [RelayCommand(CanExecute = nameof(CanRedo))]
    private async Task Redo()
        => await this.History.RedoAsync().ConfigureAwait(false);

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

        await this.CopyItemsAsync(items).ConfigureAwait(false);
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

        await this.CutItemsAsync(items).ConfigureAwait(false);
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
        var targetParent = this.FocusedItem?.Item;
        if (targetParent is null && this.SelectionModel?.IsEmpty == false)
        {
            var selected = this.GetSelectedItems();
            targetParent = selected.FirstOrDefault();
        }

        if (this.CurrentClipboardState == ClipboardState.Copied && this.ClipboardItems.Count > 1)
        {
            this.History.BeginChangeSet($"Paste {this.ClipboardItems.Count} item(s)");
            try
            {
                await this.PasteItemsAsync(targetParent).ConfigureAwait(false);
            }
            finally
            {
                this.History.EndChangeSet();
            }

            return;
        }

        await this.PasteItemsAsync(targetParent).ConfigureAwait(false);
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

    /// <summary>
    /// Loads the project asynchronously.
    /// </summary>
    [RelayCommand]
    private async Task LoadProjectAsync()
    {
        this.History.Clear();

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
    private bool CanAddScene() => this.Project != null;

    [RelayCommand(CanExecute = nameof(CanAddScene))]
    private async Task AddScene()
    {
        if (this.Project is null)
        {
            return;
        }

        var name = GetNextAvailableSceneName(this.Project.AttachedObject, "New Scene");
        var newScene = new SceneAdapter(new Scene(name));
        await this.ApplyInsertAsync(newScene, this.Project, 0).ConfigureAwait(false);
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
        await this.ApplyInsertAsync(newEntity, parent, 0).ConfigureAwait(false);
    }

    private void OnItemAdded(object? sender, TreeItemAddedEventArgs args)
    {
        _ = sender; // unused
        this.OperationError = null;

        // Update the underlying model now that the tree insertion succeeded.
        if (!this.domainModelService.TryInsert(args.TreeItem, args.Parent, args.RelativeIndex, out var insertErr))
        {
            this.OperationError = insertErr;
        }

        this.History.AddChange(
            $"RemoveItemAsync({args.TreeItem.Label})",
            async () => await this.RemoveItemAsync(args.TreeItem).ConfigureAwait(false));

        this.LogItemAdded(args.TreeItem.Label);
    }

    private void OnClipboardContentChanged(object? sender, ClipboardContentChangedEventArgs args)
        => this.PasteCommand.NotifyCanExecuteChanged();

    private void OnItemRemoved(object? sender, TreeItemRemovedEventArgs args)
    {
        _ = sender; // unused
        this.OperationError = null;

        // Update underlying domain model to reflect the removal from the tree.
        if (!this.domainModelService.TryRemove(args.TreeItem, args.Parent, out var removeErr))
        {
            this.OperationError = removeErr;
        }

        this.History.AddChange(
            $"InsertItemAsync({args.TreeItem.Label})",
            async () => await this.ApplyInsertAsync(args.TreeItem, args.Parent, args.RelativeIndex).ConfigureAwait(false));
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
            return [.. multi2.SelectedIndices.Select(i => this.GetShownItemAt(i)).Cast<ITreeItem>()];
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
                    var item = this.GetShownItemAt(selectedIndex);
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

    private void ViewModel_PropertyChanged(object? sender, PropertyChangedEventArgs args)
    {
        if (args.PropertyName?.Equals(nameof(this.FocusedItem), StringComparison.Ordinal) == true)
        {
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

    private void OnItemMoved(object? sender, TreeItemsMovedEventArgs args)
    {
        _ = sender; // unused
        this.OperationError = null;

        if (args.Moves.Count == 0)
        {
            return;
        }

        if (args.IsBatch)
        {
            this.History.BeginChangeSet($"Move {args.Moves.Count} item(s)");
        }

        try
        {
            if (!this.domainModelService.TryUpdateMoved(args, out var moveErr))
            {
                this.OperationError = moveErr;
            }

            RecordUndoChanges(args);
        }
        finally
        {
            if (args.IsBatch)
            {
                this.History.EndChangeSet();
            }
        }

        void RecordUndoChanges(TreeItemsMovedEventArgs args)
        {
            // Important: when restoring multiple items into the same previous parent, inserting an item
            // at a lower index shifts the target indices of items that should end up after it.
            // TimeMachine ChangeSet applies changes in reverse-add order, so we add in ascending
            // PreviousIndex to ensure apply happens in descending PreviousIndex.
            foreach (var group in args.Moves.GroupBy(m => m.PreviousParent))
            {
                foreach (var move in group.OrderBy(m => m.PreviousIndex))
                {
                    var item = move.Item;
                    this.History.AddChange(
                        $"MoveItemAsync({item.Label})",
                        async () => await this.ApplyMoveAsync(item, move.PreviousParent, move.PreviousIndex).ConfigureAwait(false));
                }
            }
        }
    }

    /// <summary>
    /// Applies a visual-only insert of an item into the view at the given index.
    /// </summary>
    /// <param name="item">The item to insert.</param>
    /// <param name="parent">The parent under which the item will be inserted.</param>
    /// <param name="index">The child index within the parent.</param>
    /// <returns>A <see cref="Task"/> that completes when the visual change finishes.</returns>
    private async Task ApplyInsertAsync(ITreeItem item, ITreeItem parent, int index)
    {
        await this.EnsureAncestorsExpandedAsync(parent).ConfigureAwait(false);
        await this.InsertItemAsync(index, parent, item).ConfigureAwait(false);
    }

    /// <summary>
    /// Applies a visual-only move of an existing item to a new parent/index.
    /// </summary>
    /// <param name="item">The item to move.</param>
    /// <param name="newParent">The new parent that will receive the item.</param>
    /// <param name="newIndex">The insertion index within the new parent.</param>
    /// <returns>A <see cref="Task"/> that completes when the visual change finishes.</returns>
    private async Task ApplyMoveAsync(ITreeItem item, ITreeItem newParent, int newIndex)
    {
        // Ensure moved item and the target parent are visible.
        await this.EnsureAncestorsExpandedAsync(item).ConfigureAwait(false);
        await this.EnsureAncestorsExpandedAsync(newParent).ConfigureAwait(false);

        var effectiveIndex = newIndex;
        if (ReferenceEquals(item.Parent, newParent))
        {
            var children = await newParent.Children.ConfigureAwait(false);
            var currentIndex = children.IndexOf(item);
            if (currentIndex >= 0 && currentIndex < newIndex)
            {
                effectiveIndex = newIndex + 1;
            }
        }

        await this.MoveItemAsync(item, newParent, effectiveIndex).ConfigureAwait(false);
    }

    private async Task EnsureAncestorsExpandedAsync(ITreeItem item)
    {
        var ancestors = new Stack<ITreeItem>();
        var current = item.Parent;
        while (current is not null)
        {
            ancestors.Push(current);
            if (current.IsRoot)
            {
                break;
            }

            current = current.Parent;
        }

        while (ancestors.TryPop(out var toExpand))
        {
            await this.ExpandItemAsync(toExpand).ConfigureAwait(false);
        }
    }

    private void OnUndoStackCollectionChanged(object? sender, NotifyCollectionChangedEventArgs e)
    {
        _ = sender;
        _ = e;
        this.UndoCommand.NotifyCanExecuteChanged();
        this.RedoCommand.NotifyCanExecuteChanged();
    }

    private void OnRedoStackCollectionChanged(object? sender, NotifyCollectionChangedEventArgs e)
        => this.OnUndoStackCollectionChanged(sender, e);
}
