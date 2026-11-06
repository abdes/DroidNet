// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Aura.Dialogs;
using DroidNet.Controls;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Mounts;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentExplorer.Panes.ContentFolders;

/// <summary>Applies confirmed mount changes without a separate save step.</summary>
public partial class ContentFoldersViewModel
{
    private bool isApplyingMounts;
    private TaskCompletionSource? treeReload;
    private long treeReloadVersion;

    /// <summary>Gets a value indicating whether a confirmed configuration change is being validated and applied.</summary>
    public bool IsApplyingMounts
    {
        get => this.isApplyingMounts;
        private set
        {
            if (this.SetProperty(ref this.isApplyingMounts, value))
            {
                this.OnPropertyChanged(nameof(this.CanChangeMounts));
                this.OnPropertyChanged(nameof(this.MountProgressVisibility));
                this.MountKnownLocationCommand.NotifyCanExecuteChanged();
                this.MountLocalFolderCommand.NotifyCanExecuteChanged();
                this.ManageMountsCommand.NotifyCanExecuteChanged();
                this.ContentPriorityCommand.NotifyCanExecuteChanged();
            }
        }
    }

    /// <summary>Gets a value indicating whether mount commands can start.</summary>
    public bool CanChangeMounts => !this.IsApplyingMounts;

    /// <summary>Gets progress visibility without reserving idle toolbar space.</summary>
    public Visibility MountProgressVisibility => this.IsApplyingMounts ? Visibility.Visible : Visibility.Collapsed;

    /// <summary>Rebuilds a project's mount declarations from the Content mounts dialog's staged rows.</summary>
    /// <param name="expected">The project as it was when the dialog opened.</param>
    /// <param name="candidate">The editable copy that receives the mounts.</param>
    /// <param name="rows">The staged mounts in display order.</param>
    internal static void ApplyMountRows(ProjectContext expected, ProjectInfo candidate, IReadOnlyList<ContentMountRow> rows)
    {
        candidate.Mounts.Clear();
        candidate.OutputFolderViews.Clear();
        foreach (var row in rows)
        {
            switch (row.Kind)
            {
                case ContentMountKind.EngineContent:
                    // Always mounted from the SDK; the project does not declare it.
                    continue;
                case ContentMountKind.Library:
                    AddDeclaredMount(candidate, row, MountKind.Cooked);
                    continue;
                case ContentMountKind.ProjectSource:
                    AddDeclaredMount(candidate, row, MountKind.Source);
                    continue;
                case ContentMountKind.Derived:
                    candidate.OutputFolderViews.Add(new(row.RelativePath!, row.IsExpanded));
                    continue;
                default:
                    throw new InvalidOperationException("The Content mounts dialog staged an unknown mount kind.");
            }
        }

        candidate.CookedContentOrder = [.. PriorityOf(expected, rows)];
    }

    // The libraries that stay keep their priority; each new one lands above what it needs and below what needs it.
    private static IReadOnlyList<CookedContentSource> PriorityOf(ProjectContext expected, IReadOnlyList<ContentMountRow> rows)
    {
        var libraries = rows.Where(static row => row.Kind == ContentMountKind.Library).ToArray();
        IReadOnlyList<CookedContentSource> order = [.. CookedContentOrdering.Resolve(expected).Where(source => source.Kind == CookedContentSourceKind.ProjectOutput
            || libraries.Any(row => !row.IsNew && string.Equals(row.Name, source.Name, StringComparison.Ordinal)))];
        foreach (var row in libraries.Where(static row => row.IsNew))
        {
            var needs = NeedsOf(row);
            var neededBy = libraries.Where(other => NeedsOf(other).Contains(row.Name)).Select(static other => other.Name).ToArray();
            order = LibraryPlacement.Place(order, row.Name, needs, neededBy);
        }

        return order;
    }

    private static string[] NeedsOf(ContentMountRow row)
        => [.. (row.Facts?.Dependencies ?? []).Select(static dependency => dependency.Library)];

    private static void AddDeclaredMount(ProjectInfo candidate, ContentMountRow row, MountKind kind)
    {
        // A mount only a .mounts.json adds stays out of Project.oxy; a placeholder commits no path.
        if (!row.IsLocal)
        {
            candidate.Mounts.Add(new(row.Name, kind, row.RelativePath, row.IsExpanded));
        }
    }

