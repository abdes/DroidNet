// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.Inspector.Scripting;

/// <summary>A scene node offered by a node parameter picker.</summary>
/// <param name="Id">The node identity.</param>
/// <param name="Name">The node name.</param>
/// <param name="Path">The node's position in the hierarchy, such as <c>Level/Doors/Front</c>.</param>
public sealed record ScriptNodeChoice(Guid Id, string Name, string Path)
{
    /// <inheritdoc />
    public override string ToString() => this.Name;
}
