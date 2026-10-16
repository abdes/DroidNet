// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.WorldEditor.Documents.Commands;

/// <summary>Owned snapshot of a whole node/folder selection, retaining its visual hierarchy.</summary>
/// <param name="Entries">Top-level selected visual entries, in selection order.</param>
/// <param name="Nodes">Top-level scene hierarchies referenced by those entries.</param>
/// <param name="WorldNodes">Optional representable world-pose snapshots for explicit preserve-world paste.</param>
public sealed record SceneExplorerClipboard(IReadOnlyList<ExplorerEntryData> Entries,
    IReadOnlyList<SceneNodeData> Nodes, IReadOnlyList<SceneNodeData>? WorldNodes = null);
