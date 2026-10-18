// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Layout;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Tests.Nodes;

/// <summary>A scope a new node is appended to.</summary>
/// <param name="Parent">The scene parent, or <see langword="null"/> for the root.</param>
/// <param name="Folder">The explorer folder that holds the entry, or <see langword="null"/> for the scope itself.</param>
public sealed record AppendCase(NodeId? Parent, FolderId? Folder);
