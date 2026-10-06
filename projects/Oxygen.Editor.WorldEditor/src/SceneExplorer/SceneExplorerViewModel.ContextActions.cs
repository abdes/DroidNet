// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.Windows.Input;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Controls;
using DroidNet.Controls.Menus;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Oxygen.Editor.WorldEditor.Documents.Selection;

namespace Oxygen.Editor.World.SceneExplorer;

/// <summary>Captured command surfaces for the Explorer.</summary>
public partial class SceneExplorerViewModel
{
    private readonly Dictionary<SceneExplorerCommandKind, AsyncRelayCommand> contextToolbarCommands = [];
    private readonly Dictionary<MenuItemData, SceneExplorerCommandKind> openMenuActions = [];
    private CapturedExplorerContext? openMenuContext;
    private IMenuSource? openMenuSource;
    private SceneExplorerClipboard? explorerClipboard;
    private IReadOnlyList<SceneNodeData> clipboardLocalSnapshots = [];
    private bool clipboardWorldPoseAvailable = true;
    private int clipboardGeneration;

    /// <summary>Raised when the open menu's immutable targets are no longer current.</summary>
    public event EventHandler? ContextMenuInvalidated;

    /// <summary>Gets the shared node-creation action.</summary>
    public ICommand NewNodeAction => this.GetActionCommand(SceneExplorerCommandKind.NewNode);

    /// <summary>Gets the shared folder-creation action.</summary>
    public ICommand NewFolderAction => this.GetActionCommand(SceneExplorerCommandKind.NewFolder);

    /// <summary>Gets the shared rename action.</summary>
    public ICommand RenameAction => this.GetActionCommand(SceneExplorerCommandKind.Rename);

    /// <summary>Gets the shared cut action.</summary>
    public ICommand CutAction => this.GetActionCommand(SceneExplorerCommandKind.Cut);

    /// <summary>Gets the shared copy action.</summary>
    public ICommand CopyAction => this.GetActionCommand(SceneExplorerCommandKind.Copy);

    /// <summary>Gets the shared paste action.</summary>
    public ICommand PasteAction => this.GetActionCommand(SceneExplorerCommandKind.Paste);

    /// <summary>Gets the shared delete action.</summary>
    public ICommand DeleteAction => this.GetActionCommand(SceneExplorerCommandKind.Delete);

    /// <summary>Gets the shared, live-selection toolbar command for an action.</summary>
    /// <param name="kind">The action identity.</param>
    /// <returns>A command that captures its targets before execution.</returns>
    public ICommand GetActionCommand(SceneExplorerCommandKind kind)
    {
        if (!this.contextToolbarCommands.TryGetValue(kind, out var command))
        {
            command = new AsyncRelayCommand(
                () => this.ExecuteContextActionAsync(kind, this.CaptureExplorerContext(anchor: null, background: false)),
                () => this.GetContextDisabledReason(kind, this.CaptureExplorerContext(anchor: null, background: false)) is null);
            this.contextToolbarCommands.Add(kind, command);
        }

        return command;
    }

    /// <summary>Settles Inspector edits and applies the exclusive-selection policy for an unselected row.</summary>
    /// <param name="anchor">The requesting row, or null for tree background.</param>
    /// <returns>Whether the invocation still belongs to the loaded document.</returns>
    public async Task<bool> PrepareContextMenuAsync(ITreeItem? anchor)
    {
        if (this.CreateCommandContext() is not { } document || this.isDisposed)
        {
            return false;
        }

        if (anchor is null || this.GetSelectedItems().Contains(anchor))
        {
            return true;
        }

        await this.commandService.CompleteEditSessionsAsync(document, commit: true).ConfigureAwait(true);
        if (!ReferenceEquals(this.CreateCommandContext()?.Scene, document.Scene)
            || !ReferenceEquals(this.CreateCommandContext()?.Metadata, document.Metadata)
            || !this.IsShown(anchor))
        {
            return false;
        }

        this.SelectDisplayedItem(anchor, this.ShownItems.ToArray(), isControlDown: false, isShiftDown: false);
        return true;
    }

