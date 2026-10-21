// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;

namespace Oxygen.Editor.ContentExplorer.Panes.ContentFolders;

/// <summary>One mount in the Content mounts dialog, with its staged name.</summary>
public sealed partial class ContentMountRow : ObservableObject
{
    private readonly ContentMountsViewModel owner;

    /// <summary>Initializes a new instance of the <see cref="ContentMountRow"/> class.</summary>
    /// <param name="owner">The dialog that validates names and removes rows.</param>
    /// <param name="name">The virtual mount name.</param>
    /// <param name="path">The backing path, as displayed.</param>
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

    /// <summary>Gets the name the mount had when the dialog opened.</summary>
    public string OriginalName { get; }

    /// <summary>Gets the backing path, as displayed.</summary>
    public string Path { get; }

    /// <summary>Gets what the mount exposes.</summary>
    public ContentMountKind Kind { get; }

    /// <summary>Gets the user-facing kind.</summary>
    public string KindLabel => this.Kind switch
    {
        ContentMountKind.ProjectSource => "Project source",
        ContentMountKind.Derived => "Derived output",
        ContentMountKind.PakLibrary => "Pak file",
        ContentMountKind.EngineContent => "Engine content",
        _ => "Local library",
    };

    /// <summary>Gets the row's icon glyph: a drive for folders, a package for pak files.</summary>
    public string Glyph => this.Kind is ContentMountKind.PakLibrary or ContentMountKind.EngineContent ? "\uE7B8" : "\uEDA2";

    /// <summary>Gets a value indicating whether the mount is fixed: the engine content is always mounted, under its own name.</summary>
    public bool IsLocked => this.Kind == ContentMountKind.EngineContent;

    /// <summary>Gets the project-relative path of project and derived mounts.</summary>
    public string? RelativePath { get; init; }

    /// <summary>Gets the absolute path of a local library or a pak file.</summary>
    public string? AbsolutePath { get; init; }

    /// <summary>Gets a value indicating whether the sources tree shows this mount expanded.</summary>
    public bool IsExpanded { get; init; } = true;

    /// <summary>Gets a value indicating whether this is the project's own content, which always stays mounted.</summary>
    public bool IsPrimary { get; init; }

    /// <summary>Gets a value indicating whether the mount is added by this dialog.</summary>
    public bool IsNew { get; init; }

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

    /// <summary>Gets a value indicating whether the name is shown as text.</summary>
    public bool IsNotRenaming => !this.IsRenaming;

    /// <summary>
    /// Gets a value indicating whether the mount can be renamed. A project source's name is the first segment of
    /// every asset identity inside it, so it keeps its name until relocation updates references.
    /// </summary>
    public bool CanRename => this.Kind is not (ContentMountKind.ProjectSource or ContentMountKind.EngineContent);

    /// <summary>Gets a value indicating whether the row offers rename and unmount; the project's own content and the engine content have neither.</summary>
    public bool HasActions => !this.IsPrimary && !this.IsLocked;

    /// <summary>Gets a value indicating whether the mount can be removed; the project's own content and the engine content cannot.</summary>
    public bool CanUnmount => !this.IsPrimary && !this.IsLocked;

    /// <summary>Gets the lock badge's tooltip of the engine content.</summary>
    public string LockedToolTip => "The engine's content is always mounted beneath the project's, and every engine asset path starts with /Engine.";

    /// <summary>Gets the rename button's tooltip.</summary>
    public string RenameToolTip => this.CanRename
        ? $"Rename {this.Name}"
        : "Project sources keep their names: every asset path inside starts with it.";

    /// <summary>Gets the unmount button's tooltip.</summary>
    public string UnmountToolTip => this.CanUnmount
        ? $"Unmount {this.Name}. Files are not changed."
        : "The project's content is always mounted.";

    /// <summary>Accepts the typed name when it is valid.</summary>
    /// <returns>Whether the rename finished.</returns>
    internal bool TryCommitRename()
    {
        if (this.owner.ValidateName(this.NameDraft, this.Kind, this) is { } reason)
        {
            this.owner.ErrorMessage = reason;
            return false;
        }

        this.Name = this.NameDraft.Trim();
        this.IsRenaming = false;
        this.owner.ErrorMessage = string.Empty;
        return true;
    }

    partial void OnNameChanged(string value)
    {
        this.OnPropertyChanged(nameof(this.RenameToolTip));
        this.OnPropertyChanged(nameof(this.UnmountToolTip));
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
}
