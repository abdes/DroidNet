// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentBrowser.ProjectExplorer;

/// <summary>
///     Identifies the built-in virtual folder mounts offered by the Project Explorer.
/// </summary>
public enum KnownVirtualFolderMount
{
    /// <summary>
    ///     Mount the project's cooked output folder (<c>.cooked</c>).
    /// </summary>
    Cooked,

    /// <summary>
    ///     Mount the project's imported assets folder (<c>.imported</c>).
    /// </summary>
    Imported,

    /// <summary>
    ///     Mount the project's build output folder (<c>.build</c>).
    /// </summary>
    Build,
}