    /// <summary>Builds a frozen row or background menu; execution never substitutes a later selection.</summary>
    /// <param name="anchor">The requesting row, or null for a scene-root background menu.</param>
    /// <returns>The captured menu source, empty when no scene document is loaded.</returns>
    public IMenuSource BuildContextMenuSource(ITreeItem? anchor)
    {
        var context = this.CaptureExplorerContext(anchor, background: anchor is null);
        var builder = new MenuBuilder();
        this.openMenuContext = context;
        this.openMenuActions.Clear();
        if (context is not null)
        {
            string? lastGroup = null;
            foreach (var (kind, label, group) in this.GetContextActions(context))
            {
                if (lastGroup is not null && !string.Equals(lastGroup, group, StringComparison.Ordinal))
                {
                    _ = builder.AddSeparator();
                }

                lastGroup = group;
                var reason = this.GetContextDisabledReason(kind, context);
                var item = new MenuItemData
                {
                    Text = label,
                    HelpText = reason,
                    IsEnabled = reason is null,
                    Icon = GetContextActionIcon(kind),
                    Command = new AsyncRelayCommand(
                        () => this.ExecuteContextActionAsync(kind, context),
                        () => this.GetContextDisabledReason(kind, context) is null),
                };
                this.openMenuActions.Add(item, kind);
                _ = builder.AddMenuItem(item);
            }
        }

        this.openMenuSource = builder.Build();
        return this.openMenuSource;
    }

    /// <summary>Revalidates captured targets and refreshes eligibility without rearranging menu entries.</summary>
    public void RefreshContextActions()
    {
        this.RemoveSelectedItemsCommand.NotifyCanExecuteChanged();
        this.RenameSelectedCommand.NotifyCanExecuteChanged();
        foreach (var command in this.contextToolbarCommands.Values)
        {
            command.NotifyCanExecuteChanged();
        }

        if (this.openMenuContext is not { } context)
        {
            return;
        }

        if (!this.IsContextCurrent(context))
        {
            this.openMenuContext = null;
            this.openMenuSource = null;
            this.ContextMenuInvalidated?.Invoke(this, EventArgs.Empty);
            return;
        }

        if (this.openMenuSource is { } source)
        {
            foreach (var item in source.Items.Where(static item => !item.IsSeparator))
            {
                var reason = this.GetContextDisabledReason(this.openMenuActions[item], context);
                item.IsEnabled = reason is null;
                item.HelpText = reason;
                (item.Command as AsyncRelayCommand)?.NotifyCanExecuteChanged();
            }
        }
    }

    private static IconSource? GetContextActionIcon(SceneExplorerCommandKind kind) => kind switch
    {
        SceneExplorerCommandKind.NewNode => new SymbolIconSource { Symbol = Symbol.Add },
        SceneExplorerCommandKind.NewFolder => new SymbolIconSource { Symbol = Symbol.Folder },
        SceneExplorerCommandKind.Rename => new SymbolIconSource { Symbol = Symbol.Rename },
        SceneExplorerCommandKind.Cut => new SymbolIconSource { Symbol = Symbol.Cut },
        SceneExplorerCommandKind.Copy => new SymbolIconSource { Symbol = Symbol.Copy },
        SceneExplorerCommandKind.Paste or SceneExplorerCommandKind.PasteAsChild => new SymbolIconSource { Symbol = Symbol.Paste },
        SceneExplorerCommandKind.Delete => new SymbolIconSource { Symbol = Symbol.Delete },
        _ => null,
    };

    private bool CanRemoveContextItems()
        => this.GetContextDisabledReason(SceneExplorerCommandKind.Delete,
            this.CaptureExplorerContext(anchor: null, background: false)) is null;

    private Task StageExplorerClipboardAsync(IReadOnlyList<ITreeItem> items, bool cut)
    {
        if (this.CreateCommandContext() is not { } context)
        {
            return Task.CompletedTask;
        }

        var captured = this.commandService.CaptureExplorerClipboard(context,
            items.OfType<SceneNodeAdapter>().Select(static row => row.AttachedObject.Id).ToArray(),
            items.OfType<FolderAdapter>().Select(static row => row.Id).ToArray());
        if (!captured.Succeeded || captured.Value is not { } payload)
        {
            return Task.CompletedTask;
        }

        this.explorerClipboard = payload;
        this.clipboardNodeIds.Clear();
        this.clipboardNodeIds.AddRange(payload.Nodes.Select(static data => data.Id));
        this.clipboardSnapshots.Clear();
        this.clipboardLocalSnapshots = [];
        this.clipboardIsCut = cut;
        this.StampClipboardOrigin();
        this.ClearCutMarks();
        this.ClipboardItemStore = [.. items];
        this.ClipboardStateStore = cut ? ClipboardState.Cut : ClipboardState.Copied;
        if (cut)
        {
            this.CutMarkedStore = [.. items];
            foreach (var row in items)
            {
                row.IsCut = true;
            }
        }

        this.RaiseClipboardChanged();
        return Task.CompletedTask;
    }

