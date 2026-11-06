// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Oxygen.Editor.Projects.Mounts;
using Oxygen.Managed.Assets.Persistence;

namespace Oxygen.Editor.ContentExplorer.Panes.ContentFolders;

/// <summary>
/// One mount in the Content mounts dialog: its staged name, what <c>Project.oxy</c> commits for it, and its staged
/// binding on this machine in the project's own <c>.mounts.json</c>.
/// </summary>
public sealed partial class ContentMountRow : ObservableObject
{
    private readonly ContentMountsViewModel owner;

    /// <summary>Initializes a new instance of the <see cref="ContentMountRow"/> class.</summary>
    /// <param name="owner">The dialog that validates names, picks folders and removes rows.</param>
    /// <param name="name">The virtual mount name.</param>
    /// <param name="path">The backing path, as displayed for a mount without a committed path.</param>
    /// <param name="kind">What the mount exposes.</param>
    internal ContentMountRow(ContentMountsViewModel owner, string name, string path, ContentMountKind kind)
    {
        this.owner = owner;
        this.Name = name;
        this.OriginalName = name;
        this.Path = path;
        this.Kind = kind;
        this.NameDraft = name;
    }

    /// <summary>Gets the lock badge's tooltip of the engine content.</summary>
    public static string LockedToolTip => "The engine's content is always mounted beneath the project's, and every engine asset path starts with /Engine.";

    /// <summary>Gets the name the mount had when the dialog opened.</summary>
    public string OriginalName { get; }

    /// <summary>Gets the backing path, as displayed.</summary>
    public string Path { get; }

    /// <summary>Gets what the mount exposes.</summary>
    public ContentMountKind Kind { get; }

    /// <summary>Gets the user-facing kind.</summary>
    public string KindLabel => this.Kind switch
    {
        ContentMountKind.ProjectSource => "Source",
        ContentMountKind.Derived => "Output",
        _ => "Cooked",
    };

    /// <summary>Gets the row's icon glyph: a drive for folders, a package for pak files.</summary>
    public string Glyph => this.IsPak || this.Kind == ContentMountKind.EngineContent ? "" : "";

    /// <summary>Gets a value indicating whether the mount is fixed: the engine content is always mounted, under its own name.</summary>
    public bool IsLocked => this.Kind == ContentMountKind.EngineContent;

    /// <summary>Gets the committed project-relative path of a declared mount, or of a derived mount; <see langword="null"/>
    /// for a placeholder.</summary>
    public string? RelativePath { get; init; }

    /// <summary>Gets the absolute path of a folder or a pak file added by this dialog.</summary>
    public string? AbsolutePath { get; init; }

    /// <summary>Gets the location the project's own <c>.mounts.json</c> bound the mount to when the dialog opened;
    /// <see langword="null"/> when it bound none.</summary>
    public string? OriginalLocalLocation { get; init; }

    /// <summary>Gets the location a <c>.mounts.json</c> above the project binds the mount to; the dialog does not change
    /// that file.</summary>
    public string? ParentLocation { get; init; }

    /// <summary>Gets the <c>.mounts.json</c> above the project that binds the mount, when one does.</summary>
    public string? ParentBindingFile { get; init; }

    /// <summary>Gets a value indicating whether the sources tree shows this mount expanded.</summary>
    public bool IsExpanded { get; init; } = true;

    /// <summary>Gets a value indicating whether this is the project's own content, which always stays mounted.</summary>
    public bool IsPrimary { get; init; }

    /// <summary>Gets a value indicating whether only a <c>.mounts.json</c> adds the mount, so the project does not declare it.</summary>
    public bool IsLocal { get; init; }

    /// <summary>Gets a value indicating whether the mount is added by this dialog.</summary>
    public bool IsNew { get; init; }

    /// <summary>Gets a value indicating whether a library is bound to a pak file.</summary>
    public bool IsPak { get; init; }

