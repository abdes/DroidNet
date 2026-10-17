// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Tests.Edits.Hierarchy;

/// <summary>Where a node is inserted: under a scene parent, in an explorer folder, at the start or the end.</summary>
/// <param name="Parent">The scene parent, or <see langword="null"/> for the root.</param>
/// <param name="Folder">The explorer folder that holds the entry, or <see langword="null"/> for the parent's scope itself.</param>
/// <param name="AtStart">Whether the node goes first among the siblings and first in the container, rather than last.</param>
public sealed record InsertCase(NodeId? Parent, FolderId? Folder, bool AtStart);