    private CapturedExplorerContext? CaptureExplorerContext(ITreeItem? anchor, bool background)
    {
        if (this.isDisposed || this.CreateCommandContext() is not { } document
            || document.Metadata.IsSceneLoadPending || SceneAuthoringGate.IsRetired(document.Scene))
        {
            return null;
        }

        var selection = this.selectionService.GetContext(document.DocumentId);
        var selected = this.GetSelectedItems().ToArray();

        // Authoritative identities can include collapsed/unrealized rows. Never silently shrink that batch.
        foreach (var id in selection.SelectedNodeIds)
        {
            if (this.projection.GetNode(id) is { } row && !selected.Contains(row))
            {
                selected = [.. selected, row];
            }
        }

        foreach (var id in selection.SelectedFolderIds)
        {
            if (this.projection.GetFolder(id) is { } row && !selected.Contains(row))
            {
                selected = [.. selected, row];
            }
        }

        anchor = background ? this.Scene : anchor ?? this.ActiveItem ?? selected.FirstOrDefault() ?? this.Scene;
        var targets = background || anchor is SceneAdapter ? Array.Empty<ITreeItem>() : selected;
        if (!background && anchor is not SceneAdapter && anchor is not null && !targets.Contains(anchor))
        {
            targets = [anchor];
        }

        var destination = background || anchor is SceneAdapter || targets.Length == 0
            ? this.Scene
            : targets.Length == 1 ? targets[0] : this.ResolveSharedPasteTarget(targets);
        return new(document, this.projectManager.CurrentProject, selection, Array.AsReadOnly(selected),
            Array.AsReadOnly(targets), anchor, destination, background,
            selected.Select(static row => (Row: row, Parent: row.Parent,
                SceneParent: (row as SceneNodeAdapter)?.AttachedObject.Parent)).ToArray(), this.projectContexts?.ActiveProject);
    }

    private ITreeItem? ResolveSharedPasteTarget(IReadOnlyList<ITreeItem> targets)
    {
        var first = targets[0];
        if (targets.All(item => ReferenceEquals(item.Parent, first.Parent)))
        {
            return targets.LastOrDefault(static row => row is SceneNodeAdapter) ?? first.Parent;
        }

        return null;
    }

    private bool IsContextCurrent(CapturedExplorerContext context)
    {
        var current = this.CreateCommandContext();
        return !this.isDisposed
            && ReferenceEquals(this.projectManager.CurrentProject, context.Project)
            && ReferenceEquals(this.projectContexts?.ActiveProject, context.ProjectActivation)
            && ReferenceEquals(current?.Scene, context.Document.Scene)
            && ReferenceEquals(current?.Metadata, context.Document.Metadata)
            && !context.Document.Metadata.IsSceneLoadPending
            && !SceneAuthoringGate.IsRetired(context.Document.Scene)
            && ReferenceEquals(this.sceneEngineSync.GetDocumentScene(context.Document.Metadata), context.Document.Scene)
            && SameSelection(this.selectionService.GetContext(context.Document.DocumentId), context.Selection)
            && context.Selection.SelectedNodeIds.All(id => this.projection.GetNode(id) is not null)
            && context.Selection.SelectedFolderIds.All(id => this.projection.GetFolder(id) is not null)
            && context.SelectedRows.All(this.IsCurrentContextRow)
            && context.Parentage.All(static captured => ReferenceEquals(captured.Row.Parent, captured.Parent)
                && ReferenceEquals((captured.Row as SceneNodeAdapter)?.AttachedObject.Parent, captured.SceneParent))
            && (context.Anchor is null || this.IsCurrentContextRow(context.Anchor));
    }

    private static bool SameSelection(SceneSelectionContext left, SceneSelectionContext right)
        => left.Kind == right.Kind && left.PrimaryNodeId == right.PrimaryNodeId && left.PrimaryFolderId == right.PrimaryFolderId
            && left.SelectedNodeIds.SequenceEqual(right.SelectedNodeIds)
            && left.SelectedFolderIds.SequenceEqual(right.SelectedFolderIds);

    private bool IsCurrentContextRow(ITreeItem row) => row switch
    {
        SceneNodeAdapter node => ReferenceEquals(this.projection.GetNode(node.AttachedObject.Id), row),
        FolderAdapter folder => ReferenceEquals(this.projection.GetFolder(folder.Id), row),
        SceneAdapter scene => ReferenceEquals(this.Scene, scene),
        _ => false,
    };

