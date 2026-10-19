// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using CommunityToolkit.Mvvm.Input;
using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Aura.Dialogs;
using DroidNet.Controls;
using Microsoft.Extensions.Logging;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.ContentBrowser.Messages;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentBrowser.ProjectExplorer;

/// <summary>Applies confirmed mount changes without a separate save step.</summary>
public partial class ProjectLayoutViewModel
{
    private bool isApplyingMounts;

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
        candidate.AuthoringMounts.Clear();
        candidate.LocalFolderMounts.Clear();
        candidate.PakFileMounts.Clear();
        foreach (var row in rows)
        {
            switch (row.Kind)
            {
                case ContentMountKind.EngineContent:
                    // Always mounted from the SDK; the project does not declare it.
                    continue;
                case ContentMountKind.PakLibrary:
                    candidate.PakFileMounts.Add(new(row.Name, FileSystemPath.From(row.AbsolutePath!), row.IsExpanded));
                    continue;
                case ContentMountKind.ProjectSource or ContentMountKind.Derived:
                    candidate.AuthoringMounts.Add(new(row.Name, row.RelativePath!, row.IsExpanded));
                    continue;
                case ContentMountKind.LocalLibrary:
                    break;
                default:
                    throw new InvalidOperationException("The Content mounts dialog staged an unknown mount kind.");
            }

            // A new authored folder inside the project is a project source; anything else is a read-only library.
            var folder = FileSystemPath.From(row.AbsolutePath!);
            var relative = row.IsNew ? GetRelativeMountPath(expected.ProjectRoot, folder) : null;
            if (relative is not null && !isCookedLibrary(row.AbsolutePath!))
            {
                candidate.AuthoringMounts.Add(new(row.Name, relative, row.IsExpanded));
            }
            else
            {
                candidate.LocalFolderMounts.Add(new(row.Name, folder, row.IsExpanded));
            }
        }

