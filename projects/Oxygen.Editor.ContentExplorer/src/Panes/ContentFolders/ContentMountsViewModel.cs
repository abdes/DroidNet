// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Aura.Dialogs;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Mounts;
using Oxygen.Managed.Assets.Persistence;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentExplorer.Panes.ContentFolders;

/// <summary>
/// The Content mounts dialog: every mount with its virtual name, kind, what <c>Project.oxy</c> commits for it and where
/// it is on this machine, with Bind, Override, Remove binding, rename of a new source folder and unmount; and a form that
/// adds a source folder with its name, a library, named by what it holds, or the Cooked or Build output folder node,
/// saved in <c>Project.oxy</c> or on this machine only. An output folder node is view state, never a mount, and keeps its
/// reserved name. The engine content is listed last, always mounted. Edits are staged and applied together: the
/// declarations in <c>Project.oxy</c>, and the bindings in the project's own <c>.mounts.json</c>.
/// </summary>
public sealed partial class ContentMountsViewModel : ObservableObject
{
    /// <summary>The add-form choice for a folder of authored content anywhere on disk.</summary>
    public const string SourceFolderKind = "Source folder";

    /// <summary>The add-form choice for a library: a pak file, a cooked folder or a library project's <c>.cooked</c>.</summary>
    public const string LibraryKind = "Library";

    private static readonly string[] OutputFolders = [ProjectOutputFolders.Cooked, ProjectOutputFolders.Build];

    private readonly IDialogService dialogService;
    private readonly ProjectContext project;
    private readonly List<ContentMountRow> removed = [];

    /// <summary>Initializes a new instance of the <see cref="ContentMountsViewModel"/> class.</summary>
    /// <param name="project">The project whose mounts are edited.</param>
    /// <param name="dialogService">The folder and file pickers.</param>
    /// <param name="engineContent">The engine content pak, listed as the fixed lowest mount.</param>
    public ContentMountsViewModel(ProjectContext project, IDialogService dialogService, IEngineContentPak engineContent)
    {
        ArgumentNullException.ThrowIfNull(project);
        ArgumentNullException.ThrowIfNull(engineContent);
        this.dialogService = dialogService;
        this.project = project;
        this.Placement = new(project.ProjectRoot);
        var content = project.Mounts.FirstOrDefault(static mount => mount.Kind == MountKind.Source);
        foreach (var mount in project.Mounts.Where(static mount => mount.Kind == MountKind.Source))
        {
            this.Mounts.Add(this.RowOf(mount, ContentMountKind.ProjectSource, ReferenceEquals(mount, content)));
        }

        foreach (var view in project.OutputFolderViews)
        {
            this.Mounts.Add(new(this, view.Name, "./" + view.Folder, ContentMountKind.Derived)
            {
                RelativePath = view.Folder,
                IsExpanded = view.IsExpanded,
            });
        }

        foreach (var mount in project.CookedMounts)
        {
            this.Mounts.Add(this.RowOf(mount, ContentMountKind.Library, isPrimary: false));
        }

        this.Mounts.Add(new(this, AssetPath.Engine.Mount, engineContent.Path.FullPath, ContentMountKind.EngineContent));
        this.SelectedKind = this.Kinds[0];
    }

    /// <summary>Gets the mounts as they will be after Apply.</summary>
    public ObservableCollection<ContentMountRow> Mounts { get; } = [];

    /// <summary>Gets where the folder or pak file of the mount to add is saved.</summary>
    public MountPlacement Placement { get; }

    /// <summary>Gets the locations the add form offers.</summary>
    public IReadOnlyList<string> Kinds { get; } = [SourceFolderKind, LibraryKind, .. OutputFolders.Select(ProjectOutputFolders.ViewName)];

    /// <summary>Gets or sets the location to add.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(IsExternal), nameof(IsSourceFolder), nameof(IsLibrary), nameof(PathHeader), nameof(PathPlaceholder), nameof(AddText))]
    [NotifyCanExecuteChangedFor(nameof(AddMountCommand))]
    public partial string SelectedKind { get; set; }

