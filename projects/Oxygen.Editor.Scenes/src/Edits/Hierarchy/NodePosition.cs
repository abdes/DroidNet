// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Edits.Hierarchy;

/// <summary>A place in the scene graph: a parent and a position among its children.</summary>
/// <param name="Parent">The parent node, or <see langword="null"/> for the scene's root nodes.</param>
/// <param name="Index">The position among the parent's children, in <c>[0, count]</c>; the count appends.</param>
public sealed record NodePosition(NodeId? Parent, int Index);
