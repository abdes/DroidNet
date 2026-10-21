// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentBrowser.ProjectExplorer;

/// <summary>Carries the virtual folder mount that was renamed.</summary>
/// <param name="mount">The mount, under its new name.</param>
public sealed class MountRenamedEventArgs(VirtualFolderMountTreeItemAdapter mount) : EventArgs
{
    /// <summary>Gets the mount, under its new name.</summary>
    public VirtualFolderMountTreeItemAdapter Mount { get; } = mount;
}
