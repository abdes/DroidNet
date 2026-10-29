// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Serialization;

namespace Oxygen.Editor.Projects;

/// <summary>
/// An output folder the Content Explorer shows as a read-only node. It is view state, never a mount: nothing is
/// authored in it, cooked from it or passed to the Player as a source.
/// </summary>
/// <param name="Folder">The output folder: <see cref="ProjectOutputFolders.Cooked"/> or <see cref="ProjectOutputFolders.Build"/>.</param>
/// <param name="IsExpanded">Whether the Content Explorer shows the node expanded.</param>
public sealed record OutputFolderView(string Folder, bool IsExpanded = true)
{
    /// <summary>Gets the node's name, which no mount may take.</summary>
    [JsonIgnore]
    public string Name => ProjectOutputFolders.ViewName(this.Folder);
}