    /// <summary>Gets a value indicating whether the mount breaks a mount rule, as the table resolved it.</summary>
    public bool IsInvalid { get; init; }

    /// <summary>Gets what the table found, as it resolved it.</summary>
    public IReadOnlyList<MountFinding> Findings { get; init; } = [];

    /// <summary>Gets what a library holds; <see langword="null"/> when it could not be read.</summary>
    public LibraryFacts? Facts { get; init; }

    /// <summary>Gets the current virtual name.</summary>
    [ObservableProperty]
    public partial string Name { get; private set; }

    /// <summary>Gets or sets the name being typed during a rename.</summary>
    [ObservableProperty]
    public partial string NameDraft { get; set; }

    /// <summary>Gets a value indicating whether the name is being edited.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(IsNotRenaming))]
    public partial bool IsRenaming { get; private set; }

    /// <summary>Gets or sets the location the project's own <c>.mounts.json</c> binds the mount to on this machine;
    /// <see langword="null"/> for none.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(Binding), nameof(BindingText), nameof(BindingDetail), nameof(BindingToolTip), nameof(IsCaution), nameof(ShowsInvalid), nameof(ShowsCaution), nameof(BindActionText),
        nameof(IsBindButtonVisible), nameof(IsLibraryBindButtonVisible), nameof(IsChangeBindingVisible), nameof(IsLibraryChangeBindingVisible), nameof(IsRemoveBindingVisible), nameof(ChangeBindingToolTip))]
    [NotifyCanExecuteChangedFor(nameof(RemoveBindingCommand))]
    public partial string? LocalLocation { get; set; }

    /// <summary>Gets a value indicating whether the name is shown as text.</summary>
    public bool IsNotRenaming => !this.IsRenaming;

    /// <summary>Gets the committed path, or <c>Placeholder</c> for a mount <c>Project.oxy</c> declares without one.</summary>
    public string CommittedText => this.Kind is ContentMountKind.Derived or ContentMountKind.EngineContent || (this.IsLocal && this.RelativePath is null)
        ? this.Path
        : this.RelativePath ?? "Placeholder";

    /// <summary>Gets a value indicating whether the mount is a placeholder, which <c>Project.oxy</c> declares without a
    /// path.</summary>
    public bool IsPlaceholder => !this.IsLocal && this.RelativePath is null && this.Kind is not (ContentMountKind.Derived or ContentMountKind.EngineContent);

    /// <summary>Gets where the mount's folder or pak file is on this machine.</summary>
    public MountBindingState Binding
    {
        get
        {
            if (this.Kind is ContentMountKind.Derived or ContentMountKind.EngineContent)
            {
                return MountBindingState.Fixed;
            }

            if (this.IsInvalid && this.IsAsResolved)
            {
                return MountBindingState.Invalid;
            }

            if ((this.LocalLocation ?? this.ParentLocation) is { } bound)
            {
                return this.owner.Exists(bound) ? MountBindingState.ThisMachine : MountBindingState.FolderMissing;
            }

            return this.RelativePath is null ? MountBindingState.NotBound
                : this.owner.Exists(this.owner.ProjectRoot.Combine(this.RelativePath).FullPath) ? MountBindingState.Committed
                : MountBindingState.FolderMissing;
        }
    }

    /// <summary>Gets the binding's state, as the row shows it.</summary>
    public string BindingText => this.Binding switch
    {
        MountBindingState.Committed => this.HasCurrentFindings ? "Committed · " + FindingText(this.Findings[0].Code) : "Committed",
        MountBindingState.ThisMachine => this.HasCurrentFindings ? "This machine · " + FindingText(this.Findings[0].Code) : "This machine",
        MountBindingState.Invalid => this.HasCurrentFindings ? "Invalid: " + FindingText(this.Findings[0].Code) : "Invalid",
        MountBindingState.NotBound => "Not bound",
        MountBindingState.FolderMissing => "Folder missing",
        _ => this.IsLocked ? "Always mounted" : "Shown",
    };

    /// <summary>Gets the folder the binding names, or the one that is missing; empty when the committed path says it.</summary>
    public string BindingDetail => this.Binding == MountBindingState.Invalid && this.HasCurrentFindings
        ? this.Findings[0].Code
        : this.LocalLocation ?? this.ParentLocation
            ?? (this.Binding == MountBindingState.FolderMissing && this.RelativePath is { } relative
                ? this.owner.ProjectRoot.Combine(relative).FullPath
                : string.Empty);

    /// <summary>Gets the binding's tooltip: what the table found in words, else the location.</summary>
    public string BindingToolTip => this.HasCurrentFindings
        ? string.Join('\n', this.Findings.Select(static finding => finding.Message))
        : this.BindingDetail;

    /// <summary>Gets a value indicating whether the row marks the mount as breaking a mount rule.</summary>
    public bool ShowsInvalid => this.Binding == MountBindingState.Invalid;

    /// <summary>Gets a value indicating whether the row marks a caution: it cannot be read here, or a loose library lacks what it needs.</summary>
    public bool ShowsCaution => this.IsCaution || (this.HasCurrentFindings && !this.ShowsInvalid);

    /// <summary>Gets a value indicating whether the mount cannot be read on this machine: not bound, or its folder is
    /// missing.</summary>
    public bool IsCaution => this.Binding is MountBindingState.NotBound or MountBindingState.FolderMissing;

    /// <summary>Gets a value indicating whether the mount's location can be bound on this machine.</summary>
    public bool CanBind => this.Kind is not (ContentMountKind.Derived or ContentMountKind.EngineContent) && this.ParentBindingFile is null;

    /// <summary>Gets the text of the button that binds a mount this machine cannot read.</summary>
    public string BindActionText => this.Binding == MountBindingState.NotBound ? "Bind…" : "Override…";

    /// <summary>Gets a value indicating whether the row offers Bind or Override.</summary>
    public bool IsBindButtonVisible => this.CanBind && this.IsCaution && this.Kind != ContentMountKind.Library;

    /// <summary>Gets a value indicating whether the row offers Bind or Override for a library, which picks a pak file or a folder.</summary>
    public bool IsLibraryBindButtonVisible => this.CanBind && this.IsCaution && this.Kind == ContentMountKind.Library;

    /// <summary>Gets a value indicating whether the row offers to change where a readable mount is on this machine.</summary>
    public bool IsChangeBindingVisible => this.CanBind && !this.IsCaution && !this.IsPrimary && this.Kind != ContentMountKind.Library;

    /// <summary>Gets a value indicating whether the row offers to change where a readable library is on this machine, a pak file or a folder.</summary>
    public bool IsLibraryChangeBindingVisible => this.CanBind && !this.IsCaution && this.Kind == ContentMountKind.Library;

    /// <summary>Gets a value indicating whether the row offers to remove the binding of the project's own <c>.mounts.json</c>.</summary>
    public bool IsRemoveBindingVisible => this.LocalLocation is not null && !this.IsLocal;

    /// <summary>Gets the tooltip of the button that changes the binding.</summary>
    public string ChangeBindingToolTip => this.LocalLocation is null
        ? $"Override {this.Name} on this machine: bind it to another folder in .mounts.json."
        : $"Bind {this.Name} to another folder on this machine.";

    /// <summary>
    /// Gets a value indicating whether the mount can be renamed: only a source folder this dialog adds. A source's name
    /// is the first segment of every asset identity inside it, a library's is the root of what it holds, and an output
    /// folder node keeps its reserved name.
    /// </summary>
    public bool CanRename => this.Kind == ContentMountKind.ProjectSource && this.IsNew;

    /// <summary>Gets a value indicating whether the row shows the rename button; a library's name is its content's, so it has none.</summary>
    public bool IsRenameVisible => this.HasActions && this.Kind != ContentMountKind.Library;

    /// <summary>Gets a value indicating whether the row offers rename and unmount; the project's own content and the engine content have neither.</summary>
    public bool HasActions => !this.IsPrimary && !this.IsLocked;

    /// <summary>Gets a value indicating whether the mount can be removed: not the project's own content, the engine content,
    /// or a mount only a <c>.mounts.json</c> above the project adds.</summary>
    public bool CanUnmount => !this.IsPrimary && !this.IsLocked && !(this.IsLocal && this.ParentBindingFile is not null);

    /// <summary>Gets the rename button's tooltip.</summary>
    public string RenameToolTip => this.CanRename
        ? $"Rename {this.Name}"
        : "Project sources keep their names: every asset path inside starts with it.";

    /// <summary>Gets the unmount button's tooltip.</summary>
    public string UnmountToolTip => this.CanUnmount
        ? $"Unmount {this.Name}. Files are not changed."
        : this.ParentBindingFile is { } file ? $"'{file}' adds {this.Name}; change that file to remove it." : "The project's content is always mounted.";

    private bool IsAsResolved => string.Equals(this.LocalLocation, this.OriginalLocalLocation, StringComparison.Ordinal);

    private bool HasCurrentFindings => this.Findings.Count != 0 && this.IsAsResolved;

    /// <summary>Accepts the typed name when it is valid.</summary>
    /// <returns>Whether the rename finished.</returns>
    internal bool TryCommitRename()
    {
        if (this.owner.ValidateName(this.NameDraft, this) is { } reason)
        {
            this.owner.ErrorMessage = reason;
            return false;
        }

        this.Name = this.NameDraft.Trim();
        this.IsRenaming = false;
        this.owner.ErrorMessage = string.Empty;
        return true;
    }

    private static string FindingText(string code) => code switch
    {
        MountCodes.CookedRootMismatch => "holds another root",
        MountCodes.CookedRootMultiple => "holds several roots",
        MountCodes.CookedExternalScript => "holds an external script",
        MountCodes.CookedDependencyMissing => "lacks a library it needs",
        MountCodes.CookedFormatUnsupported => "cannot be read",
        _ => code,
    };

    partial void OnNameChanged(string value)
    {
        this.OnPropertyChanged(nameof(this.RenameToolTip));
        this.OnPropertyChanged(nameof(this.UnmountToolTip));
        this.OnPropertyChanged(nameof(this.ChangeBindingToolTip));
    }

    // The pencil starts a rename; the check accepts it.
    [RelayCommand(CanExecute = nameof(CanRename))]
    private void Rename()
    {
        if (this.IsRenaming)
        {
            _ = this.TryCommitRename();
            return;
        }

        this.NameDraft = this.Name;
        this.IsRenaming = true;
    }

    [RelayCommand]
    private void CancelRename()
    {
        this.NameDraft = this.Name;
        this.IsRenaming = false;
        this.owner.ErrorMessage = string.Empty;
    }

    [RelayCommand(CanExecute = nameof(CanUnmount))]
    private void Unmount() => this.owner.Remove(this);

    // Bind, Override and Change binding pick a folder for this machine's .mounts.json; a library's form is a machine fact,
    // so it may pick a pak file instead.
    [RelayCommand(CanExecute = nameof(CanBind))]
    private Task BindAsync(CancellationToken cancellationToken) => this.PickBindingAsync(pakFile: false, cancellationToken);

    [RelayCommand(CanExecute = nameof(CanBind))]
    private Task BindPakAsync(CancellationToken cancellationToken) => this.PickBindingAsync(pakFile: true, cancellationToken);

    private async Task PickBindingAsync(bool pakFile, CancellationToken cancellationToken)
    {
        if (await this.owner.PickLocationAsync(pakFile, this.LocalLocation ?? this.AbsolutePath, cancellationToken).ConfigureAwait(true) is { } location)
        {
            this.LocalLocation = location;
        }
    }

    [RelayCommand(CanExecute = nameof(IsRemoveBindingVisible))]
    private void RemoveBinding() => this.LocalLocation = null;
}
