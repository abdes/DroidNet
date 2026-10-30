// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Projects.Mounts;

/// <summary>The diagnostic codes of a mount table, as the engine's mount contract defines them.</summary>
public static class MountCodes
{
    /// <summary>A name breaks the name rule or is reserved.</summary>
    public const string NameInvalid = "mount.name_invalid";

    /// <summary>Two mounts share a name, ignoring case.</summary>
    public const string NameDuplicate = "mount.name_duplicate";

    /// <summary>A committed file holds an absolute path.</summary>
    public const string AbsoluteCommitted = "path.absolute_committed";

    /// <summary>Two source mount folders are the same folder or contain one another.</summary>
    public const string FolderOverlap = "mount.folder_overlap";

    /// <summary>A source mount folder is or lies in an output folder.</summary>
    public const string FolderInOutput = "mount.folder_in_output";

    /// <summary>A bound folder or file does not exist.</summary>
    public const string FolderMissing = "mount.folder_missing";

    /// <summary>A pak mount is bound to a folder, or another kind to a file.</summary>
    public const string PathKind = "mount.path_kind";

    /// <summary>A parent <c>.mounts.json</c> names a mount the project commits a path for.</summary>
    public const string ParentIgnored = "mount.parent_ignored";

    /// <summary>A placeholder nothing binds.</summary>
    public const string Unbound = "mount.unbound";

    /// <summary>A <c>.mounts.json</c> of another version.</summary>
    public const string FileVersion = "mount.file_version";

    /// <summary>A <c>.mounts.json</c> that cannot be read or breaks its schema.</summary>
    public const string FileInvalid = "mount.file_invalid";

    /// <summary>The project declares no source mount, so it has nowhere to author content.</summary>
    public const string SourceRequired = "mount.source_required";

    /// <summary>Gets whether a code is a warning: the table stays usable and the project opens.</summary>
    /// <param name="code">The code.</param>
    /// <returns><see langword="true"/> for a warning; <see langword="false"/> for an error.</returns>
    public static bool IsWarning(string code)
        => code is FolderMissing or ParentIgnored or Unbound;
}