        RemapPriorityNames(expected, candidate);
    }

    // A library keeps its priority across a rename: its container is still declared, under a name that is new.
    private static void RemapPriorityNames(ProjectContext previous, ProjectInfo candidate)
    {
        var before = DeclaredLibrary.Of(previous.LocalFolderMounts, previous.PakFileMounts);
        var after = DeclaredLibrary.Of(candidate.LocalFolderMounts, candidate.PakFileMounts);
        candidate.CookedContentOrder = previous.CookedContentOrder.Select(source =>
        {
            if (source.Kind == CookedContentSourceKind.ProjectOutput || after.Exists(library => library.Is(source)))
            {
                return source;
            }

            var old = before.Find(library => library.Is(source));
            var renamed = old is null ? null : after.Find(library => library.Kind == old.Kind && library.Container == old.Container
                && !before.Exists(original => string.Equals(original.Name, library.Name, StringComparison.Ordinal)));
            return renamed is null ? null : new CookedContentSource(renamed.Kind, renamed.Name);
        }).OfType<CookedContentSource>().ToList();
    }

    [RelayCommand(CanExecute = nameof(CanChangeMounts))]
    private async Task ManageMountsAsync()
    {
        if (projectContextService.ActiveProject is not { } expected || this.GetActiveProjectInfo() is not { } candidate)
        {
            return;
        }

        var model = new ContentMountsViewModel(expected, dialogService, engineContent);
        var spec = new DialogSpec("Content mounts", new ContentMountsView { ViewModel = model })
        {
            PrimaryButtonText = "Apply",
            SecondaryButtonText = "Cancel",
            CloseButtonText = string.Empty,
            DefaultButton = DialogButton.Primary,
            PrimaryAction = () => Task.FromResult(model.Validate()),
            MaxWidth = 760,
        };
        if (await dialogService.ShowAsync(spec).ConfigureAwait(true) != DialogButton.Primary)
        {
            return;
        }

        ApplyMountRows(expected, candidate, model.Mounts, static folder => File.Exists(Path.Combine(folder, "container.index.bin")));
        if (candidate.AuthoringMounts.SequenceEqual(expected.AuthoringMounts) && candidate.LocalFolderMounts.SequenceEqual(expected.LocalFolderMounts)
            && candidate.PakFileMounts.SequenceEqual(expected.PakFileMounts))
        {
            return;
        }

        await this.ApplyMountCandidateAsync(expected, candidate).ConfigureAwait(true);

        // Show what was just mounted.
        if (model.Mounts.LastOrDefault(static row => row.IsNew) is { } added
            && projectContextService.ActiveProject is { } applied
            && (applied.AuthoringMounts.Any(mount => string.Equals(mount.Name, added.Name, StringComparison.Ordinal))
                || applied.LocalFolderMounts.Any(mount => string.Equals(mount.Name, added.Name, StringComparison.Ordinal))
                || applied.PakFileMounts.Any(mount => string.Equals(mount.Name, added.Name, StringComparison.Ordinal))))
        {
            contentBrowserState.SetSelectedFolders([AssetPath.Create(added.Name)]);
        }
    }

    [RelayCommand(CanExecute = nameof(CanChangeMounts))]
    private async Task ContentPriorityAsync()
    {
        if (projectContextService.ActiveProject is not { } expected || this.GetActiveProjectInfo() is not { } candidate)
        {
            return;
        }

        var model = new ContentPriorityViewModel(expected);
        var spec = new DialogSpec("Content priority", new ContentPriorityView { ViewModel = model })
        {
            PrimaryButtonText = "Apply",
            SecondaryButtonText = "Cancel",
            CloseButtonText = string.Empty,
            DefaultButton = DialogButton.Primary,
        };
        if (await dialogService.ShowAsync(spec).ConfigureAwait(true) == DialogButton.Primary)
        {
            candidate.CookedContentOrder = model.GetMountOrder().ToList();
            await this.ApplyMountCandidateAsync(expected, candidate).ConfigureAwait(true);
        }
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "The UI operation boundary presents validation, persistence and native refresh failures and restores the accepted tree.")]
    private async Task ApplyMountCandidateAsync(ProjectContext expected, ProjectInfo candidate)
    {
        if (this.IsApplyingMounts)
        {
            return;
        }

        this.IsApplyingMounts = true;
        try
        {
            candidate.CookedContentOrder = CookedContentOrdering.Resolve(candidate).ToList();
            var request = this.messenger.Send(new ChangeContentMountsRequestMessage(expected, candidate));
            if (!request.HasReceivedResponse || !await request.Response.ConfigureAwait(true))
            {
                throw new InvalidOperationException("The workspace could not apply this content mount change.");
            }

            this.ReconcileSelectedMounts(expected, candidate);
            this.HasUnsavedChanges = false;
        }
        catch (Exception exception)
        {
            this.LogMountChangeFailed(exception);
            if (projectContextService.ActiveProject?.ProjectId == expected.ProjectId)
            {
                var spec = new DialogSpec("Content mount update", new TextBlock { Text = exception.Message, TextWrapping = TextWrapping.Wrap, MaxWidth = 520, IsTextSelectionEnabled = true })
                {
                    PrimaryButtonText = "Close",
                    SecondaryButtonText = string.Empty,
                    CloseButtonText = string.Empty,
                    DefaultButton = DialogButton.Primary,
                };
                _ = await dialogService.ShowAsync(spec).ConfigureAwait(true);
            }
        }
        finally
        {
            try
            {
                if (projectContextService.ActiveProject?.ProjectId == expected.ProjectId)
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

    private TaskCompletionSource? treeReload;
    private long treeReloadVersion;

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
            while (!this.selectionDisposed && projectContextService.ActiveProject is { } context)
            {
                var version = this.treeReloadVersion;
                try
                {
                    var projectInfo = this.GetActiveProjectInfo()!;
                    var folder = await storage.GetFolderFromPathAsync(projectInfo.Location!).ConfigureAwait(true);
                    if (!this.IsTreeBuildCurrent(context, version))
                    {
                        continue;
                    }

                    ProjectRootTreeItemAdapter? candidate = new(this.logger, storage, projectInfo, folder) { IsExpanded = true };
                    try
                    {
                        await this.LoadPersistedMountsAsync(candidate, projectInfo, context).ConfigureAwait(true);
                        await this.PrepareExpandedFoldersAsync(candidate, context, version).ConfigureAwait(true);
                        if (!this.IsTreeBuildCurrent(context, version))
                        {
                            continue;
                        }

                        var previous = this.projectRoot;
                        previous?.MountRenamed -= this.OnMountRenamed;

                        this.projectRoot = candidate;
                        candidate = null;
                        this.projectRoot.MountRenamed += this.OnMountRenamed;
                        this.suppressTreeSelectionEvents = true;
                        try
                        {
                            // Expanded children are ready, so installation does not interleave filesystem reads.
                            await this.InitializeRootAsync(this.projectRoot, skipRoot: false).ConfigureAwait(true);
                            foreach (var child in await this.projectRoot.Children.ConfigureAwait(true))
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
                        if (this.IsTreeBuildCurrent(context, version))
                        {
                            return;
                        }
                    }
                    finally
                    {
                        candidate?.Dispose();
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
            completion.TrySetResult();
        }
    }

    private bool IsTreeBuildCurrent(ProjectContext context, long version)
        => !this.selectionDisposed && version == this.treeReloadVersion && ReferenceEquals(context, projectContextService.ActiveProject);

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
        var before = previous.AuthoringMounts.Select(mount => (mount.Name, Root: previous.ProjectRoot.Combine(mount.RelativePath)))
            .Concat(previous.LocalFolderMounts.Select(static mount => (mount.Name, mount.Root)))
            .Concat(previous.PakFileMounts.Select(static mount => (mount.Name, Root: mount.Path))).ToArray();
        var after = candidate.AuthoringMounts.Select(mount => (mount.Name, Root: previous.ProjectRoot.Combine(mount.RelativePath)))
            .Concat(candidate.LocalFolderMounts.Select(static mount => (mount.Name, mount.Root)))
            .Concat(candidate.PakFileMounts.Select(static mount => (mount.Name, Root: mount.Path))).ToArray();

        // A renamed mount keeps its folders selected under its new name; a removed one leaves the whole project shown.
        var folders = contentBrowserState.SelectedFolders.Select(folder =>
        {
            if (after.Any(mount => string.Equals(mount.Name, folder.Mount, StringComparison.Ordinal)))
            {
                return folder;
            }

            var old = before.FirstOrDefault(mount => string.Equals(mount.Name, folder.Mount, StringComparison.Ordinal));
            if (old.Name is null)
            {
                return folder;
            }

            var replacement = after.FirstOrDefault(mount => mount.Root == old.Root);
            return replacement.Name is null ? null : folder.IsMount ? AssetPath.Create(replacement.Name) : AssetPath.Create(replacement.Name, folder.RelativePath);
        }).ToArray();
        contentBrowserState.SetSelectedFolders(folders.Any(static folder => folder is null) ? [] : folders.OfType<AssetPath>());
    }

    [LoggerMessage(Level = LogLevel.Error, Message = "Content mount update failed.")]
    private partial void LogMountChangeFailed(Exception exception);

    /// <summary>A declared library: a local folder or a pak file, with the container that identifies it across renames.</summary>
    /// <param name="Kind">The library's kind in the content priority.</param>
    /// <param name="Name">The library's mount name.</param>
    /// <param name="Container">The folder or the pak file.</param>
    private sealed record DeclaredLibrary(CookedContentSourceKind Kind, string Name, FileSystemPath Container)
    {
        public static List<DeclaredLibrary> Of(IEnumerable<LocalFolderMount> folders, IEnumerable<PakFileMount> paks)
            => [.. folders.Select(static folder => new DeclaredLibrary(CookedContentSourceKind.LocalFolder, folder.Name, folder.Root)),
                .. paks.Select(static pak => new DeclaredLibrary(CookedContentSourceKind.PakFile, pak.Name, pak.Path))];

        public bool Is(CookedContentSource source) => this.Kind == source.Kind && string.Equals(this.Name, source.Name, StringComparison.Ordinal);
    }
}