    private IEnumerable<(SceneExplorerCommandKind Kind, string Label, string Group)> GetContextActions(CapturedExplorerContext context)
    {
        var targets = context.Targets;
        var root = targets.Count == 0;
        var single = targets.Count == 1;
        var node = single ? targets[0] as SceneNodeAdapter : null;
        var folder = single ? targets[0] as FolderAdapter : null;
        if (root || single)
        {
            yield return (SceneExplorerCommandKind.NewNode, node is not null ? "New child node" : folder is not null ? "New node in folder" : "New node", "create");
            yield return (SceneExplorerCommandKind.NewFolder, folder is not null ? "New subfolder" : node is not null ? "New folder beside node" : "New folder", "create");
        }

        if (context.Anchor is SceneAdapter && !context.Background)
        {
            yield return (SceneExplorerCommandKind.Rename, "Rename scene", "edit");
        }

        if (!root)
        {
            if (single)
            {
                yield return (SceneExplorerCommandKind.Rename, "Rename", "edit");
            }

            yield return (SceneExplorerCommandKind.Cut, "Cut", "edit");
            yield return (SceneExplorerCommandKind.Copy, "Copy", "edit");
        }

        yield return (SceneExplorerCommandKind.Paste, root ? "Paste at scene root" : "Paste", "edit");
        if (node is not null)
        {
            yield return (SceneExplorerCommandKind.PasteAsChild, "Paste as child (keep world pose)", "edit");
        }

        if (!root)
        {
            yield return (SceneExplorerCommandKind.Delete, folder is not null ? "Remove folder (keep objects)" : single ? "Delete node" : "Delete selected items", "edit");
        }

        if (!context.Background && context.Anchor is LayoutItemAdapter { HasChildren: true } anchor && (root || single))
        {
            yield return (anchor.IsExpanded ? SceneExplorerCommandKind.Collapse : SceneExplorerCommandKind.Expand,
                anchor.IsExpanded ? "Collapse" : "Expand", "hierarchy");
        }

        if (node?.Parent is FolderAdapter)
        {
            yield return (SceneExplorerCommandKind.RemoveFromFolder, "Remove from folder", "hierarchy");
        }

        if (node is not null && (node.AttachedObject.Parent is not null || node.Parent is FolderAdapter))
        {
            yield return (SceneExplorerCommandKind.MoveToSceneRoot, "Move to scene root", "hierarchy");
        }

        if (targets.Count > 0 && targets.All(static row => row is SceneNodeAdapter))
        {
            var hidden = single && (node!.IsEffectivelyHiddenInEditor || this.interaction?.IsHidden(node.AttachedObject.Id) == true);
            var locked = single && this.FindLockedContextRow(node!) is not null;
            if (!single || !hidden)
            {
                yield return (SceneExplorerCommandKind.Hide, single ? "Hide in editor" : "Hide selected", "state");
            }

            if (!single || hidden)
            {
                yield return (SceneExplorerCommandKind.Show, single ? "Show in editor" : "Show selected", "state");
            }

            if (!single || !locked)
            {
                yield return (SceneExplorerCommandKind.Lock, single ? "Lock" : "Lock selected", "state");
            }

            if (!single || locked)
            {
                yield return (SceneExplorerCommandKind.Unlock, single ? "Unlock" : "Unlock selected", "state");
            }
        }

        if (root && this.projection.Nodes.Any(row => this.interaction?.IsHidden(row.AttachedObject.Id) == true))
        {
            yield return (SceneExplorerCommandKind.ShowAll, "Show all in editor", "state");
        }
    }