    [RelayCommand(CanExecute = nameof(CanChangeMounts))]
    private async Task ManageMountsAsync()
    {
        var expected = session.Context;
        var candidate = this.GetActiveProjectInfo();
        var model = new ContentMountsViewModel(expected, dialogService, engineContent);
        var spec = new DialogSpec("Content mounts", new ContentMountsView { ViewModel = model })
        {
            PrimaryButtonText = "Apply",
            SecondaryButtonText = "Cancel",
            CloseButtonText = string.Empty,
            DefaultButton = DialogButton.Primary,
            PrimaryAction = _ => Task.FromResult(model.Validate()),
            MaxWidth = 900,
        };
        if (await dialogService.ShowAsync(spec, session.Lifetime).ConfigureAwait(true) != DialogButton.Primary)
        {
            return;
        }

        ApplyMountRows(expected, candidate, model.Mounts);
        var bindings = model.GetBindingChanges();
        if (candidate.Mounts.SequenceEqual(expected.DeclaredMounts) && candidate.OutputFolderViews.SequenceEqual(expected.OutputFolderViews) && bindings.IsEmpty)
        {
            return;
        }

        await this.ApplyMountCandidateAsync(expected, candidate, bindings).ConfigureAwait(true);

        // Show what was just mounted.
        if (model.Mounts.LastOrDefault(static row => row.IsNew) is { } added
            && (session.Context.Mounts.Any(mount => string.Equals(mount.Name, added.Name, StringComparison.Ordinal))
                || (added.Kind == ContentMountKind.Derived && session.Context.OutputFolderViews.Any(view => string.Equals(view.Folder, added.RelativePath, StringComparison.Ordinal)))))
        {
            contentExplorerState.SetSelectedFolders([AssetPath.Create(added.Name)]);
        }
    }

