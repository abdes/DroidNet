// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Runtime.InteropServices;
using Oxygen.Editor.World;

namespace Oxygen.Editor.WorldEditor.Messages;

/// <summary>
/// Snapshot of geometry selection for messaging/undo purposes.
/// </summary>
[StructLayout(LayoutKind.Auto)]
[System.Diagnostics.CodeAnalysis.SuppressMessage("StyleCop.CSharp.DocumentationRules", "SA1649:File name should match first type name", Justification = "Auxiliary type in message file")]
public readonly record struct GeometrySnapshot(string? UriString);

/// <summary>
/// Message sent when one or more <see cref="SceneNode"/> geometry values have
/// been applied by a UI editor.
/// </summary>
internal sealed class SceneNodeGeometryAppliedMessage(IList<SceneNode> nodes, IList<GeometrySnapshot> oldValues, IList<GeometrySnapshot> newValues, string property)
{
    public IList<SceneNode> Nodes { get; } = nodes;
    public IList<GeometrySnapshot> OldValues { get; } = oldValues;
    public IList<GeometrySnapshot> NewValues { get; } = newValues;
    public string Property { get; } = property;
}
