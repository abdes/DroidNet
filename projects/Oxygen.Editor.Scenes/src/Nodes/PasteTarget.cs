// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Nodes;

/// <summary>Where pasted items go: a scene parent, a folder in its scope, and an optional sibling to follow.</summary>
/// <remarks>
/// A folder implies its scope's node; a sibling implies its parent. When more than one is given they must agree on the
/// scene parent.
/// </remarks>
/// <param name="Parent">The scene parent, or <see langword="null"/> for the root.</param>
/// <param name="Folder">The folder the items go in, if any.</param>
/// <param name="After">The node the items go after, if any; otherwise they are appended.</param>
public sealed record PasteTarget(NodeId? Parent = null, FolderId? Folder = null, NodeId? After = null);
