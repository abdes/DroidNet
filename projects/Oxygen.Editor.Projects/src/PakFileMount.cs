// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Projects;

/// <summary>
///     Defines a pak file the project mounts as a read-only cooked library: its assets keep the virtual paths the pak
///     records, and cooks reference them without copying them.
/// </summary>
/// <param name="Name">The mount name, unique among the project's mounts.</param>
/// <param name="Path">The pak file, stored in the manifest as <c>AbsolutePath</c>.</param>
/// <param name="IsExpanded">Whether the project layout shows the mount expanded.</param>
public sealed record PakFileMount(
    string Name,
    [property: JsonPropertyName("AbsolutePath")] FileSystemPath Path,
    bool IsExpanded = true)
{
    /// <summary>The extension of a pak file; runtimes and the Player tell a pak from a loose root by it.</summary>
    public const string Extension = ".pak";

    /// <summary>Checks whether a path names a pak file, which is what a pak file mount must name.</summary>
    /// <param name="path">The path.</param>
    /// <returns><see langword="true"/> when the path has the pak file extension.</returns>
    public static bool IsPakFile(string path)
        => string.Equals(System.IO.Path.GetExtension(path), Extension, StringComparison.OrdinalIgnoreCase);
}
