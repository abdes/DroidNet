// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Editor.Projects.Mounts;

/// <summary>A mounts file that was read: its bindings, in document order.</summary>
/// <param name="Path">The file.</param>
/// <param name="IsParent">Whether the file is above the project root.</param>
/// <param name="Entries">The mount names and their paths, as written.</param>
internal sealed record MountsFile(FileSystemPath Path, bool IsParent, IReadOnlyList<(string Name, string Path)> Entries);