    private string? GetContextDisabledReason(SceneExplorerCommandKind kind, CapturedExplorerContext? context)
    {
        if (context is null || !this.IsContextCurrent(context))
        {
            return "The scene document or captured selection is no longer available.";
        }

        var targets = context.Targets;
        var nodes = targets.OfType<SceneNodeAdapter>().ToArray();
        var single = targets.Count == 1;
        var creation = kind is SceneExplorerCommandKind.NewNode or SceneExplorerCommandKind.NewFolder;
        var workspace = kind is SceneExplorerCommandKind.Hide or SceneExplorerCommandKind.Show or SceneExplorerCommandKind.Lock or SceneExplorerCommandKind.Unlock;
        if (kind == SceneExplorerCommandKind.Rename && context.Anchor is SceneAdapter && !context.Background)
        {
            return null;
        }

        if (creation && targets.Count > 1)
        {
            return "Select one creation destination.";
        }

        if (kind is SceneExplorerCommandKind.Rename or SceneExplorerCommandKind.RemoveFromFolder or SceneExplorerCommandKind.MoveToSceneRoot && !single)
        {
            return "Select one row.";
        }

        if (kind is SceneExplorerCommandKind.Rename or SceneExplorerCommandKind.Cut or SceneExplorerCommandKind.Copy or SceneExplorerCommandKind.Delete && targets.Count == 0)
        {
            return "The scene root cannot be edited by this action.";
        }

        if (workspace && (this.interaction is null || nodes.Length != targets.Count || nodes.Length == 0))
        {
            return "Editor state requires a node-only selection and the workspace service.";
        }

        if (targets.Any(static row => row is SceneAdapter)
            && kind is SceneExplorerCommandKind.Rename or SceneExplorerCommandKind.Delete or SceneExplorerCommandKind.Cut or SceneExplorerCommandKind.Copy)
        {
            return "The scene root is not a selection edit target.";
        }

        var manipulates = creation || kind is SceneExplorerCommandKind.Rename or SceneExplorerCommandKind.Cut
            or SceneExplorerCommandKind.Delete or SceneExplorerCommandKind.RemoveFromFolder
            or SceneExplorerCommandKind.MoveToSceneRoot;
        if (manipulates)
        {
            foreach (var row in targets)
            {
                if (this.FindLockedContextRow(row) is { } locked)
                {
                    return $"Locked by {locked.Label}.";
                }
            }
        }

        if (kind is SceneExplorerCommandKind.Cut or SceneExplorerCommandKind.Delete or SceneExplorerCommandKind.MoveToSceneRoot)
        {
            var ids = nodes.Select(static row => row.AttachedObject.Id).ToHashSet();
            foreach (var descendant in this.projection.Nodes)
            {
                for (var parent = descendant.AttachedObject.Parent; parent is not null; parent = parent.Parent)
                {
                    if (ids.Contains(parent.Id) && this.FindLockedContextRow(descendant) is { } locked)
                    {
                        return $"The hierarchy contains a node locked by {locked.Label}.";
                    }
                }
            }
        }

        if (kind == SceneExplorerCommandKind.Cut)
        {
            foreach (var descendant in this.projection.Nodes)
            {
                for (var parent = descendant.Parent; parent is not null; parent = parent.Parent)
                {
                    if (targets.Contains(parent) && this.FindLockedContextRow(descendant) is { } locked)
                    {
                        return $"The selection contains a node locked by {locked.Label}.";
                    }
                }
            }
        }

        if (kind is SceneExplorerCommandKind.Show or SceneExplorerCommandKind.Unlock)
        {
            foreach (var row in nodes)
            {
                for (var parent = row.AttachedObject.Parent; parent is not null; parent = parent.Parent)
                {
                    if (kind == SceneExplorerCommandKind.Show ? this.interaction!.IsHidden(parent.Id) : this.interaction!.IsLocked(parent.Id))
                    {
                        return $"{(kind == SceneExplorerCommandKind.Show ? "Hidden" : "Locked")} by {parent.Name}.";
                    }
                }
            }
        }

        if (kind is SceneExplorerCommandKind.Paste or SceneExplorerCommandKind.PasteAsChild)
        {
            var destination = kind == SceneExplorerCommandKind.PasteAsChild
                ? single && targets[0] is SceneNodeAdapter ? targets[0] : null
                : context.Destination;
            if (destination is null)
            {
                return "The selection has no unambiguous paste destination.";
            }

            if ((this.clipboardNodeIds.Count == 0 && this.explorerClipboard is null) || !this.ClipboardLifetimeIsCurrent())
            {
                return "The clipboard has no payload for this scene.";
            }

            if (destination is not SceneAdapter && this.FindLockedContextRow(destination) is { } locked)
            {
                return $"Locked by {locked.Label}.";
            }

            if (this.explorerClipboard is { } payload)
            {
                var (parentId, folderId, _) = this.ResolveCapturedPasteDestination(context, kind == SceneExplorerCommandKind.PasteAsChild);
                return this.commandService.ValidateExplorerPaste(context.Document, payload, this.clipboardIsCut,
                    parentId, folderId, preserveWorld: kind == SceneExplorerCommandKind.PasteAsChild);
            }

            if (kind == SceneExplorerCommandKind.PasteAsChild && !this.clipboardWorldPoseAvailable)
            {
                return "The copied world pose cannot be represented without shear.";
            }

            if (!this.clipboardIsCut && kind == SceneExplorerCommandKind.PasteAsChild)
            {
                var parent = (destination as SceneNodeAdapter)?.AttachedObject;
                foreach (var snapshot in this.clipboardSnapshots)
                {
                    var copied = SceneNode.CreateAndHydrate(context.Document.Scene, snapshot);
                    if (!SceneTransformMath.TryPreserveWorldLocal(copied, parent, out _, out _, out _))
                    {
                        return "The destination cannot represent every copied world pose.";
                    }
                }
            }

            if (this.clipboardIsCut && this.clipboardNodeIds.Any(id => this.projection.GetNode(id) is not { } source || this.FindLockedContextRow(source) is not null))
            {
                return "A staged source was deleted or locked.";
            }

            if (this.clipboardIsCut)
            {
                foreach (var sourceId in this.clipboardNodeIds)
                {
                    foreach (var child in this.projection.GetNode(sourceId)!.AttachedObject.Descendants())
                    {
                        if (this.projection.GetNode(child.Id) is { } row && this.FindLockedContextRow(row) is { } lockedChild)
                        {
                            return $"The staged hierarchy contains a node locked by {lockedChild.Label}.";
                        }
                    }
                }
            }

            if (this.clipboardIsCut)
            {
                var resolvedParent = kind == SceneExplorerCommandKind.PasteAsChild
                    ? (destination as SceneNodeAdapter)?.AttachedObject
                    : (destination as SceneNodeAdapter)?.AttachedObject.Parent;
                if (destination is FolderAdapter destinationFolder)
                {
                    for (var owner = destinationFolder.Parent; owner is not null; owner = owner.Parent)
                    {
                        if (owner is SceneNodeAdapter scope)
                        {
                            resolvedParent = scope.AttachedObject;
                            break;
                        }
                    }
                }

                for (var current = resolvedParent; current is not null; current = current.Parent)
                {
                    if (this.clipboardNodeIds.Contains(current.Id))
                    {
                        return "A cut hierarchy cannot be pasted into itself or its descendants.";
                    }
                }

                foreach (var id in kind == SceneExplorerCommandKind.PasteAsChild ? this.clipboardNodeIds : [])
                {
                    if (!SceneTransformMath.TryPreserveWorldLocal(this.projection.GetNode(id)!.AttachedObject, resolvedParent, out _, out _, out _))
                    {
                        return "The staged world poses cannot be represented at this destination.";
                    }
                }
            }
        }

        return null;
    }

