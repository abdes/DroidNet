// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentExplorer.Panes.ContentFolders;

/// <summary>
///     Identifies the output folder nodes the Content Folders pane can show; view state, never mounts.
/// </summary>
public enum KnownVirtualFolderMount
{
    /// <summary>
    ///     Show the project's cooked output folder (<c>.cooked</c>).
    /// </summary>
    Cooked,

    /// <summary>
    ///     Show the project's build output folder (<c>.build</c>).
    /// </summary>
    Build,
}