    [RelayCommand(CanExecute = nameof(CanChangeMounts))]
    private async Task ContentPriorityAsync()
    {
        var expected = session.Context;
        var candidate = this.GetActiveProjectInfo();
        var model = new ContentPriorityViewModel(expected);
        var spec = new DialogSpec("Content priority", new ContentPriorityView { ViewModel = model })
        {
            PrimaryButtonText = "Apply",
            SecondaryButtonText = "Cancel",
            CloseButtonText = string.Empty,
            DefaultButton = DialogButton.Primary,
        };
        if (await dialogService.ShowAsync(spec, session.Lifetime).ConfigureAwait(true) == DialogButton.Primary)
        {
            candidate.CookedContentOrder = [.. model.GetMountOrder()];
            await this.ApplyMountCandidateAsync(expected, candidate, MountBindings.None).ConfigureAwait(true);
        }
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "The UI operation boundary presents validation, persistence and native refresh failures and restores the accepted tree.")]
    private async Task ApplyMountCandidateAsync(ProjectContext expected, ProjectInfo candidate, MountBindings bindings)
    {
        if (this.IsApplyingMounts)
        {
            return;
        }

        this.IsApplyingMounts = true;
        try
        {
            candidate.CookedContentOrder = [.. CookedContentOrdering.Resolve(candidate)];
            CookedContentOrdering.DeclareInOrder(candidate);
            CookedContentOrdering.DeclareInOrder(candidate);
            if (!await mountChanges.ChangeMountsAsync(expected, candidate, bindings, session.Lifetime).ConfigureAwait(true))
            {
                throw new InvalidOperationException("Another project became active before this content mount change could be shown.");
            }

            this.ReconcileSelectedMounts(expected, candidate);
        }
        catch (Exception exception)
        {
            this.LogMountChangeFailed(exception);
            if (session.Context.ProjectId == expected.ProjectId)
            {
                _ = await this.ShowMountUpdateFailureAsync(exception.Message).ConfigureAwait(true);
            }
        }
        finally
        {
            try
            {
                if (session.Context.ProjectId == expected.ProjectId)
                {
                    await this.ReloadMountTreeAsync().ConfigureAwait(true);
                }
            }
            finally
            {
                this.IsApplyingMounts = false;
            }
        }
    }

    private Task ReloadMountTreeAsync()
    {
        this.treeReloadVersion++;
        if (this.selectionDisposed)
        {
            return Task.CompletedTask;
        }

        if (this.treeReload is { } pending)
        {
            return pending.Task;
        }

        var completion = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        this.treeReload = completion;
        _ = this.ReloadMountTreeCoreAsync(completion);
        return completion.Task;
    }

    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "Background tree refresh keeps the prior usable tree and reports preparation failures.")]
    private async Task ReloadMountTreeCoreAsync(TaskCompletionSource completion)
    {
        try
        {
            while (!this.selectionDisposed)
            {
                var context = session.Context;
                var version = this.treeReloadVersion;
                try
                {
                    if (await this.TryReloadMountTreeAsync(context, version).ConfigureAwait(true))
                    {
                        return;
                    }
                }
                catch (Exception failure)
                {
                    if (this.IsTreeBuildCurrent(context, version))
                    {
                        this.LogPreloadingProjectFoldersError(failure);
                        return;
                    }
                }
            }
        }
        finally
        {
            this.treeReload = null;
            _ = completion.TrySetResult();
        }
    }

    // Builds the tree of one project state and installs it; false when the state changed meanwhile.
    private async Task<bool> TryReloadMountTreeAsync(ProjectContext context, long version)
    {
        if (await this.BuildMountTreeAsync(context, version).ConfigureAwait(true) is not { } tree)
        {
            return false;
        }

        await this.InstallMountTreeAsync(tree).ConfigureAwait(true);
        return this.IsTreeBuildCurrent(context, version);
    }

    // The tree with its expanded folders read, or null when the state changed while it was read.
    private async Task<ProjectRootTreeItemAdapter?> BuildMountTreeAsync(ProjectContext context, long version)
    {
        var folder = await storage.GetFolderFromPathAsync(this.GetActiveProjectInfo().Location!, session.Lifetime).ConfigureAwait(true);
        if (!this.IsTreeBuildCurrent(context, version))
        {
            return null;
        }

        ProjectRootTreeItemAdapter? candidate = null;
        try
        {
            candidate = new(this.logger, storage, context, folder) { IsExpanded = true };
            await this.LoadPersistedMountsAsync(candidate, context).ConfigureAwait(true);
            await this.PrepareExpandedFoldersAsync(candidate, context, version).ConfigureAwait(true);
            if (!this.IsTreeBuildCurrent(context, version))
            {
                return null;
            }

            var tree = candidate;
            candidate = null;
            return tree;
        }
        finally
        {
            candidate?.Dispose();
        }
    }

    // Replaces the shown tree; expanded children are ready, so installation does not interleave filesystem reads.
    private async Task InstallMountTreeAsync(ProjectRootTreeItemAdapter tree)
    {
        var previous = this.projectRoot;
        this.projectRoot = tree;
        this.suppressTreeSelectionEvents = true;
        try
        {
            await this.InitializeRootAsync(tree, skipRoot: false).ConfigureAwait(true);
        }
        finally
        {
            previous?.Dispose();
            this.suppressTreeSelectionEvents = false;
        }

        await this.UpdateTreeSelectionFromStateAsync().ConfigureAwait(true);
    }

    private bool IsTreeBuildCurrent(ProjectContext context, long version)
        => !this.selectionDisposed && version == this.treeReloadVersion && ReferenceEquals(context, session.Context);

    private async Task PrepareExpandedFoldersAsync(ITreeItem item, ProjectContext context, long version)
    {
        if (!item.IsExpanded || !this.IsTreeBuildCurrent(context, version))
        {
            return;
        }

        foreach (var child in await item.Children.ConfigureAwait(true))
        {
            if (!this.IsTreeBuildCurrent(context, version))
            {
                return;
            }

            await this.PrepareExpandedFoldersAsync(child, context, version).ConfigureAwait(true);
        }
    }

    // Selected folders of a mount that is gone leave the whole project shown.
    private void ReconcileSelectedMounts(ProjectContext previous, ProjectInfo candidate)
    {
        var gone = previous.Mounts.Select(static mount => mount.Name).Except(candidate.Mounts.Select(static mount => mount.Name), StringComparer.Ordinal).ToHashSet(StringComparer.Ordinal);
        if (contentExplorerState.SelectedFolders.Any(folder => gone.Contains(folder.Mount)))
        {
            contentExplorerState.SetSelectedFolders([]);
        }
    }
}
