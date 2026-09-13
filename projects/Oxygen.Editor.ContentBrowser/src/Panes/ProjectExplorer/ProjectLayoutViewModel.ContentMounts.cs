// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Input;
using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Aura.Dialogs;
using Microsoft.Extensions.Logging;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.ContentBrowser.Messages;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;

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
                this.OnPropertyChanged(nameof(this.CanUnmountSelectedItem));
                this.OnPropertyChanged(nameof(this.CanRenameSelectedItem));
                this.OnPropertyChanged(nameof(this.MountStatusText));
                this.OnPropertyChanged(nameof(this.MountProgressVisibility));
                this.MountKnownLocationCommand.NotifyCanExecuteChanged();
                this.MountLocalFolderCommand.NotifyCanExecuteChanged();
                this.ContentPriorityCommand.NotifyCanExecuteChanged();
                this.UnmountSelectedItemCommand.NotifyCanExecuteChanged();
                this.RenameSelectedItemCommand.NotifyCanExecuteChanged();
            }
        }
    }

    /// <summary>Gets a value indicating whether mount commands can start.</summary>
    public bool CanChangeMounts => !this.IsApplyingMounts;

    /// <summary>Gets the compact operation feedback shown in the mount toolbar.</summary>
    public string MountStatusText => this.IsApplyingMounts ? "Updating…" : "Virtual folders";

    /// <summary>Gets progress visibility without reserving idle toolbar space.</summary>
    public Visibility MountProgressVisibility => this.IsApplyingMounts ? Visibility.Visible : Visibility.Collapsed;

    private static void RemapPriorityNames(ProjectContext previous, ProjectInfo candidate)
        => candidate.CookedContentOrder = previous.CookedContentOrder.Select(source =>
        {
            if (source.Kind == CookedContentSourceKind.ProjectOutput || candidate.LocalFolderMounts.Any(mount => string.Equals(mount.Name, source.Name, StringComparison.Ordinal)))
            {
                return source;
            }

            var old = previous.LocalFolderMounts.FirstOrDefault(mount => string.Equals(mount.Name, source.Name, StringComparison.Ordinal));
            var renamed = old is null ? null : candidate.LocalFolderMounts.FirstOrDefault(mount =>
                string.Equals(mount.AbsolutePath, old.AbsolutePath, StringComparison.OrdinalIgnoreCase)
                && !previous.LocalFolderMounts.Any(original => string.Equals(original.Name, mount.Name, StringComparison.Ordinal)));
            return renamed is null ? null : new CookedContentSource(CookedContentSourceKind.LocalFolder, renamed.Name);
        }).OfType<CookedContentSource>().ToList();

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
            PrimaryButtonText = "Apply", SecondaryButtonText = "Cancel", CloseButtonText = string.Empty, DefaultButton = DialogButton.Primary,
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
            candidate.CookedContentOrder = CookedContentOrdering.Resolve(candidate.LocalFolderMounts, candidate.CookedContentOrder).ToList();
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
                    PrimaryButtonText = "Close", SecondaryButtonText = string.Empty, CloseButtonText = string.Empty, DefaultButton = DialogButton.Primary,
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

    private async Task ReloadMountTreeAsync()
    {
        var previous = this.projectRoot;
        previous?.MountRenamed -= this.OnMountRenamed;

        this.projectRoot = null;
        this.suppressTreeSelectionEvents = true;
        try
        {
            await this.PreloadRecentTemplatesAsync().ConfigureAwait(true);
            await this.UpdateTreeSelectionFromStateAsync().ConfigureAwait(true);
        }
        finally
        {
            previous?.Dispose();
            this.suppressTreeSelectionEvents = false;
        }
    }

    private void ReconcileSelectedMounts(ProjectContext previous, ProjectInfo candidate)
    {
        var before = previous.AuthoringMounts.Select(mount => (mount.Name, Path: Path.GetFullPath(Path.Combine(previous.ProjectRoot, mount.RelativePath))))
            .Concat(previous.LocalFolderMounts.Select(static mount => (mount.Name, Path: Path.GetFullPath(mount.AbsolutePath)))).ToArray();
        var after = candidate.AuthoringMounts.Select(mount => (mount.Name, Path: Path.GetFullPath(Path.Combine(previous.ProjectRoot, mount.RelativePath))))
            .Concat(candidate.LocalFolderMounts.Select(static mount => (mount.Name, Path: Path.GetFullPath(mount.AbsolutePath)))).ToArray();
        var folders = contentBrowserState.SelectedFolders.Select(folder =>
        {
            var path = folder.TrimStart('/');
            var split = path.IndexOf('/', StringComparison.Ordinal);
            var name = split < 0 ? path : path[..split];
            if (after.Any(mount => string.Equals(mount.Name, name, StringComparison.Ordinal)))
            {
                return folder;
            }

            var old = before.FirstOrDefault(mount => string.Equals(mount.Name, name, StringComparison.Ordinal));
            if (old.Name is null)
            {
                return folder;
            }

            var replacement = after.FirstOrDefault(mount => string.Equals(mount.Path, old.Path, StringComparison.OrdinalIgnoreCase));
            return replacement.Name is null ? "/" : "/" + replacement.Name + (split < 0 ? string.Empty : path[split..]);
        }).Distinct(StringComparer.Ordinal).ToArray();
        contentBrowserState.SetSelectedFolders(folders);
    }

    [LoggerMessage(Level = LogLevel.Error, Message = "Content mount update failed.")]
    private partial void LogMountChangeFailed(Exception exception);
}
