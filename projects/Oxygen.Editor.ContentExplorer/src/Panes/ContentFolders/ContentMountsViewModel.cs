// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Aura.Dialogs;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentExplorer.Panes.ContentFolders;

/// <summary>
/// The Content mounts dialog: every mount with its virtual name, backing path and kind, with rename and unmount,
/// and a form that adds Cooked, Imported, Build, a local folder or a pak file. The engine content is listed last, always
/// mounted. Edits are staged and applied together.
/// </summary>
public sealed partial class ContentMountsViewModel : ObservableObject
{
    /// <summary>The add-form choice for a folder anywhere on disk.</summary>
    public const string LocalFolderKind = "Local folder";

    /// <summary>The add-form choice for a pak file anywhere on disk.</summary>
    public const string PakFileKind = "Pak file";

    private static readonly (string Kind, string Path)[] KnownLocations = [("Cooked", ".cooked"), ("Imported", ".imported"), ("Build", ".build")];

    private readonly IDialogService dialogService;

    /// <summary>Initializes a new instance of the <see cref="ContentMountsViewModel"/> class.</summary>
    /// <param name="project">The project whose mounts are edited.</param>
    /// <param name="dialogService">The folder and file pickers.</param>
    /// <param name="engineContent">The engine content pak, listed as the fixed lowest mount.</param>
    public ContentMountsViewModel(ProjectContext project, IDialogService dialogService, IEngineContentPak engineContent)
    {
        ArgumentNullException.ThrowIfNull(project);
        ArgumentNullException.ThrowIfNull(engineContent);
        this.dialogService = dialogService;
        var content = project.AuthoringMounts.FirstOrDefault(static mount => !ContentFoldersViewModel.IsPersistedProjectRelativeVirtualMount(mount));
        foreach (var mount in project.AuthoringMounts)
        {
            var derived = ContentFoldersViewModel.IsPersistedProjectRelativeVirtualMount(mount);
            this.Mounts.Add(new(this, mount.Name, "./" + mount.RelativePath.Replace('\\', '/').Trim('/'), derived ? ContentMountKind.Derived : ContentMountKind.ProjectSource)
            {
                RelativePath = mount.RelativePath,
                IsExpanded = mount.IsExpanded,
                IsPrimary = ReferenceEquals(mount, content),
            });
        }

        foreach (var mount in project.LocalFolderMounts)
        {
            this.Mounts.Add(new(this, mount.Name, mount.Root.FullPath, ContentMountKind.LocalLibrary)
            {
                AbsolutePath = mount.Root.FullPath,
                IsExpanded = mount.IsExpanded,
            });
        }

        foreach (var mount in project.PakFileMounts)
        {
            this.Mounts.Add(new(this, mount.Name, mount.Path.FullPath, ContentMountKind.PakLibrary)
            {
                AbsolutePath = mount.Path.FullPath,
                IsExpanded = mount.IsExpanded,
            });
        }

        this.Mounts.Add(new(this, AssetPath.Engine.Mount, engineContent.Path.FullPath, ContentMountKind.EngineContent));
        this.SelectedKind = this.Kinds[0];
    }

    /// <summary>Gets the mounts as they will be after Apply.</summary>
    public ObservableCollection<ContentMountRow> Mounts { get; } = [];

    /// <summary>Gets the locations the add form offers.</summary>
    public IReadOnlyList<string> Kinds { get; } = [LocalFolderKind, PakFileKind, .. KnownLocations.Select(static location => location.Kind)];

    /// <summary>Gets or sets the location to add.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(IsExternal), nameof(PathHeader), nameof(PathPlaceholder), nameof(BrowseToolTip))]
    public partial string SelectedKind { get; set; }

    /// <summary>Gets a value indicating whether the add form needs a name and a path: a local folder or a pak file.</summary>
    public bool IsExternal => this.IsPakFile || string.Equals(this.SelectedKind, LocalFolderKind, StringComparison.Ordinal);

    /// <summary>Gets the path field's header.</summary>
    public string PathHeader => this.IsPakFile ? "Pak file" : "Folder path";

    /// <summary>Gets the path field's example.</summary>
    public string PathPlaceholder => this.IsPakFile ? "D:/Studio/Paks/Environment.pak" : "D:/Studio/Assets";

    /// <summary>Gets the browse button's tooltip.</summary>
    public string BrowseToolTip => this.IsPakFile ? "Browse for a pak file" : "Browse for a folder";

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

    private bool IsPakFile => string.Equals(this.SelectedKind, PakFileKind, StringComparison.Ordinal);