    private ITreeItem? FindLockedContextRow(ITreeItem row)
    {
        if (row is SceneNodeAdapter node)
        {
            if (this.interaction?.GetLockOwner(node.AttachedObject) is { } lockOwner)
            {
                return this.projection.GetNode(lockOwner.Id) ?? row;
            }
        }

        if (row is FolderAdapter)
        {
            for (var parent = row.Parent; parent is not null; parent = parent.Parent)
            {
                if (parent is SceneNodeAdapter)
                {
                    return this.FindLockedContextRow(parent);
                }
            }
        }

        return row is not SceneAdapter && row.IsLocked ? row : null;
    }

    private async Task ExecuteContextActionAsync(SceneExplorerCommandKind kind, CapturedExplorerContext? captured)
    {
        if (captured is not { } context || this.GetContextDisabledReason(kind, context) is not null)
        {
            return;
        }

        using var authoring = SceneAuthoringGate.TryEnter(context.Document.Scene);
        if (authoring is null)
        {
            return;
        }

        var previousSuppression = this.suppressNodeMessages;
        this.suppressNodeMessages = true;
        try
        {
            await this.ExecuteContextActionCoreAsync(kind, context).ConfigureAwait(true);
        }
        finally
        {
            this.suppressNodeMessages = previousSuppression;
        }
    }

