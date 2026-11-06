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
using Oxygen.Managed.Assets.Persistence.LooseCooked;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentExplorer.Panes.ContentFolders;

/// <summary>Applies confirmed mount changes without a separate save step.</summary>
public partial class ContentFoldersViewModel
{
    private bool isApplyingMounts;
    private TaskCompletionSource? treeReload;
    private long treeReloadVersion;

    /// <summary>Gets completion of a rename-triggered mount update.</summary>
    public Task PendingMountChange { get; private set; } = Task.CompletedTask;

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
    /// <param name="isCookedLibrary">Tells whether a folder holds cooked content rather than authored sources.</param>
    internal static void ApplyMountRows(ProjectContext expected, ProjectInfo candidate, IReadOnlyList<ContentMountRow> rows, Func<string, bool> isCookedLibrary)
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
                case ContentMountKind.PakLibrary:
                    AddDeclaredMount(candidate, row, MountKind.Cooked);
                    continue;
                case ContentMountKind.ProjectSource:
                    AddDeclaredMount(candidate, row, MountKind.Source);
                    continue;
                case ContentMountKind.Derived:
                    candidate.OutputFolderViews.Add(new(row.RelativePath!, row.IsExpanded));
                    continue;
                case ContentMountKind.LocalLibrary:
                    // A new folder with a cooked index is a read-only library; any other new folder is a project source.
                    AddDeclaredMount(candidate, row, row.IsNew && !isCookedLibrary(row.AbsolutePath!) ? MountKind.Source : MountKind.Cooked);
                    continue;
                default:
                    throw new InvalidOperationException("The Content mounts dialog staged an unknown mount kind.");
            }
        }

        RemapPriorityNames(expected, candidate);
    }

    private static void AddDeclaredMount(ProjectInfo candidate, ContentMountRow row, MountKind kind)
    {
        // A mount only a .mounts.json adds stays out of Project.oxy; a placeholder commits no path.
        if (!row.IsLocal)
        {
            candidate.Mounts.Add(new(row.Name, kind, row.RelativePath, row.IsExpanded));
        }
    }

    // A library keeps its priority across a rename: its container is still declared, under a name that is new.
    private static void RemapPriorityNames(ProjectContext previous, ProjectInfo candidate)
    {
        var before = DeclaredLibrary.Of(previous.DeclaredMounts);
        var after = DeclaredLibrary.Of(candidate.Mounts);
        candidate.CookedContentOrder = [.. previous.CookedContentOrder.Select(source =>
        {
            if (source.Kind == CookedContentSourceKind.ProjectOutput || after.Exists(library => library.Is(source)))
            {
                return source;
            }

            var old = before.Find(library => library.Is(source));
            var renamed = old?.Container is null ? null : after.Find(library => library.Kind == old.Kind && string.Equals(library.Container, old.Container, StringComparison.Ordinal) && !before.Exists(original => string.Equals(original.Name, library.Name, StringComparison.Ordinal)));
            return renamed is null ? null : new CookedContentSource(renamed.Kind, renamed.Name);
        }).OfType<CookedContentSource>()];
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

        ApplyMountRows(expected, candidate, model.Mounts, folder => expected.FileSystem.File.Exists(Path.Combine(folder, LooseCookedIndex.FileName)));
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
            if (!await mountChanges.ChangeMountsAsync(expected, candidate, bindings, session.Lifetime).ConfigureAwait(true))
            {
                throw new InvalidOperationException("Another project became active before this content mount change could be shown.");
            }

            this.ReconcileSelectedMounts(expected, candidate);
            this.HasUnsavedChanges = false;
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
                    this.HasUnsavedChanges = false;
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
        previous?.MountRenamed -= this.OnMountRenamed;
        this.projectRoot = tree;
        tree.MountRenamed += this.OnMountRenamed;
        this.suppressTreeSelectionEvents = true;
        try
        {
            await this.InitializeRootAsync(tree, skipRoot: false).ConfigureAwait(true);
            foreach (var child in await tree.Children.ConfigureAwait(true))
            {
                if (child is AuthoringMountPointTreeItemAdapter authoring)
                {
                    authoring.PropertyChanged += this.OnMountPointPropertyChanged;
                }
            }
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

    private void ReconcileSelectedMounts(ProjectContext previous, ProjectInfo candidate)
    {
        var before = previous.Mounts.Select(static mount => (mount.Name, Root: mount.Location)).ToArray();
        var after = candidate.Mounts.Select(mount => (mount.Name, Root: mount.Path is null ? null : previous.ProjectRoot.Combine(mount.Path))).ToArray();

        // A renamed mount keeps its folders selected under its new name; a removed one leaves the whole project shown.
        var selected = contentExplorerState.SelectedFolders.Select(folder =>
        {
            if (after.Any(mount => string.Equals(mount.Name, folder.Mount, StringComparison.Ordinal)))
            {
                return folder;
            }

            var (oldName, oldRoot) = before.FirstOrDefault(mount => string.Equals(mount.Name, folder.Mount, StringComparison.Ordinal));
            if (oldName is null)
            {
                return folder;
            }

            var (newName, _) = oldRoot is null ? default : after.FirstOrDefault(mount => mount.Root == oldRoot);
            return newName is null ? null : folder.IsMount ? AssetPath.Create(newName) : AssetPath.Create(newName, folder.RelativePath);
        }).ToArray();
        contentExplorerState.SetSelectedFolders(selected.Any(static folder => folder is null) ? [] : selected.OfType<AssetPath>());
    }

    /// <summary>A declared library: a cooked folder or a pak file, with the committed path that identifies it across renames.</summary>
    /// <param name="Kind">The library's kind in the content priority.</param>
    /// <param name="Name">The library's mount name.</param>
    /// <param name="Container">The committed path of the folder or the pak file, or <see langword="null"/> for a placeholder.</param>
    private sealed record DeclaredLibrary(CookedContentSourceKind Kind, string Name, string? Container)
    {
        public static List<DeclaredLibrary> Of(IEnumerable<ProjectMount> mounts)
            => [.. mounts.Where(static mount => mount.Kind == MountKind.Cooked)
                .Select(static mount => new DeclaredLibrary(CookedContentSourceKind.Cooked, mount.Name, mount.Path))];

        public bool Is(CookedContentSource source) => this.Kind == source.Kind && string.Equals(this.Name, source.Name, StringComparison.Ordinal);
    }
}
