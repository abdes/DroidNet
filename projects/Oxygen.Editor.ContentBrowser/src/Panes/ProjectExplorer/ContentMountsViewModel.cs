// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Aura.Dialogs;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.ContentBrowser.ProjectExplorer;

/// <summary>
/// The Content mounts dialog: every mount with its virtual name, backing path and kind, with rename and unmount,
/// and a form that adds Cooked, Imported, Build or a local folder. Edits are staged and applied together.
/// </summary>
public sealed partial class ContentMountsViewModel : ObservableObject
{
    /// <summary>The add-form choice for a folder anywhere on disk.</summary>
    public const string LocalFolderKind = "Local folder";

    private static readonly (string Kind, string Path)[] KnownLocations = [("Cooked", ".cooked"), ("Imported", ".imported"), ("Build", ".build")];

    private readonly IDialogService dialogService;

    /// <summary>Initializes a new instance of the <see cref="ContentMountsViewModel"/> class.</summary>
    /// <param name="project">The project whose mounts are edited.</param>
    /// <param name="dialogService">The folder picker.</param>
    public ContentMountsViewModel(ProjectContext project, IDialogService dialogService)
    {
        ArgumentNullException.ThrowIfNull(project);
        this.dialogService = dialogService;
        var content = project.AuthoringMounts.FirstOrDefault(static mount => !ProjectLayoutViewModel.IsPersistedProjectRelativeVirtualMount(mount));
        foreach (var mount in project.AuthoringMounts)
        {
            var derived = ProjectLayoutViewModel.IsPersistedProjectRelativeVirtualMount(mount);
            this.Mounts.Add(new(this, mount.Name, "./" + mount.RelativePath.Replace('\\', '/').Trim('/'), derived ? ContentMountKind.Derived : ContentMountKind.ProjectSource)
            {
                RelativePath = mount.RelativePath,
                IsExpanded = mount.IsExpanded,
                IsPrimary = ReferenceEquals(mount, content),
            });
        }

        foreach (var mount in project.LocalFolderMounts)
        {
            this.Mounts.Add(new(this, mount.Name, mount.AbsolutePath, ContentMountKind.LocalLibrary)
            {
                AbsolutePath = mount.AbsolutePath,
                IsExpanded = mount.IsExpanded,
            });
        }

        this.SelectedKind = this.Kinds[0];
    }

    /// <summary>Gets the mounts as they will be after Apply.</summary>
    public ObservableCollection<ContentMountRow> Mounts { get; } = [];

    /// <summary>Gets the locations the add form offers.</summary>
    public IReadOnlyList<string> Kinds { get; } = [LocalFolderKind, .. KnownLocations.Select(static location => location.Kind)];

    /// <summary>Gets or sets the location to add.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(IsLocalFolder))]
    public partial string SelectedKind { get; set; }

    /// <summary>Gets a value indicating whether the add form needs a name and a folder.</summary>
    public bool IsLocalFolder => string.Equals(this.SelectedKind, LocalFolderKind, StringComparison.Ordinal);

    /// <summary>Gets or sets the virtual name of a local folder to add.</summary>
    [ObservableProperty]
    public partial string NewMountName { get; set; } = string.Empty;

    /// <summary>Gets or sets the folder to add.</summary>
    [ObservableProperty]
    public partial string NewMountPath { get; set; } = string.Empty;

    /// <summary>Gets the reason the last add or rename was refused.</summary>
    [ObservableProperty]
    public partial string ErrorMessage { get; set; } = string.Empty;

    /// <summary>Gets a value indicating whether there is an error to show.</summary>
    public bool HasError => this.ErrorMessage.Length != 0;

    /// <summary>Checks a proposed mount name: present, without separators, and unique among the remaining mounts.</summary>
    /// <param name="name">The proposed name.</param>
    /// <param name="except">The mount being renamed, if any.</param>
    /// <returns>The reason the name is refused, or null.</returns>
    public string? ValidateName(string name, ContentMountRow? except = null)
    {
        var trimmed = name.Trim();
        if (trimmed.Length == 0)
        {
            return "Enter a mount name.";
        }

        if (trimmed.IndexOfAny(['/', '\\', ':']) >= 0)
        {
            return "A mount name cannot contain slashes or a colon.";
        }

        return this.Mounts.Any(row => !ReferenceEquals(row, except) && string.Equals(row.Name, trimmed, StringComparison.OrdinalIgnoreCase))
            ? "A mount with this name already exists."
            : null;
    }

    /// <summary>Accepts the dialog only when no rename is left unfinished.</summary>
    /// <returns>Whether the staged mounts can be applied.</returns>
    public bool Validate()
    {
        foreach (var row in this.Mounts.Where(static row => row.IsRenaming))
        {
            if (!row.TryCommitRename())
            {
                return false;
            }
        }

        return true;
    }

    /// <summary>Removes a mount from the staged list; its files are not touched.</summary>
    /// <param name="row">The mount to remove.</param>
    internal void Remove(ContentMountRow row)
    {
        _ = this.Mounts.Remove(row);
        this.ErrorMessage = string.Empty;
    }

    partial void OnErrorMessageChanged(string value) => this.OnPropertyChanged(nameof(this.HasError));

    [RelayCommand]
    private void AddMount()
    {
        if (!this.IsLocalFolder)
        {
            var (kind, path) = KnownLocations.Single(location => string.Equals(location.Kind, this.SelectedKind, StringComparison.Ordinal));
            if (this.Mounts.Any(row => row.Kind == ContentMountKind.Derived && string.Equals(row.RelativePath?.Trim('/', '\\'), path, StringComparison.OrdinalIgnoreCase)))
            {
                this.ErrorMessage = $"{kind} is already mounted.";
                return;
            }

            if (this.ValidateName(kind) is { } conflict)
            {
                this.ErrorMessage = conflict;
                return;
            }

            this.Mounts.Add(new(this, kind, "./" + path, ContentMountKind.Derived) { RelativePath = path, IsNew = true });
            this.ErrorMessage = string.Empty;
            return;
        }

        var name = this.NewMountName.Trim();
        if (this.ValidateName(name) is { } invalid)
        {
            this.ErrorMessage = invalid;
            return;
        }

        if (string.IsNullOrWhiteSpace(this.NewMountPath) || !Directory.Exists(this.NewMountPath))
        {
            this.ErrorMessage = "Choose a folder that exists.";
            return;
        }

        var folder = Path.GetFullPath(this.NewMountPath);
        this.Mounts.Add(new(this, name, folder, ContentMountKind.LocalLibrary) { AbsolutePath = folder, IsNew = true });
        this.NewMountName = string.Empty;
        this.NewMountPath = string.Empty;
        this.ErrorMessage = string.Empty;
    }

    [RelayCommand]
    private async Task BrowseAsync(CancellationToken cancellationToken)
    {
        try
        {
            var picker = new FolderPickerSpec("Choose a folder to mount", "Oxygen.LocalFolderMount")
            {
                SuggestedStartFolder = this.NewMountPath,
            };
            if (await this.dialogService.PickFolderAsync(picker, cancellationToken).ConfigureAwait(true) is { Length: > 0 } folder)
            {
                this.NewMountPath = folder;
                if (this.NewMountName.Trim().Length == 0)
                {
                    this.NewMountName = Path.GetFileName(folder.TrimEnd('\\', '/'));
                }
            }
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            // The dialog closed while the picker was open.
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or ArgumentException
            or InvalidOperationException or System.Runtime.InteropServices.COMException)
        {
            this.ErrorMessage = $"Could not pick a folder: {error.Message}";
        }
    }
}