    /// <summary>Gets a value indicating whether the add form needs a path: a source folder or a library.</summary>
    public bool IsExternal => this.IsSourceFolder || this.IsLibrary;

    /// <summary>Gets a value indicating whether the add form adds a source folder, which needs a name.</summary>
    public bool IsSourceFolder => string.Equals(this.SelectedKind, SourceFolderKind, StringComparison.Ordinal);

    /// <summary>Gets a value indicating whether the add form adds a library, which is named by what it holds.</summary>
    public bool IsLibrary => string.Equals(this.SelectedKind, LibraryKind, StringComparison.Ordinal);

    /// <summary>Gets the path field's header.</summary>
    public string PathHeader => this.IsLibrary ? "Pak file or cooked folder" : "Folder path";

    /// <summary>Gets the path field's example.</summary>
    public string PathPlaceholder => this.IsLibrary ? "D:/Studio/Paks/Environment.pak" : "D:/Studio/Assets";

    /// <summary>Gets the add button's text.</summary>
    public string AddText => this.IsLibrary ? "Add library" : "Add mount";

    /// <summary>Gets or sets the virtual name of a source folder to add.</summary>
    [ObservableProperty]
    public partial string NewMountName { get; set; } = string.Empty;

    /// <summary>Gets or sets the folder or library to add.</summary>
    [ObservableProperty]
    public partial string NewMountPath { get; set; } = string.Empty;

    /// <summary>Gets the library the form would add, read from <see cref="NewMountPath"/>.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasCandidate), nameof(HasUnmetNeed))]
    [NotifyCanExecuteChangedFor(nameof(AddMountCommand))]
    public partial LibraryCandidate? Candidate { get; private set; }

    /// <summary>Gets the reason the last add or rename was refused.</summary>
    [ObservableProperty]
    public partial string ErrorMessage { get; set; } = string.Empty;

    /// <summary>Gets a value indicating whether there is an error to show.</summary>
    public bool HasError => this.ErrorMessage.Length != 0;

    /// <summary>Gets a value indicating whether a readable library is picked.</summary>
    public bool HasCandidate => this.Candidate is not null;

    /// <summary>Gets a value indicating whether the picked library lacks something it needs.</summary>
    public bool HasUnmetNeed => this.Candidate?.HasUnmetNeed == true;

    /// <summary>Gets the project root, which committed paths are relative to.</summary>
    internal FileSystemPath ProjectRoot => this.project.ProjectRoot;

