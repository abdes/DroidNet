// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentBrowser.ProjectExplorer;

/// <summary>
///     Defines the user-provided data required to mount a local folder into the Project Explorer.
/// </summary>
/// <param name="MountPointName">The mount point name (case-sensitive identity).</param>
/// <param name="AbsoluteFolderPath">The absolute folder path to mount.</param>
public sealed record LocalFolderMountDefinition(string MountPointName, string AbsoluteFolderPath);
