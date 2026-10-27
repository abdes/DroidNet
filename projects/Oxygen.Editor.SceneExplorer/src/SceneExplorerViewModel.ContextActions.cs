// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.Windows.Input;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Controls;
using DroidNet.Controls.Menus;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Scenes;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Documents;
using Oxygen.Editor.Scenes.Edits.EditorView;
using Oxygen.Editor.Scenes.Edits.Hierarchy;
using Oxygen.Editor.Scenes.Edits.Instances;
using Oxygen.Editor.Scenes.Edits.Layout;
using Oxygen.Editor.Scenes.Layout;
using Oxygen.Editor.Scenes.Nodes;
using Oxygen.Editor.Scenes.Selection;
using Oxygen.Editor.Scenes.Views;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.SceneExplorer;

/// <summary>Captured command surfaces for the Explorer.</summary>
public partial class SceneExplorerViewModel
{
    // The menu group of the commands on an instance: selecting it, unpacking it and applying its changes to its source.
    private const string InstanceGroup = "instance";

    private readonly Dictionary<SceneExplorerCommandKind, AsyncRelayCommand> contextToolbarCommands = [];
    private readonly Dictionary<MenuItemData, SceneExplorerCommandKind> openMenuActions = [];

    // The open menu's Apply to items, by the row whose changes they apply.
    private readonly Dictionary<MenuItemData, ITreeItem> openMenuApply = [];
    private CapturedExplorerContext? openMenuContext;

    // The authored scenes the menu's instance row can apply its changes to, read when the menu opens.
    private (NodeReference Node, IReadOnlyList<ApplyTarget> Targets)? menuApplyTargets;
    private IMenuSource? openMenuSource;

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
        if (this.CurrentAuthoring() is not { } authoring)
        {
            return false;
        }

        if (anchor is not null && !this.GetSelectedItems().Contains(anchor))
        {
            await authoring.FinishEditsAsync().ConfigureAwait(true);
            if (!ReferenceEquals(this.CurrentAuthoring(), authoring) || !this.IsShown(anchor))
            {
                return false;
            }

            this.SelectDisplayedItem(anchor, [.. this.ShownItems], isControlDown: false, isShiftDown: false);
        }

