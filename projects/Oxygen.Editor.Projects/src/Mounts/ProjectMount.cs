// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Projects.Mounts;

/// <summary>
/// A mount declared in <c>Project.oxy</c>: a name, a kind and a path relative to the project root, or no path for a
/// placeholder that a <c>.mounts.json</c> binds. A committed path is never absolute and may leave the project root.
/// </summary>
/// <param name="Name">The mount name: the first segment of every mount path in it.</param>
/// <param name="Kind">What the mount holds.</param>
/// <param name="Path">The path relative to the project root, or <see langword="null"/> for a placeholder.</param>
/// <param name="IsExpanded">Whether the Content Explorer shows the mount expanded.</param>
public sealed record ProjectMount(string Name, MountKind Kind = MountKind.Source, string? Path = null, bool IsExpanded = true);
