// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentExplorer.Panes.ContentFolders;

/// <summary>
/// Where a new mount's folder or pak file is saved: as a path relative to the project in <c>Project.oxy</c>, shared
/// through the repository, or as a placeholder that the project's own <c>.mounts.json</c> binds on this machine only.
/// </summary>
/// <remarks>
/// The two differ in what is committed, not in where the folder is. A folder on the project's drive is saved in
/// <c>Project.oxy</c> unless the user chooses otherwise; one on another drive has no relative path, so it can only be
/// bound on this machine. A library's place is usually a fact of each machine, so outside the project it starts on this
/// machine.
/// </remarks>
/// <param name="projectRoot">The project root the relative path starts from.</param>
public sealed partial class MountPlacement(FileSystemPath projectRoot) : ObservableObject
{
    /// <summary>Gets or sets the folder or pak file the new mount names; empty until one is chosen.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(RelativePath), nameof(CanSaveInProject), nameof(InProjectText))]
    public partial string Location { get; set; } = string.Empty;

    /// <summary>Gets or sets a value indicating whether a location outside the project starts on this machine.</summary>
    public bool KeepsOutsideOnThisMachine { get; set; }

    /// <summary>Gets or sets a value indicating whether the location is saved in <c>Project.oxy</c>.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(SaveOnThisMachine))]
    public partial bool SaveInProject { get; set; }

    /// <summary>Gets or sets a value indicating whether the location is bound on this machine only.</summary>
    public bool SaveOnThisMachine
    {
        get => !this.SaveInProject;
        set => this.SaveInProject = !value;
    }

    /// <summary>Gets the location relative to the project root, with <c>/</c> separators; <see langword="null"/> when
    /// none is chosen or it is on another drive.</summary>
    public string? RelativePath
        => this.Location.Length != 0 && Path.IsPathFullyQualified(this.Location)
            && string.Equals(Path.GetPathRoot(this.Location), Path.GetPathRoot(projectRoot.FullPath), StringComparison.OrdinalIgnoreCase)
                ? Path.GetRelativePath(projectRoot.FullPath, this.Location).Replace('\\', '/')
                : null;

    /// <summary>Gets a value indicating whether the location has a relative path <c>Project.oxy</c> can hold.</summary>
    public bool CanSaveInProject => this.RelativePath is not null;

    /// <summary>Gets the text of the choice that saves the location in <c>Project.oxy</c>.</summary>
    public string InProjectText => this.RelativePath is { } relative
        ? $"In Project.oxy, as {relative}"
        : "In Project.oxy (not available: it is on another drive)";

    /// <summary>Gets the path <c>Project.oxy</c> declares: the relative path, or <see langword="null"/> for a
    /// placeholder.</summary>
    public string? CommittedPath => this.SaveInProject ? this.RelativePath : null;

    /// <summary>Gets the location the project's own <c>.mounts.json</c> binds: the location of a placeholder, or
    /// <see langword="null"/> when <c>Project.oxy</c> holds it.</summary>
    public FileSystemPath? LocalLocation => this.SaveInProject || this.Location.Length == 0 ? null : FileSystemPath.From(this.Location);

    // A newly chosen location takes the default: in Project.oxy when it has a relative path, for a library inside the project.
    partial void OnLocationChanged(string value)
        => this.SaveInProject = this.CanSaveInProject && (!this.KeepsOutsideOnThisMachine || !this.RelativePath!.StartsWith("..", StringComparison.Ordinal));

    partial void OnSaveInProjectChanged(bool value)
    {
        if (value && !this.CanSaveInProject)
        {
            this.SaveInProject = false;
        }
    }
}
