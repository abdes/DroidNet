// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World;

/// <summary>
///     Defines a local folder mount point.
/// </summary>
/// <param name="Name">The mount point name (virtual root segment).</param>
/// <param name="AbsolutePath">The absolute path to the local folder.</param>
public sealed record LocalFolderMount(string Name, string AbsolutePath, bool IsExpanded = true);