    /// <summary>
    /// Checks a proposed mount name, unique among the remaining mounts, also when only the case differs. A project
    /// or derived mount names the first segment of its virtual paths, so it follows the authored mount rule; a local
    /// library or a pak file is a label, since its virtual paths come from its own index, and only needs text without
    /// separators.
    /// </summary>
    /// <param name="name">The proposed name.</param>
    /// <param name="kind">The kind of mount the name is for.</param>
    /// <param name="except">The mount being renamed, if any.</param>
    /// <returns>The reason the name is refused, or null.</returns>
    public string? ValidateName(string name, ContentMountKind kind, ContentMountRow? except = null)
    {
        ArgumentNullException.ThrowIfNull(name);
        var trimmed = name.Trim();
        if (kind is not (ContentMountKind.LocalLibrary or ContentMountKind.PakLibrary))
        {
            if (ProjectAssetFiles.ValidateDeclaredMountName(trimmed) is { } refusal)
            {
                return refusal;
            }
        }
        else if (trimmed.Length == 0 || trimmed.IndexOfAny(['/', '\\', ':']) >= 0)
        {
            return "Enter a library name without slashes or a colon.";
        }
        else if (ProjectAssetFiles.IsReservedMountName(trimmed))
        {
            return ProjectAssetFiles.ValidateDeclaredMountName(trimmed);
        }

        return this.Mounts.Any(row => !ReferenceEquals(row, except) && VirtualPathComparer.CaseFolded.Equals(row.Name, trimmed))
            ? "A mount with this name already exists, or differs from one only in case."
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
        if (this.IsPakFile)
        {
            this.AddPakFile();
            return;
        }

        if (!this.IsExternal)
        {
            var (kind, path) = KnownLocations.Single(location => string.Equals(location.Kind, this.SelectedKind, StringComparison.Ordinal));
            if (this.Mounts.Any(row => row.Kind == ContentMountKind.Derived && string.Equals(row.RelativePath?.Trim('/', '\\'), path, StringComparison.OrdinalIgnoreCase)))
            {
                this.ErrorMessage = $"{kind} is already mounted.";
                return;
            }

            if (this.ValidateName(kind, ContentMountKind.Derived) is { } conflict)
            {
                this.ErrorMessage = conflict;
                return;
            }

            this.Mounts.Insert(this.Mounts.Count - 1, new(this, kind, "./" + path, ContentMountKind.Derived) { RelativePath = path, IsNew = true });
            this.ErrorMessage = string.Empty;
            return;
        }

        var name = this.NewMountName.Trim();
        if (this.ValidateName(name, ContentMountKind.LocalLibrary) is { } invalid)
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
        this.Mounts.Insert(this.Mounts.Count - 1, new(this, name, folder, ContentMountKind.LocalLibrary) { AbsolutePath = folder, IsNew = true });
        this.NewMountName = string.Empty;
        this.NewMountPath = string.Empty;
        this.ErrorMessage = string.Empty;
    }

    private void AddPakFile()
    {
        var name = this.NewMountName.Trim();
        if (this.ValidateName(name, ContentMountKind.PakLibrary) is { } invalid)
        {
            this.ErrorMessage = invalid;
            return;
        }

        if (string.IsNullOrWhiteSpace(this.NewMountPath) || !File.Exists(this.NewMountPath)
            || !PakFileMount.IsPakFile(this.NewMountPath))
        {
            this.ErrorMessage = "Choose a .pak file that exists.";
            return;
        }

        var pak = Path.GetFullPath(this.NewMountPath);
        this.Mounts.Insert(this.Mounts.Count - 1, new(this, name, pak, ContentMountKind.PakLibrary) { AbsolutePath = pak, IsNew = true });
        this.NewMountName = string.Empty;
        this.NewMountPath = string.Empty;
        this.ErrorMessage = string.Empty;
    }

    [RelayCommand]
    private async Task BrowseAsync(CancellationToken cancellationToken)
    {
        try
        {
            var path = this.IsPakFile
                ? await this.dialogService.PickFileAsync(
                    new FilePickerSpec("Choose a pak file to mount", "Oxygen.PakFileMount", new Dictionary<string, IList<string>> { ["Pak files"] = [PakFileMount.Extension] })
                    {
                        SuggestedStartFolder = this.NewMountPath,
                    },
                    cancellationToken).ConfigureAwait(true)
                : await this.dialogService.PickFolderAsync(
                    new FolderPickerSpec("Choose a folder to mount", "Oxygen.LocalFolderMount") { SuggestedStartFolder = this.NewMountPath },
                    cancellationToken).ConfigureAwait(true);
            if (path is { Length: > 0 })
            {
                this.NewMountPath = path;
                if (this.NewMountName.Trim().Length == 0)
                {
                    this.NewMountName = this.IsPakFile ? Path.GetFileNameWithoutExtension(path) : Path.GetFileName(path.TrimEnd('\\', '/'));
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
            this.ErrorMessage = $"Could not pick a {(this.IsPakFile ? "pak file" : "folder")}: {error.Message}";
        }
    }
}
