// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Editor.Projects.Mounts;

/// <summary>The resolved mount table of a project on this machine, with what resolving it found.</summary>
/// <param name="ProjectRoot">The canonical project root.</param>
/// <param name="Mounts">The declared mounts, in declaration order, then the local ones.</param>
/// <param name="Findings">The diagnostics.</param>
public sealed record ProjectMountTable(FileSystemPath ProjectRoot, IReadOnlyList<ResolvedMount> Mounts, IReadOnlyList<MountFinding> Findings)
{
    /// <summary>Gets a value indicating whether the table has no error; warnings, such as an unbound mount, leave it valid.</summary>
    public bool IsValid => !this.Findings.Any(static finding => finding.IsError);

    /// <summary>Finds a mount by name, ignoring case as mount names are unique.</summary>
    /// <param name="name">The mount name.</param>
    /// <returns>The mount, or <see langword="null"/>.</returns>
    public ResolvedMount? Find(string name)
        => this.Mounts.FirstOrDefault(mount => string.Equals(mount.Name, name, StringComparison.OrdinalIgnoreCase));
}
