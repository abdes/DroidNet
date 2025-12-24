// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentBrowser.ProjectExplorer;

/// <summary>
///     Identifies whether a virtual folder mount backing path is project-relative or absolute.
/// </summary>
public enum VirtualFolderMountBackingPathKind
{
    /// <summary>
    ///     The backing path is relative to the project root (e.g. <c>.cooked</c>).
    /// </summary>
    ProjectRelative,

    /// <summary>
    ///     The backing path is an absolute OS path.
    /// </summary>
    Absolute,
}