    /// <summary>
    /// Checks a proposed mount name, unique among the remaining mounts, also when only the case differs. Every mount
    /// kind follows the one rule of a declared mount name.
    /// </summary>
    /// <param name="name">The proposed name.</param>
    /// <param name="except">The mount being renamed, if any.</param>
    /// <returns>The reason the name is refused, or null.</returns>
    public string? ValidateName(string name, ContentMountRow? except = null)
    {
        ArgumentNullException.ThrowIfNull(name);
        var trimmed = name.Trim();
        if (ProjectAssetFiles.ValidateDeclaredMountName(trimmed) is { } refusal)
        {
            return refusal;
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

    /// <summary>
    /// Gets the changes the staged rows make to the project's own <c>.mounts.json</c>: the bindings added, changed or
    /// removed, under the mounts' new names, and those of the mounts unmounted.
    /// </summary>
    /// <returns>The changes; none when no binding changed.</returns>
    public MountBindings GetBindingChanges()
    {
        var changes = MountBindings.None;
        foreach (var row in this.removed.Where(static row => row.OriginalLocalLocation is not null))
        {
            changes = changes.Unbind(row.OriginalName);
        }

        foreach (var row in this.Mounts)
        {
            var renamed = !string.Equals(row.Name, row.OriginalName, StringComparison.Ordinal);
            if (renamed && row.OriginalLocalLocation is not null)
            {
                changes = changes.Unbind(row.OriginalName);
            }

            if (renamed ? row.LocalLocation is not null : !string.Equals(row.LocalLocation, row.OriginalLocalLocation, StringComparison.Ordinal))
            {
                changes = row.LocalLocation is { } location ? changes.Bind(row.Name, FileSystemPath.From(location)) : changes.Unbind(row.Name);
            }
        }

        return changes;
    }

    /// <summary>Removes a mount from the staged list; its files are not touched.</summary>
    /// <param name="row">The mount to remove.</param>
    internal void Remove(ContentMountRow row)
    {
        if (this.Mounts.Remove(row) && !row.IsNew)
        {
            this.removed.Add(row);
        }

        this.ErrorMessage = string.Empty;
        this.InspectLibrary();
    }

    /// <summary>Tests whether a folder or a file exists on this machine.</summary>
    /// <param name="path">The absolute path.</param>
    /// <returns>Whether a folder or a file is there.</returns>
    internal bool Exists(string path) => this.project.FileSystem.Directory.Exists(path) || this.project.FileSystem.File.Exists(path);

    /// <summary>Asks for a folder, or a pak file, to bind a mount to.</summary>
    /// <param name="pakFile">Whether a pak file is picked rather than a folder.</param>
    /// <param name="start">Where the picker starts, if anywhere.</param>
    /// <param name="cancellationToken">Cancels the picker.</param>
    /// <returns>The picked location, or <see langword="null"/> when none was picked.</returns>
    internal async Task<string?> PickLocationAsync(bool pakFile, string? start, CancellationToken cancellationToken)
    {
        try
        {
            return await MountLocationPicker.PickAsync(this.dialogService, pakFile, start, cancellationToken).ConfigureAwait(true);
        }
        catch (IOException error)
        {
            this.ErrorMessage = error.Message;
            return null;
        }
    }

    // A mount's row: what Project.oxy commits for it, and where a .mounts.json binds it on this machine. The project's
    // own file is the one the dialog changes; one above the project is shown and left as it is.
    private ContentMountRow RowOf(ResolvedMount mount, ContentMountKind kind, bool isPrimary)
    {
        var ownFile = this.project.ProjectRoot.Combine(ProjectMounts.FileName);
        var boundByFile = mount.Origin == MountOrigin.MountsFile ? mount.BoundTo?.FullPath : null;
        var boundHere = mount.OriginFile == ownFile;
        return new(this, mount.Name, mount.BoundTo?.FullPath ?? string.Empty, kind)
        {
            RelativePath = mount.DeclaredPath?.Replace('\\', '/').Trim('/'),
            IsExpanded = mount.IsExpanded,
            IsPrimary = isPrimary,
            IsLocal = mount.Scope == MountScope.Local,
            OriginalLocalLocation = boundHere ? boundByFile : null,
            LocalLocation = boundHere ? boundByFile : null,
            ParentLocation = boundHere ? null : boundByFile,
            ParentBindingFile = boundHere || boundByFile is null ? null : mount.OriginFile?.FullPath,
            IsPak = mount.IsPakFile,
            IsInvalid = mount.State == MountState.Invalid,
            Findings = mount.Findings,
            Facts = mount.Library,
        };
    }

    partial void OnSelectedKindChanged(string value)
    {
        this.Placement.KeepsOutsideOnThisMachine = this.IsLibrary;
        this.InspectLibrary();
    }

    partial void OnErrorMessageChanged(string value) => this.OnPropertyChanged(nameof(this.HasError));

    partial void OnNewMountPathChanged(string value)
    {
        this.Placement.Location = Path.IsPathFullyQualified(value) ? Path.GetFullPath(value) : value;
        this.InspectLibrary();
    }

    // The library's facts come from the file as it is picked, so what it is called and needs shows before it is added.
    private void InspectLibrary()
    {
        this.Candidate = null;
        if (!this.IsLibrary || !Path.IsPathFullyQualified(this.NewMountPath) || !this.Exists(this.NewMountPath))
        {
            return;
        }

        var mounted = this.Mounts.Where(static row => row.Kind == ContentMountKind.Library && row.Facts is not null)
            .ToDictionary(static row => row.Name, static row => row.Facts!, StringComparer.Ordinal);
        var candidate = LibraryCandidate.Inspect(this.project, FileSystemPath.From(Path.GetFullPath(this.NewMountPath)), mounted, out var refusal);
        if (candidate is not null && this.ValidateName(candidate.Name) is { } name)
        {
            refusal = string.Equals(name, "A mount with this name already exists, or differs from one only in case.", StringComparison.Ordinal)
                ? $"{MountCodes.NameDuplicate}: a mount named {candidate.Name} is already here."
                : $"{MountCodes.NameInvalid}: {name}";
            candidate = null;
        }

        this.Candidate = candidate;
        this.ErrorMessage = refusal ?? string.Empty;
    }

    private bool CanAddMount() => !this.IsLibrary || this.Candidate is not null;

    [RelayCommand(CanExecute = nameof(CanAddMount))]
    private void AddMount()
    {
        if (this.IsLibrary)
        {
            this.AddLibrary();
        }
        else if (this.IsSourceFolder)
        {
            this.AddSourceFolder();
        }
        else
        {
            this.AddOutputFolder();
        }
    }

    private void AddOutputFolder()
    {
        var path = OutputFolders.Single(folder => string.Equals(ProjectOutputFolders.ViewName(folder), this.SelectedKind, StringComparison.Ordinal));
        var kind = ProjectOutputFolders.ViewName(path);
        if (this.Mounts.Any(row => row.Kind == ContentMountKind.Derived && string.Equals(row.RelativePath, path, StringComparison.Ordinal)))
        {
            this.ErrorMessage = $"{kind} is already shown.";
            return;
        }

        this.Mounts.Insert(this.Mounts.Count - 1, new(this, kind, "./" + path, ContentMountKind.Derived) { RelativePath = path, IsNew = true });
        this.ErrorMessage = string.Empty;
    }

    private void AddSourceFolder()
    {
        var name = this.NewMountName.Trim();
        if (this.ValidateName(name) is { } invalid)
        {
            this.ErrorMessage = invalid;
            return;
        }

        if (!this.project.FileSystem.Directory.Exists(this.NewMountPath))
        {
            this.ErrorMessage = "Choose a folder that exists.";
            return;
        }

        // Project.oxy commits the location, or a placeholder that this machine's .mounts.json binds to it.
        var location = Path.GetFullPath(this.NewMountPath);
        this.Mounts.Insert(this.Mounts.Count - 1, new(this, name, location, ContentMountKind.ProjectSource)
        {
            AbsolutePath = location,
            RelativePath = this.Placement.CommittedPath,
            LocalLocation = this.Placement.LocalLocation?.FullPath,
            IsNew = true,
        });
        this.NewMountName = string.Empty;
        this.NewMountPath = string.Empty;
        this.ErrorMessage = string.Empty;
    }

    private void AddLibrary()
    {
        if (this.Candidate is not { } candidate)
        {
            return;
        }

        var location = Path.GetFullPath(this.NewMountPath);
        this.Mounts.Insert(this.Mounts.Count - 1, new(this, candidate.Name, location, ContentMountKind.Library)
        {
            AbsolutePath = location,
            RelativePath = this.Placement.CommittedPath,
            LocalLocation = this.Placement.LocalLocation?.FullPath,
            IsNew = true,
            IsPak = candidate.Read.Container == MountContainer.Pak,
            Facts = candidate.Read.Facts,
        });
        this.NewMountPath = string.Empty;
        this.ErrorMessage = string.Empty;
    }

    [RelayCommand]
    private async Task BrowseAsync(CancellationToken cancellationToken)
    {
        if (await this.PickLocationAsync(pakFile: false, this.NewMountPath, cancellationToken).ConfigureAwait(true) is { } path)
        {
            this.NewMountPath = path;
            if (this.IsSourceFolder && this.NewMountName.Trim().Length == 0)
            {
                this.NewMountName = Path.GetFileName(Path.TrimEndingDirectorySeparator(path));
            }
        }
    }

    [RelayCommand]
    private async Task BrowsePakAsync(CancellationToken cancellationToken)
    {
        if (await this.PickLocationAsync(pakFile: true, this.NewMountPath, cancellationToken).ConfigureAwait(true) is { } path)
        {
            this.NewMountPath = path;
        }
    }
}
