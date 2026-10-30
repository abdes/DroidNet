// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentExplorer.Panes.ContentFolders;

/// <summary>
///     Defines the user-provided data required to mount a local folder into the Content Folders pane.
/// </summary>
/// <param name="MountPointName">The mount point name (case-sensitive identity).</param>
/// <param name="AbsoluteFolderPath">The absolute folder path to mount.</param>
/// <param name="CommittedPath">The path <c>Project.oxy</c> declares, relative to the project; <see langword="null"/>
/// for a placeholder that the project's own <c>.mounts.json</c> binds to the folder on this machine.</param>
public sealed record LocalFolderMountDefinition(string MountPointName, string AbsoluteFolderPath, string? CommittedPath);
