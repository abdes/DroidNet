// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentExplorer.Panes.ContentFolders;

/// <summary>Where a mount's folder or pak file is on this machine, as the Content mounts dialog shows it.</summary>
public enum MountBindingState
{
    /// <summary><c>Project.oxy</c> holds its relative path, and the folder is there.</summary>
    Committed,

    /// <summary>A <c>.mounts.json</c> binds it on this machine, and the folder is there.</summary>
    ThisMachine,

    /// <summary>It is a placeholder that no <c>.mounts.json</c> binds.</summary>
    NotBound,

    /// <summary>The folder or pak file it is bound to is missing.</summary>
    FolderMissing,

    /// <summary>The library is there and breaks a mount rule; it contributes no assets.</summary>
    Invalid,

    /// <summary>It has no binding to change: the engine's content, or an output folder node.</summary>
    Fixed,
}