    private async Task ExecuteContextActionCoreAsync(SceneExplorerCommandKind kind, CapturedExplorerContext context)
    {
        var nodes = context.Targets.OfType<SceneNodeAdapter>().Select(static row => row.AttachedObject.Id).ToArray();
        var folders = context.Targets.OfType<FolderAdapter>().Select(static row => row.Id).ToArray();
        var target = context.Targets.FirstOrDefault();
        var selectionGeneration = this.selectionApplyGeneration;
        var anchorIndex = context.Anchor is { } anchor ? this.ShownIndexOf(anchor) : -1;
        var neighbors = kind == SceneExplorerCommandKind.Delete
            ? this.ShownItems.Skip(anchorIndex + 1).Concat(this.ShownItems.Take(Math.Max(0, anchorIndex)).Reverse())
                .Where(row => !context.Targets.Contains(row)).ToArray()
            : [];
        SceneCommandResult? result = null;
        switch (kind)
        {
            case SceneExplorerCommandKind.NewNode:
                var createdNode = await this.commandService.CreateNodeAsync(context.Document,
                    (target as SceneNodeAdapter)?.AttachedObject.Id, (target as FolderAdapter)?.Id, this.GetNextEntityName()).ConfigureAwait(true);
                result = new SceneCommandResult(createdNode.Succeeded, createdNode.OperationResultId);
                break;
            case SceneExplorerCommandKind.NewFolder:
                var container = target is SceneNodeAdapter ? target.Parent : target;
                var createdFolder = await this.commandService.CreateFolderAsync(context.Document,
                    (container as FolderAdapter)?.Id, (container as SceneNodeAdapter)?.AttachedObject.Id, "New Folder").ConfigureAwait(true);
                result = new SceneCommandResult(createdFolder.Succeeded, createdFolder.OperationResultId);
                break;
            case SceneExplorerCommandKind.Rename:
                this.RenameRequested?.Invoke(this, new RenameRequestedEventArgs(target ?? context.Anchor!));
                return;
            case SceneExplorerCommandKind.Copy:
                await this.CopyItemsAsync(context.Targets).ConfigureAwait(true);
                break;
            case SceneExplorerCommandKind.Cut:
                await this.CutItemsAsync(context.Targets).ConfigureAwait(true);
                break;
            case SceneExplorerCommandKind.Delete:
                result = await this.commandService.DeleteItemsAsync(context.Document, nodes, folders).ConfigureAwait(true);
                break;
            case SceneExplorerCommandKind.Paste:
            case SceneExplorerCommandKind.PasteAsChild:
                result = await this.PasteCapturedAsync(context, kind == SceneExplorerCommandKind.PasteAsChild).ConfigureAwait(true);
                break;
            case SceneExplorerCommandKind.RemoveFromFolder:
                result = await this.commandService.RemoveNodesFromFolderAsync(context.Document, nodes, ((FolderAdapter)target!.Parent!).Id).ConfigureAwait(true);
                break;
            case SceneExplorerCommandKind.MoveToSceneRoot:
                result = target!.Parent is FolderAdapter sourceFolder && ((SceneNodeAdapter)target).AttachedObject.Parent is null
                    ? await this.commandService.RemoveNodesFromFolderAsync(context.Document, nodes, sourceFolder.Id).ConfigureAwait(true)
                    : await this.commandService.ReparentNodesAsync(context.Document, nodes, newParentNodeId: null, preserveWorldTransform: false).ConfigureAwait(true);
                break;
            case SceneExplorerCommandKind.Expand:
                await this.ExpandItemAsync(context.Anchor!).ConfigureAwait(true);
                break;
            case SceneExplorerCommandKind.Collapse:
                await this.CollapseItemAsync(context.Anchor!).ConfigureAwait(true);
                break;
            case SceneExplorerCommandKind.Hide:
            case SceneExplorerCommandKind.Show:
                result = await this.commandService.SetEditorHiddenAsync(context.Document, nodes, kind == SceneExplorerCommandKind.Hide).ConfigureAwait(true);
                break;
            case SceneExplorerCommandKind.Lock:
            case SceneExplorerCommandKind.Unlock:
                foreach (var id in nodes)
                {
                    this.interaction!.SetLocked(id, kind == SceneExplorerCommandKind.Lock);
                }

                break;
            case SceneExplorerCommandKind.ShowAll:
                result = await this.commandService.SetEditorHiddenAsync(context.Document,
                    this.projection.Nodes.Select(static row => row.AttachedObject.Id).ToArray(), hidden: false).ConfigureAwait(true);
                break;
        }

        // A command may have awaited live sync while the user changed documents. Only reconcile its original owner.
        if (result?.Succeeded == true
            && kind is not (SceneExplorerCommandKind.Hide or SceneExplorerCommandKind.Show or SceneExplorerCommandKind.ShowAll)
            && ReferenceEquals(this.CreateCommandContext()?.Scene, context.Document.Scene))
        {
            var restoreDeleteNeighbor = kind == SceneExplorerCommandKind.Delete
                && selectionGeneration == this.selectionApplyGeneration
                && SameSelection(this.selectionService.GetContext(context.Document.DocumentId), context.Selection);
            await this.ReconcileProjectionAsync().ConfigureAwait(true);
            if (restoreDeleteNeighbor && this.SelectedItemsCount == 0
                && ReferenceEquals(this.CreateCommandContext()?.Scene, context.Document.Scene))
            {
                foreach (var oldRow in neighbors)
                {
                    ITreeItem? neighbor = oldRow switch
                    {
                        SceneNodeAdapter node => this.projection.GetNode(node.AttachedObject.Id),
                        FolderAdapter folder => this.projection.GetFolder(folder.Id),
                        SceneAdapter => this.Scene,
                        _ => null,
                    };
                    if (neighbor is not null && this.IsShown(neighbor))
                    {
                        this.SelectDisplayedItem(neighbor, this.ShownItems.ToArray(), isControlDown: false, isShiftDown: false);
                        _ = this.FocusItem(neighbor, RequestOrigin.Programmatic);
                        break;
                    }
                }
            }
        }

        this.RefreshContextActions();
    }

