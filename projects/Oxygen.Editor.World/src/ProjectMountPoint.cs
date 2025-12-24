// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World;

/// <summary>
/// Represents an authoring mount point within a project.
/// </summary>
/// <remarks>
/// A mount point defines:
/// - A logical name (used as the virtual root segment, e.g. <c>Content</c>).
/// - A project-relative folder path (using <c>/</c> separators) that contains source assets.
///
/// This data is persisted in the project manifest (<c>Project.oxy</c>).
/// </remarks>
public sealed record ProjectMountPoint(string Name, string RelativePath, bool IsExpanded = true);
