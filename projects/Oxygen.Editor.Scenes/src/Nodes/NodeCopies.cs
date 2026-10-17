// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Nodes;

/// <summary>Copies of node subtrees, with the ids they were given.</summary>
/// <param name="Roots">The copied subtrees, in the order of their sources.</param>
/// <param name="Ids">Maps each source node id, at any depth, to its copy's id.</param>
public sealed record NodeCopies(IReadOnlyList<SceneNodeData> Roots, IReadOnlyDictionary<NodeId, NodeId> Ids);