    private (Guid? ParentId, Guid? FolderId, Guid? InsertAfterId) ResolveCapturedPasteDestination(CapturedExplorerContext context, bool asChild)
    {
        var target = asChild ? context.Targets[0] : context.Destination;
        var node = target as SceneNodeAdapter;
        var folder = target as FolderAdapter ?? (!asChild ? node?.Parent as FolderAdapter : null);
        var parentId = asChild ? node!.AttachedObject.Id : node?.AttachedObject.Parent?.Id;
        if (!asChild && context.Targets.Count > 1
            && context.Targets.All(row => ReferenceEquals(row.Parent, target)))
        {
            parentId = node?.AttachedObject.Id;
            node = null;
        }

        if (node is null && folder is not null)
        {
            for (var owner = folder.Parent; owner is not null; owner = owner.Parent)
            {
                if (owner is SceneNodeAdapter scope)
                {
                    parentId = scope.AttachedObject.Id;
                    break;
                }
            }
        }

        return (parentId, folder?.Id, !asChild ? node?.AttachedObject.Id : null);
    }

    private async Task<SceneCommandResult> PasteCapturedAsync(CapturedExplorerContext context, bool asChild)
    {
        var (parentId, folderId, insertAfterId) = this.ResolveCapturedPasteDestination(context, asChild);
        var stagedIds = this.clipboardNodeIds.ToArray();
        var stagedSnapshots = asChild ? this.clipboardSnapshots.ToArray() : this.clipboardLocalSnapshots.ToArray();
        var isCut = this.clipboardIsCut;
        var payload = this.explorerClipboard;
        var clipboardVersion = this.clipboardGeneration;
        SceneCommandResult result;
        if (payload is not null)
        {
            result = await this.commandService.PasteExplorerItemsAsync(context.Document, payload, isCut,
                parentId, folderId, preserveWorld: asChild, insertAfterId).ConfigureAwait(true);
        }
        else if (isCut)
        {
            result = folderId is { } folder
                ? await this.commandService.MoveNodesToFolderAsync(context.Document, stagedIds, folder).ConfigureAwait(true)
                : await this.commandService.ReparentNodesAsync(context.Document, stagedIds, parentId,
                    preserveWorldTransform: asChild, insertAfterId).ConfigureAwait(true);
        }
        else
        {
            var parent = parentId is { } id ? this.projection.GetNode(id)?.AttachedObject : null;
            var data = new List<SceneNodeData>();
            foreach (var snapshot in stagedSnapshots)
            {
                if (!asChild)
                {
                    data.Add(snapshot);
                    continue;
                }

                var transient = SceneNode.CreateAndHydrate(context.Document.Scene, snapshot);
                if (!SceneTransformMath.TryPreserveWorldLocal(transient, parent, out var position, out var rotation, out var scale))
                {
                    return new SceneCommandResult(Succeeded: false) { ValidationMessage = "The pasted world pose cannot be represented." };
                }

                data.Add(snapshot with
                {
                    Components = snapshot.Components.Select(component => component is TransformData transform
                        ? transform with { Position = position, Rotation = rotation, Scale = scale } : component).ToList(),
                });
            }

            if (!this.IsContextCurrent(context))
            {
                return new SceneCommandResult(Succeeded: false);
            }

            var duplicated = await this.commandService.DuplicateNodesFromDataAsync(context.Document, data, parentId, folderId, insertAfterId).ConfigureAwait(true);
            result = new SceneCommandResult(duplicated.Succeeded, duplicated.OperationResultId);
        }

        if (result.Succeeded && isCut && this.clipboardIsCut && stagedIds.SequenceEqual(this.clipboardNodeIds)
            && clipboardVersion == this.clipboardGeneration && ReferenceEquals(payload, this.explorerClipboard)
            && ReferenceEquals(this.CreateCommandContext()?.Scene, context.Document.Scene))
        {
            this.OnClipboardCleared();
            this.ClipboardStateStore = ClipboardState.Empty;
            this.ClearCutMarks();
            this.RaiseClipboardChanged();
        }

        return result;
    }

    private void OnContextOwnerPropertyChanged(object? sender, PropertyChangedEventArgs args)
    {
        if (string.Equals(args.PropertyName, nameof(this.Scene), StringComparison.Ordinal))
        {
            this.OnPropertyChanged(nameof(this.CanChangeCategories));
        }

        if (args.PropertyName is nameof(this.Scene) or nameof(this.CurrentClipboardState))
        {
            this.RefreshContextActions();
        }
    }

    private sealed record CapturedExplorerContext(
        SceneDocumentCommandContext Document,
        IProject? Project,
        SceneSelectionContext Selection,
        IReadOnlyList<ITreeItem> SelectedRows,
        IReadOnlyList<ITreeItem> Targets,
        ITreeItem? Anchor,
        ITreeItem? Destination,
        bool Background,
        IReadOnlyList<(ITreeItem Row, ITreeItem? Parent, SceneNode? SceneParent)> Parentage,
        Oxygen.Editor.Projects.ProjectContext? ProjectActivation);
}
