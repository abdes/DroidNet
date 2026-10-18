// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Projects;

/// <summary>
///     Defines a local folder mount point.
/// </summary>
/// <param name="Name">The mount point name (virtual root segment).</param>
/// <param name="Root">The local folder, stored in the manifest as <c>AbsolutePath</c>.</param>
/// <param name="IsExpanded">Whether the project layout shows the mount expanded.</param>
public sealed record LocalFolderMount(
    string Name,
    [property: JsonPropertyName("AbsolutePath")] FileSystemPath Root,
    bool IsExpanded = true);