        this.menuApplyTargets = await this.ReadApplyTargetsAsync(authoring, anchor).ConfigureAwait(true);
        return ReferenceEquals(this.CurrentAuthoring(), authoring);
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
        this.openMenuApply.Clear();
        if (context is not null)
        {
            string? lastGroup = null;
            foreach (var (kind, label, group) in this.GetContextActions(context))
            {
                if (lastGroup is not null && !string.Equals(lastGroup, group, StringComparison.Ordinal))
                {
                    if (string.Equals(lastGroup, InstanceGroup, StringComparison.Ordinal))
                    {
                        this.AddApplyItems(builder, context);
                    }

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

            if (string.Equals(lastGroup, InstanceGroup, StringComparison.Ordinal))
            {
                this.AddApplyItems(builder, context);
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
                var reason = this.openMenuApply.TryGetValue(item, out var row)
                    ? this.GetApplyDisabledReason(row)
                    : this.GetContextDisabledReason(this.openMenuActions[item], context);
                item.IsEnabled = reason is null;
                item.HelpText = reason;
                (item.Command as AsyncRelayCommand)?.NotifyCanExecuteChanged();
            }
        }
    }

    // The commands that change the scene, as opposed to those that only show, select or protect its nodes.
    private static bool ChangesScene(SceneExplorerCommandKind kind)
        => kind is SceneExplorerCommandKind.NewNode or SceneExplorerCommandKind.NewFolder or SceneExplorerCommandKind.Rename
            or SceneExplorerCommandKind.Cut or SceneExplorerCommandKind.Copy or SceneExplorerCommandKind.Paste
            or SceneExplorerCommandKind.PasteAsChild or SceneExplorerCommandKind.Delete or SceneExplorerCommandKind.RemoveFromFolder
            or SceneExplorerCommandKind.MoveToSceneRoot or SceneExplorerCommandKind.PilotCamera or SceneExplorerCommandKind.AlignCameraToView
            or SceneExplorerCommandKind.Unpack or SceneExplorerCommandKind.UnpackCompletely;

    // Which changes a row's Apply to moves: all of an instance root's, or a node's own.
    private static ChangeSelection ApplySelectionOf(NodeReference node)
        => node.IsOwned ? ChangeSelection.Instance : ChangeSelection.OfNode(node.Path);

    private static SymbolIconSource? GetContextActionIcon(SceneExplorerCommandKind kind) => kind switch
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

    private static ITreeItem? ResolveSharedPasteTarget(ITreeItem[] targets)
    {
        var first = targets[0];
        if (targets.All(item => ReferenceEquals(item.Parent, first.Parent)))
        {
            return targets.LastOrDefault(static row => row is SceneNodeAdapter) ?? first.Parent;
        }

        return null;
    }

    private static bool SameSelection(SceneSelectionContext left, SceneSelectionContext right)
        => left.Kind == right.Kind && left.PrimaryNode == right.PrimaryNode && left.PrimaryFolderId == right.PrimaryFolderId
            && left.SelectedNodes.SequenceEqual(right.SelectedNodes)
            && left.SelectedFolderIds.SequenceEqual(right.SelectedFolderIds);

    // Selecting the instance, then the rows a command creates at or under the context.
    private static IEnumerable<(SceneExplorerCommandKind Kind, string Label, string Group)> CreationActions(CapturedExplorerContext context)
    {
        var targets = context.Targets;
        var single = targets.Count == 1;
        var node = single ? targets[0] as SceneNodeAdapter : null;
        var instanced = single ? targets[0] as InstancedNodeAdapter : null;
        var folder = single ? targets[0] as FolderAdapter : null;
        if (instanced is not null)
        {
            yield return (SceneExplorerCommandKind.SelectInstance, "Select instance", InstanceGroup);
        }

        if (node is { IsInstanceRoot: true })
        {
            yield return (SceneExplorerCommandKind.Unpack, "Unpack", InstanceGroup);
            yield return (SceneExplorerCommandKind.UnpackCompletely, "Unpack completely", InstanceGroup);
        }

        if (targets.Count == 0 || single)
        {
            yield return (SceneExplorerCommandKind.NewNode, node is not null || instanced is not null ? "New child node" : folder is not null ? "New node in folder" : "New node", "create");
            yield return (SceneExplorerCommandKind.NewFolder, folder is not null ? "New subfolder" : node is not null ? "New folder beside node" : "New folder", "create");
        }
    }

    // Renaming, the clipboard and deleting.
    private static IEnumerable<(SceneExplorerCommandKind Kind, string Label, string Group)> EditActions(CapturedExplorerContext context)
    {
        var targets = context.Targets;
        var root = targets.Count == 0;
        var single = targets.Count == 1;
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
        if (single && targets[0] is SceneNodeAdapter)
        {
            yield return (SceneExplorerCommandKind.PasteAsChild, "Paste as child (keep world pose)", "edit");
        }

        if (!root)
        {
            yield return (SceneExplorerCommandKind.Delete, single && targets[0] is FolderAdapter ? "Remove folder (keep objects)" : single ? "Delete node" : "Delete selected items", "edit");
        }
    }

    // Expanding the anchor, and moving a node out of its folder or parent.
    private static IEnumerable<(SceneExplorerCommandKind Kind, string Label, string Group)> HierarchyActions(CapturedExplorerContext context)
    {
        var targets = context.Targets;
        var single = targets.Count == 1;
        var node = single ? targets[0] as SceneNodeAdapter : null;
        if (!context.Background && context.Anchor is LayoutItemAdapter { HasChildren: true } anchor && (targets.Count == 0 || single))
        {
            yield return (anchor.IsExpanded ? SceneExplorerCommandKind.Collapse : SceneExplorerCommandKind.Expand,
                anchor.IsExpanded ? "Collapse" : "Expand", "hierarchy");
        }

        if (node?.Parent is FolderAdapter)
        {
            yield return (SceneExplorerCommandKind.RemoveFromFolder, "Remove from folder", "hierarchy");
        }

        if (node is not null && (node.AttachedObject.Hierarchy.Parent is not null || node.Parent is FolderAdapter))
        {
            yield return (SceneExplorerCommandKind.MoveToSceneRoot, "Move to scene root", "hierarchy");
        }
    }

    private static NodeReference[] ReferencesOf(CapturedExplorerContext context)
        => [.. context.Targets.OfType<NodeRowAdapter>().Select(static row => row.Reference)];

    // A new folder at the end of the target folder, beside the target node, or at the scene root.
    private static Task<SceneEditResult> CreateFolderAsync(SceneAuthoring authoring, ITreeItem? target)
    {
        var container = (target is SceneNodeAdapter ? target.Parent : target) switch
        {
            FolderAdapter folderRow => LayoutPosition.InFolder(folderRow.Id, 0),
            SceneNodeAdapter nodeRow => LayoutPosition.UnderNode(nodeRow.AttachedObject.Id, 0),
            _ => LayoutPosition.AtRoot(0),
        };
        return authoring.ExecuteAsync(new CreateFolder(FolderId.New(), LayoutScopes.EndOf(authoring.Layout, container), "New Folder"));
    }

    private static (NodeId? ParentId, FolderId? FolderId, NodeId? InsertAfterId) ResolveCapturedPasteDestination(CapturedExplorerContext context, bool asChild)
    {
        var target = asChild ? context.Targets[0] : context.Destination;
        var node = target as SceneNodeAdapter;
        var folder = target as FolderAdapter ?? (!asChild ? node?.Parent as FolderAdapter : null);
        var parentId = asChild ? node!.AttachedObject.Id : node?.AttachedObject.Hierarchy.Parent?.Id;
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

    private static PasteTarget PasteTargetOf(CapturedExplorerContext context, bool asChild)
    {
        var (parentId, folderId, insertAfterId) = ResolveCapturedPasteDestination(context, asChild);
        return new PasteTarget(parentId, folderId, insertAfterId);
    }

    // Deleting folders deletes what they hold: their nodes first, then the folders, innermost first.
    private static SceneEditBatch DeleteEdit(IList<ExplorerEntryData> layout, IReadOnlyList<NodeId> nodes, IReadOnlyList<FolderId> folders)
    {
        var heldNodes = new List<NodeId>(nodes);
        var heldFolders = new List<FolderId>();
        void Collect(FolderId folder)
        {
            foreach (var entry in LayoutScopes.EntriesOf(layout, LayoutPosition.InFolder(folder, 0)))
            {
                if (entry.Folder is { } inner)
                {
                    Collect(inner);
                }
                else if (entry.Node is { } node)
                {
                    heldNodes.Add(node);
                }
            }

            heldFolders.Add(folder);
        }

        foreach (var folder in folders)
        {
            Collect(folder);
        }

        List<ISceneEdit> edits = [];
        if (heldNodes.Count > 0)
        {
            edits.Add(new RemoveNodes([.. heldNodes.Distinct()]));
        }

        edits.AddRange(heldFolders.Distinct().Select(static folder => new RemoveFolder(folder, PromoteChildren: false)));
        return new SceneEditBatch("Delete", edits);
    }

    // The entries go right after the folder, in the folder's own container.
    private static Task<SceneEditResult> OutOfFolderAsync(SceneAuthoring authoring, IReadOnlyList<NodeId> nodes, FolderId folder)
        => LayoutScopes.PositionOf(authoring.Layout, LayoutEntryId.Of(folder)) is { } place
            ? authoring.ExecuteAsync(new MoveLayoutEntries([.. nodes.Select(LayoutEntryId.Of)], place with { Index = place.Index + 1 }))
            : Task.FromResult(new SceneEditResult(SceneEditOutcome.Invalid, [SceneDiagnostic.Error(SceneEditCodes.FolderMissing, "The folder no longer exists.")]));

    private static bool IsCameraNode(SceneNodeAdapter node)
        => node.AttachedObject.Components.OfType<CameraComponent>().Any();

    private bool CanRemoveContextItems()
        => this.GetContextDisabledReason(
            SceneExplorerCommandKind.Delete,
            this.CaptureExplorerContext(anchor: null, background: false)) is null;

    // Copied nodes an instance brought in go as owned subtrees with their resolved values, linked to nothing (scene
    // instances 6.2).
    private async Task StageExplorerClipboardAsync(IReadOnlyList<ITreeItem> items, bool cut)
    {
        if (this.CurrentAuthoring() is not { } authoring)
        {
            return;
        }

        var payload = SceneClipboard.Capture(
            authoring.Scene,
            authoring.Layout,
            [.. items.OfType<SceneNodeAdapter>().Select(static row => row.AttachedObject.Id)],
            [.. items.OfType<FolderAdapter>().Select(static row => row.Id)]);
        NodeReference[] instanced = [.. items.OfType<InstancedNodeAdapter>().Select(static row => row.Reference)];
        if (!cut && instanced.Length != 0)
        {
            if (this.Conversion() is not { } conversion)
            {
                return;
            }

            var copied = await conversion.CopyAsync(authoring.Scene, instanced, this.StructureOf, this.session?.Lifetime ?? CancellationToken.None).ConfigureAwait(true);
            if (copied.Value is not { } copiedNodes)
            {
                this.PublishInstanceResult(SceneOperationKinds.NodeDuplicate, "Nodes not copied", SceneEditResult.Of(SceneEditValidation.Refused(copied.Refusal!)));
                return;
            }

            if (!ReferenceEquals(this.CurrentAuthoring(), authoring))
            {
                return;
            }

            var copies = SceneClipboard.Of(copiedNodes);
            payload = payload is null ? copies : new SceneClipboard([.. payload.Entries, .. copies.Entries], [.. payload.Nodes, .. copies.Nodes], worldNodes: null);
        }

        if (payload is null)
        {
            return;
        }

        this.clipboard.Stage(payload, cut, authoring.Scene);
        this.StageClipboard(items, cut);
    }

    // Apply to takes the row's own changes, or all of an instance root's, while neither the row nor an ancestor is locked.
    private string? GetApplyDisabledReason(ITreeItem row)
        => this.FindLockedContextRow(row) is { } locked ? $"Locked by {locked.Label}." : null;

    // The authored scenes an instance root's or instanced row's changes can go to, while it has any; null for another row.
    private async Task<(NodeReference Node, IReadOnlyList<ApplyTarget> Targets)?> ReadApplyTargetsAsync(SceneAuthoring authoring, ITreeItem? anchor)
    {
        if (this.session?.Context is not { } project
            || anchor is not (SceneNodeAdapter { IsInstanceRoot: true } or InstancedNodeAdapter)
            || anchor is not NodeRowAdapter { Reference: var reference }
            || authoring.Scene.AllNodes.FirstOrDefault(node => node.Id == reference.Node)?.Components.OfType<SceneInstanceComponent>().FirstOrDefault() is not { } instance
            || instance.Changes.Nodes.All(entry => ApplySelectionOf(reference).Select(entry.Key, entry.Value).IsEmpty))
        {
            return null;
        }

        return (reference, await new SourceApplication(project).TargetsAsync(authoring.Scene, reference, this.session.Lifetime).ConfigureAwait(true));
    }

    // One Apply to item per authored scene the menu's single instance row can apply its changes to.
    private void AddApplyItems(MenuBuilder builder, CapturedExplorerContext context)
    {
        if (this.menuApplyTargets is not { } read || context.Targets is not [NodeRowAdapter row] || row.Reference != read.Node)
        {
            return;
        }

        foreach (var target in read.Targets)
        {
            var reason = this.GetApplyDisabledReason(row);
            var item = new MenuItemData
            {
                Text = target.Label,
                HelpText = reason,
                IsEnabled = reason is null,
                Command = new AsyncRelayCommand(
                    () => this.ApplyToSourceAsync(context.Authoring, read.Node, target),
                    () => this.GetApplyDisabledReason(row) is null),
            };
            this.openMenuApply.Add(item, row);
            _ = builder.AddMenuItem(item);
        }
    }

    private async Task ApplyToSourceAsync(SceneAuthoring authoring, NodeReference node, ApplyTarget target)
    {
        if (this.session?.Context is not { } project)
        {
            return;
        }

        var result = await new SourceApplication(project).ExecuteAsync(authoring, node.Node, ApplySelectionOf(node), target, this.session.Lifetime).ConfigureAwait(true);
        this.PublishInstanceResult(SceneOperationKinds.EditInstance, "Changes not applied", result, "Changes kept on the instance");
    }

    // Unpack takes one instance root that is not locked, and needs the contents of its source.
    private string? GetUnpackDisabledReason(CapturedExplorerContext context)
        => context.Targets is not [SceneNodeAdapter { IsInstanceRoot: true } root] ? "Select one instance root."
            : this.Conversion() is null ? ContentsUnreadable
            : this.FindLockedContextRow(root) is { } locked ? $"Locked by {locked.Label}."
            : null;

    // What makes owned nodes of an instance's nodes, when the window reads them.
    private InstanceConversion? Conversion()
        => this.session?.Context is { } project && this.nodes is { } reader ? new InstanceConversion(project, reader) : null;

    private async Task<SceneEditResult> UnpackAsync(SceneAuthoring authoring, NodeId instance, InstanceUnpack depth)
    {
        var result = await this.Conversion()!.ExecuteUnpackAsync(authoring, instance, depth, this.session?.Lifetime ?? CancellationToken.None).ConfigureAwait(true);
        this.PublishInstanceResult(SceneOperationKinds.EditInstance, "Instance not unpacked", result);
        return result;
    }

    // Tells the user why an instance command did nothing, or what it dropped.
    private void PublishInstanceResult(string operationKind, string failureTitle, SceneEditResult result, string warningTitle = "Changes dropped")
    {
        if (this.operationResults is { } publisher && this.statusReducer is { } reducer)
        {
            SceneOperationResults.PublishEditResult(publisher, reducer, operationKind, failureTitle, warningTitle, result, new AffectedScope { SceneId = this.Scene?.AttachedObject.Id });
        }
    }

    private CapturedExplorerContext? CaptureExplorerContext(ITreeItem? anchor, bool background)
    {
        if (this.CurrentAuthoring() is not { } authoring)
        {
            return null;
        }

        var selection = this.selectionService.GetContext(authoring.Document);
        var selected = this.GetSelectedItems().ToArray();

        // Authoritative identities can include collapsed/unrealized rows. Never silently shrink that batch.
        foreach (var row in this.RowsOf(selection.SelectedNodes))
        {
            if (!selected.Contains(row))
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
        var targets = background || anchor is SceneAdapter ? [] : selected;
        if (!background && anchor is not SceneAdapter and not null && !targets.Contains(anchor))
        {
            targets = [anchor];
        }

        var destination = background || anchor is SceneAdapter || targets.Length == 0
            ? this.Scene
            : targets.Length == 1 ? targets[0] : ResolveSharedPasteTarget(targets);
        return new(
            authoring,
            this.session?.Project,
            selection,
            Array.AsReadOnly(selected),
            Array.AsReadOnly(targets),
            anchor,
            destination,
            background,
            [.. selected.Select(static row => (Row: row, row.Parent, SceneParent: (row as SceneNodeAdapter)?.AttachedObject.Hierarchy.Parent))],
            this.session?.Context);
    }

    private bool IsContextCurrent(CapturedExplorerContext context)
        => !this.isDisposed
            && ReferenceEquals(this.session?.Project, context.Project)
            && ReferenceEquals(this.session?.Context, context.ProjectActivation)
            && ReferenceEquals(this.CurrentAuthoring(), context.Authoring)
            && this.openDocuments.Find(context.Authoring.Document) is { Content: SceneContent { Scene: var scene } } && ReferenceEquals(scene, context.Authoring.Scene)
            && SameSelection(this.selectionService.GetContext(context.Authoring.Document), context.Selection)
            && context.Selection.OwnedNodeIds.All(id => this.projection.GetNode(id) is not null)
            && context.Selection.SelectedFolderIds.All(id => this.projection.GetFolder(id) is not null)
            && context.SelectedRows.All(this.IsCurrentContextRow)
            && context.Parentage.All(static captured => ReferenceEquals(captured.Row.Parent, captured.Parent)
                && ReferenceEquals((captured.Row as SceneNodeAdapter)?.AttachedObject.Hierarchy.Parent, captured.SceneParent))
            && (context.Anchor is null || this.IsCurrentContextRow(context.Anchor));

    private bool IsCurrentContextRow(ITreeItem row) => row switch
    {
        SceneNodeAdapter node => ReferenceEquals(this.projection.GetNode(node.AttachedObject.Id), row),
        InstancedNodeAdapter instanced => ReferenceEquals(this.projection.GetRow(instanced.Reference), row),
        FolderAdapter folder => ReferenceEquals(this.projection.GetFolder(folder.Id), row),
        SceneAdapter scene => ReferenceEquals(this.Scene, scene),
        _ => false,
    };

    private IEnumerable<(SceneExplorerCommandKind Kind, string Label, string Group)> GetContextActions(CapturedExplorerContext context)
        => CreationActions(context)
            .Concat(EditActions(context))
            .Concat(HierarchyActions(context))
            .Concat(this.CameraActions(context))
            .Concat(this.StateActions(context));

    // Viewing through, piloting and aligning a single camera node.
    private IEnumerable<(SceneExplorerCommandKind Kind, string Label, string Group)> CameraActions(CapturedExplorerContext context)
    {
        if (context.Targets is not [SceneNodeAdapter node] || !IsCameraNode(node))
        {
            yield break;
        }

        var viewport = this.QueryCameraViewport();
        var viewing = viewport?.ViewedCameraId == node.AttachedObject.Id;
        yield return (SceneExplorerCommandKind.LookThroughCamera, viewing ? "Return to editor camera" : "Look through camera", "camera");
        yield return (SceneExplorerCommandKind.PilotCamera, viewing && viewport!.IsPiloting ? "Stop piloting camera" : "Pilot camera", "camera");
        yield return (SceneExplorerCommandKind.AlignCameraToView, "Align camera to view", "camera");
    }

    // Hiding and locking the selected node rows, and showing every hidden node from the scene root.
    private IEnumerable<(SceneExplorerCommandKind Kind, string Label, string Group)> StateActions(CapturedExplorerContext context)
    {
        var targets = context.Targets;
        var single = targets.Count == 1;
        if (targets.Count > 0 && targets.All(static row => row is NodeRowAdapter))
        {
            var row = single ? (NodeRowAdapter)targets[0] : null;
            var hidden = row is not null && (row.IsEffectivelyHiddenInEditor || this.interaction?.IsHidden(row.Reference) == true);
            var locked = row is not null && this.FindLockedContextRow(row) is not null;
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

        if (targets.Count == 0 && this.interaction?.HiddenNodes().Count > 0)
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

        // A model's preview scene is read-only: what changes it is refused, with the model's reason.
        if (ChangesScene(kind) && this.CurrentAuthoring()?.ReadOnlyReason is { } readOnly)
        {
            return readOnly;
        }

        var targets = context.Targets;
        var single = targets.Count == 1;
        if (kind == SceneExplorerCommandKind.SelectInstance)
        {
            return single && targets[0] is InstancedNodeAdapter ? null : "Select one node an instance brought in.";
        }

        if (kind is SceneExplorerCommandKind.Unpack or SceneExplorerCommandKind.UnpackCompletely)
        {
            return this.GetUnpackDisabledReason(context);
        }

        if (this.GetInstancedRefusal(kind, context) is { } refusal)
        {
            return refusal;
        }

        if (kind is SceneExplorerCommandKind.LookThroughCamera or SceneExplorerCommandKind.PilotCamera or SceneExplorerCommandKind.AlignCameraToView)
        {
            return this.GetCameraActionDisabledReason(kind, [.. targets.OfType<SceneNodeAdapter>()], single);
        }

        if (kind == SceneExplorerCommandKind.Rename && context.Anchor is SceneAdapter && !context.Background)
        {
            return null;
        }

        return this.GetSelectionDisabledReason(kind, context)
            ?? this.GetLockDisabledReason(kind, context)
            ?? this.GetInheritedStateDisabledReason(kind, context)
            ?? this.GetPasteDisabledReason(kind, context);
    }

    // Whether the selection has the shape the command needs: how many rows, which kinds, and the editor state service.
    private string? GetSelectionDisabledReason(SceneExplorerCommandKind kind, CapturedExplorerContext context)
    {
        var targets = context.Targets;
        var single = targets.Count == 1;
        var rows = targets.Count(static row => row is NodeRowAdapter);
        if (kind is SceneExplorerCommandKind.NewNode or SceneExplorerCommandKind.NewFolder && targets.Count > 1)
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

        if (kind is SceneExplorerCommandKind.Hide or SceneExplorerCommandKind.Show or SceneExplorerCommandKind.Lock or SceneExplorerCommandKind.Unlock
            && (this.interaction is null || rows != targets.Count || rows == 0))
        {
            return "Editor state requires a node-only selection and the workspace service.";
        }

        return targets.Any(static row => row is SceneAdapter)
            && kind is SceneExplorerCommandKind.Rename or SceneExplorerCommandKind.Delete or SceneExplorerCommandKind.Cut or SceneExplorerCommandKind.Copy
            ? "The scene root is not a selection edit target."
            : null;
    }

    // Whether a lock protects a row the command changes, or a node below one it moves or removes.
    private string? GetLockDisabledReason(SceneExplorerCommandKind kind, CapturedExplorerContext context)
    {
        var targets = context.Targets;
        var manipulates = kind is SceneExplorerCommandKind.NewNode or SceneExplorerCommandKind.NewFolder or SceneExplorerCommandKind.Rename
            or SceneExplorerCommandKind.Cut or SceneExplorerCommandKind.Delete or SceneExplorerCommandKind.RemoveFromFolder
            or SceneExplorerCommandKind.MoveToSceneRoot;
        if (manipulates && targets.Select(this.FindLockedContextRow).FirstOrDefault(static locked => locked is not null) is { } lockedTarget)
        {
            return $"Locked by {lockedTarget.Label}.";
        }

        if (kind is SceneExplorerCommandKind.Cut or SceneExplorerCommandKind.Delete or SceneExplorerCommandKind.MoveToSceneRoot)
        {
            var ids = targets.OfType<SceneNodeAdapter>().Select(static row => row.AttachedObject.Id).ToHashSet();
            foreach (var descendant in this.projection.Nodes)
            {
                for (var parent = descendant.AttachedObject.Hierarchy.Parent; parent is not null; parent = parent.Hierarchy.Parent)
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

        return null;
    }

    // Whether showing or unlocking a row would leave it hidden or locked through an ancestor.
    private string? GetInheritedStateDisabledReason(SceneExplorerCommandKind kind, CapturedExplorerContext context)
    {
        if (kind is not (SceneExplorerCommandKind.Show or SceneExplorerCommandKind.Unlock))
        {
            return null;
        }

        foreach (var row in context.Targets.OfType<NodeRowAdapter>())
        {
            foreach (var parent in this.interaction!.GetAncestors(context.Authoring.Scene, row.Reference) ?? [])
            {
                if (kind == SceneExplorerCommandKind.Show ? this.interaction!.IsHidden(parent) : this.interaction!.IsLocked(parent))
                {
                    return $"{(kind == SceneExplorerCommandKind.Show ? "Hidden" : "Locked")} by {this.projection.GetRow(parent)?.Label ?? parent.ToString()}.";
                }
            }
        }

        return null;
    }

    // Whether the clipboard can paste at the command's destination.
    private string? GetPasteDisabledReason(SceneExplorerCommandKind kind, CapturedExplorerContext context)
    {
        if (kind is not (SceneExplorerCommandKind.Paste or SceneExplorerCommandKind.PasteAsChild))
        {
            return null;
        }

        var targets = context.Targets;
        var asChild = kind == SceneExplorerCommandKind.PasteAsChild;
        var destination = asChild
            ? targets is [SceneNodeAdapter] ? targets[0] : null
            : context.Destination;
        if (destination is null)
        {
            return "The selection has no unambiguous paste destination.";
        }

        if (this.clipboard.Payload is not { } payload || !this.ClipboardLifetimeIsCurrent())
        {
            return "The clipboard has no payload for this scene.";
        }

        if (destination is not SceneAdapter && this.FindLockedContextRow(destination) is { } locked)
        {
            return $"Locked by {locked.Label}.";
        }

        return payload.TryPaste(context.Authoring.Scene, context.Authoring.Layout, PasteTargetOf(context, asChild), this.clipboard.IsCut, preserveWorld: asChild, out _, out var refused)
            ? null
            : refused.Message;
    }

    /// <summary>
    /// Refuses a structural command whose targets or selection hold a node an instance brought in, or that would paste
    /// under one, as a whole (scene instances 6.2, 8.3).
    /// </summary>
    /// <param name="kind">The command.</param>
    /// <param name="context">The captured context.</param>
    /// <returns>The refusal, naming the node and its source; otherwise, <see langword="null"/>.</returns>
    private string? GetInstancedRefusal(SceneExplorerCommandKind kind, CapturedExplorerContext context)
    {
        if (kind == SceneExplorerCommandKind.Copy)
        {
            return context.Targets.Any(static row => row is InstancedNodeAdapter) && this.Conversion() is null
                ? ContentsUnreadable
                : null;
        }

        var structural = kind is SceneExplorerCommandKind.NewNode or SceneExplorerCommandKind.NewFolder or SceneExplorerCommandKind.Rename
            or SceneExplorerCommandKind.Cut or SceneExplorerCommandKind.Delete or SceneExplorerCommandKind.Paste
            or SceneExplorerCommandKind.PasteAsChild or SceneExplorerCommandKind.RemoveFromFolder or SceneExplorerCommandKind.MoveToSceneRoot;
        if (!structural)
        {
            return null;
        }

        // A command over the selection covers its rows that are not realized too.
        var targetNodes = context.Targets.OfType<NodeRowAdapter>().Select(static row => row.Reference);
        if (context.Targets.Count > 1 || (context.Targets.Count == 1 && context.SelectedRows.Contains(context.Targets[0])))
        {
            targetNodes = targetNodes.Concat(context.Selection.SelectedNodes);
        }

        if (kind == SceneExplorerCommandKind.Paste && context.Destination is InstancedNodeAdapter destination)
        {
            targetNodes = targetNodes.Append(destination.Reference);
        }

        return InstanceRules.RefusesStructure(context.Authoring.Scene, targetNodes, node => (this.projection.GetRow(node) as InstancedNodeAdapter)?.Name)?.Message;
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

        if (row is InstancedNodeAdapter instanced && this.Scene?.AttachedObject is { } scene)
        {
            // Contents not read yet protect the node through its instance root.
            return this.interaction?.GetLock(scene, instanced.Reference) switch
            {
                { Owner: { } lockOwner } => this.projection.GetRow(lockOwner) ?? row,
                { UnreadInstance: { } root } => this.projection.GetNode(root) ?? row,
                _ => null,
            };
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
        var selectionGeneration = this.selectionApplyGeneration;
        var neighbors = kind == SceneExplorerCommandKind.Delete ? this.DeleteNeighbors(context) : [];
        var (result, refresh) = await this.RunContextCommandAsync(kind, context).ConfigureAwait(true);
        if (!refresh)
        {
            return;
        }

        // A command may have awaited live sync while the user changed documents. Only reconcile its original owner.
        if (result?.Succeeded == true
            && kind is not (SceneExplorerCommandKind.Hide or SceneExplorerCommandKind.Show or SceneExplorerCommandKind.ShowAll)
            && ReferenceEquals(this.CurrentAuthoring(), context.Authoring))
        {
            var restoreDeleteNeighbor = kind == SceneExplorerCommandKind.Delete
                && selectionGeneration == this.selectionApplyGeneration
                && SameSelection(this.selectionService.GetContext(context.Authoring.Document), context.Selection);
            await this.ReconcileProjectionAsync().ConfigureAwait(true);
            if (restoreDeleteNeighbor && this.SelectedItemsCount == 0
                && ReferenceEquals(this.CurrentAuthoring(), context.Authoring))
            {
                this.SelectFirstShown(neighbors);
            }
        }

        this.RefreshContextActions();
    }

    // Runs a command. One that hands its work to another view, as renaming or viewing through a camera, needs no refresh.
    private async Task<(SceneEditResult? Result, bool Refresh)> RunContextCommandAsync(SceneExplorerCommandKind kind, CapturedExplorerContext context)
    {
        var target = context.Targets.Count > 0 ? context.Targets[0] : null;
        switch (kind)
        {
            case SceneExplorerCommandKind.Rename:
                this.RenameRequested?.Invoke(this, new RenameRequestedEventArgs(target ?? context.Anchor!));
                return (null, false);
            case SceneExplorerCommandKind.Copy:
                await this.CopyItemsAsync(context.Targets).ConfigureAwait(true);
                return (null, true);
            case SceneExplorerCommandKind.Cut:
                await this.CutItemsAsync(context.Targets).ConfigureAwait(true);
                return (null, true);
            case SceneExplorerCommandKind.Expand:
                await this.ExpandItemAsync(context.Anchor!).ConfigureAwait(true);
                return (null, true);
            case SceneExplorerCommandKind.Collapse:
                await this.CollapseItemAsync(context.Anchor!).ConfigureAwait(true);
                return (null, true);
            case SceneExplorerCommandKind.Lock:
            case SceneExplorerCommandKind.Unlock:
                foreach (var reference in ReferencesOf(context))
                {
                    this.interaction!.SetLocked(reference, kind == SceneExplorerCommandKind.Lock);
                }

                return (null, true);
            case SceneExplorerCommandKind.SelectInstance:
                _ = await this.SetSelectedNodes([((InstancedNodeAdapter)target!).Reference.Node], focusPrimary: true).ConfigureAwait(true);
                return (null, false);
            case SceneExplorerCommandKind.LookThroughCamera:
            case SceneExplorerCommandKind.PilotCamera:
            case SceneExplorerCommandKind.AlignCameraToView:
                await this.RequestCameraCommandAsync(kind, context.Targets.OfType<SceneNodeAdapter>().First().AttachedObject.Id).ConfigureAwait(true);
                return (null, false);
            default:
                return (await this.RunEditCommandAsync(kind, context).ConfigureAwait(true), true);
        }
    }

    // The commands that edit the scene through its authoring.
    private async Task<SceneEditResult?> RunEditCommandAsync(SceneExplorerCommandKind kind, CapturedExplorerContext context)
    {
        var authoring = context.Authoring;
        var target = context.Targets.Count > 0 ? context.Targets[0] : null;
        NodeId[] targetNodes = [.. context.Targets.OfType<SceneNodeAdapter>().Select(static row => row.AttachedObject.Id)];
        return kind switch
        {
            SceneExplorerCommandKind.NewNode => await this.CreateNodeAsync(authoring, target).ConfigureAwait(true),
            SceneExplorerCommandKind.NewFolder => await CreateFolderAsync(authoring, target).ConfigureAwait(true),
            SceneExplorerCommandKind.Delete => await authoring.ExecuteAsync(
                DeleteEdit(authoring.Layout, targetNodes, [.. context.Targets.OfType<FolderAdapter>().Select(static row => row.Id)])).ConfigureAwait(true),
            SceneExplorerCommandKind.Paste or SceneExplorerCommandKind.PasteAsChild
                => await this.PasteCapturedAsync(context, kind == SceneExplorerCommandKind.PasteAsChild).ConfigureAwait(true),
            SceneExplorerCommandKind.RemoveFromFolder => await OutOfFolderAsync(authoring, targetNodes, ((FolderAdapter)target!.Parent!).Id).ConfigureAwait(true),
            SceneExplorerCommandKind.MoveToSceneRoot => target!.Parent is FolderAdapter sourceFolder && ((SceneNodeAdapter)target).AttachedObject.Hierarchy.Parent is null
                ? await OutOfFolderAsync(authoring, targetNodes, sourceFolder.Id).ConfigureAwait(true)
                : await authoring.ExecuteAsync(new ReparentNodes(targetNodes, parent: null, preserveWorld: false)).ConfigureAwait(true),
            SceneExplorerCommandKind.Hide or SceneExplorerCommandKind.Show
                => await authoring.ExecuteAsync(new SetEditorHidden(ReferencesOf(context), kind == SceneExplorerCommandKind.Hide)).ConfigureAwait(true),
            SceneExplorerCommandKind.ShowAll => await authoring.ExecuteAsync(new SetEditorHidden([.. this.interaction!.HiddenNodes()], hidden: false)).ConfigureAwait(true),
            SceneExplorerCommandKind.Unpack or SceneExplorerCommandKind.UnpackCompletely => await this.UnpackAsync(
                authoring,
                ((SceneNodeAdapter)target!).AttachedObject.Id,
                kind == SceneExplorerCommandKind.Unpack ? InstanceUnpack.One : InstanceUnpack.Complete).ConfigureAwait(true),
            _ => null,
        };
    }

    // A new empty node: under the target node, in the target folder, or at the scene root.
    private Task<SceneEditResult> CreateNodeAsync(SceneAuthoring authoring, ITreeItem? target)
    {
        var scene = authoring.Scene;
        var node = SceneNodeTemplates.Empty(scene, this.GetNextEntityName());
        var folder = (target as FolderAdapter)?.Id;
        var parent = (target as SceneNodeAdapter)?.AttachedObject.Id;
        if (folder is { } inFolder && LayoutScopes.TryScopeOf(authoring.Layout, LayoutEntryId.Of(inFolder), out var folderScope))
        {
            parent = folderScope;
        }

        return authoring.ExecuteAsync(new InsertNodes([SceneNodeTemplates.Append(scene, authoring.Layout, node, parent, folder)]));
    }

    // The rows shown after the anchor, then those before it nearest first, which a delete leaves for the selection.
    private ITreeItem[] DeleteNeighbors(CapturedExplorerContext context)
    {
        var anchorIndex = context.Anchor is { } anchor ? this.ShownIndexOf(anchor) : -1;
        return [.. this.ShownItems.Skip(anchorIndex + 1).Concat(this.ShownItems.Take(Math.Max(0, anchorIndex)).Reverse())
            .Where(row => !context.Targets.Contains(row))];
    }

    // Selects the first of the rows a delete left that is still shown, by its row now.
    private void SelectFirstShown(IEnumerable<ITreeItem> neighbors)
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
                this.SelectDisplayedItem(neighbor, [.. this.ShownItems], isControlDown: false, isShiftDown: false);
                _ = this.FocusItem(neighbor, RequestOrigin.Programmatic);
                return;
            }
        }
    }

    private async Task<SceneEditResult?> PasteCapturedAsync(CapturedExplorerContext context, bool asChild)
    {
        var isCut = this.clipboard.IsCut;
        var clipboardVersion = this.clipboard.Generation;
        if (this.clipboard.Payload is not { } payload
            || !payload.TryPaste(context.Authoring.Scene, context.Authoring.Layout, PasteTargetOf(context, asChild), isCut, preserveWorld: asChild, out var paste, out var refused))
        {
            return null;
        }

        _ = refused;
        var result = await context.Authoring.ExecuteAsync(paste).ConfigureAwait(true);
        if (result.Succeeded && isCut && this.clipboard.IsCut && clipboardVersion == this.clipboard.Generation
            && ReferenceEquals(payload, this.clipboard.Payload) && ReferenceEquals(this.CurrentAuthoring(), context.Authoring))
        {
            await this.ClearClipboardAsync().ConfigureAwait(true);
        }

        return result;
    }

    private void OnContextOwnerPropertyChanged(object? sender, PropertyChangedEventArgs args)
    {
        // Clipboard changes refresh the actions through ClipboardContentChanged.
        if (string.Equals(args.PropertyName, nameof(this.Scene), StringComparison.Ordinal))
        {
            this.OnPropertyChanged(nameof(this.CanChangeCategories));
            this.RefreshContextActions();
        }
    }

    /// <summary>What the window's active viewport shows, or null when no scene viewport exists.</summary>
    private SceneCameraViewState? QueryCameraViewport()
        => this.views?.CameraStateOf(this.SceneDocumentId);

    private string? GetCameraActionDisabledReason(SceneExplorerCommandKind kind, SceneNodeAdapter[] nodes, bool single)
    {
        if (!single || nodes is not [{ } node] || !IsCameraNode(node))
        {
            return "Select one camera node.";
        }

        if (this.QueryCameraViewport() is not { } viewport)
        {
            return "Open the scene in a viewport first.";
        }

        var piloting = viewport.IsPiloting && viewport.ViewedCameraId == node.AttachedObject.Id;
        if ((kind is SceneExplorerCommandKind.PilotCamera && !piloting) || kind is SceneExplorerCommandKind.AlignCameraToView)
        {
            if (this.FindLockedContextRow(node) is { } locked)
            {
                return $"Locked by {locked.Label}.";
            }
        }

        return kind is SceneExplorerCommandKind.AlignCameraToView && viewport.ViewedCameraId is not null
            ? "The active viewport is looking through a scene camera; return it to the editor camera first."
            : null;
    }

    private async Task RequestCameraCommandAsync(SceneExplorerCommandKind kind, NodeId nodeId)
    {
        var viewport = this.QueryCameraViewport();
        var viewing = viewport?.ViewedCameraId == nodeId;
        var command = kind switch
        {
            SceneExplorerCommandKind.LookThroughCamera => viewing ? SceneCameraCommand.ReturnToEditorCamera : SceneCameraCommand.LookThrough,
            SceneExplorerCommandKind.PilotCamera => viewing && viewport!.IsPiloting ? SceneCameraCommand.StopPiloting : SceneCameraCommand.Pilot,
            _ => SceneCameraCommand.AlignToView,
        };
        if (this.views is { } sceneViews)
        {
            _ = await sceneViews.RunCameraCommandAsync(this.SceneDocumentId, nodeId, command).ConfigureAwait(true);
        }
    }

    private sealed record CapturedExplorerContext(
        SceneAuthoring Authoring,
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
