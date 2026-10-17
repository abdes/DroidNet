// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.Scenes.Edits;

/// <summary>Deep copies of serialization records, so an edit owns its data and never shares a list with the scene.</summary>
internal static class SerializationCopies
{
    /// <summary>Copies a node and its subtree.</summary>
    /// <param name="node">The node data.</param>
    /// <returns>The copy.</returns>
    public static SceneNodeData Copy(SceneNodeData node)
        => JsonSerializer.Deserialize(JsonSerializer.SerializeToUtf8Bytes(node, SceneJsonContext.Default.SceneNodeData), SceneJsonContext.Default.SceneNodeData)!;

    /// <summary>Copies a component.</summary>
    /// <param name="component">The component data.</param>
    /// <returns>The copy.</returns>
    public static ComponentData Copy(ComponentData component)
        => JsonSerializer.Deserialize(JsonSerializer.SerializeToUtf8Bytes(component, SceneJsonContext.Default.ComponentData), SceneJsonContext.Default.ComponentData)!;

    /// <summary>Copies an explorer layout entry and its children.</summary>
    /// <param name="entry">The entry.</param>
    /// <returns>The copy.</returns>
    public static ExplorerEntryData Copy(ExplorerEntryData entry)
        => JsonSerializer.Deserialize(JsonSerializer.SerializeToUtf8Bytes(entry, SceneJsonContext.Default.ExplorerEntryData), SceneJsonContext.Default.ExplorerEntryData)!;
}
