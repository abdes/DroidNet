// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Tests.Edits.Hierarchy;

/// <summary>A reparent in which one participant is locked.</summary>
/// <param name="Moved">The nodes to move.</param>
/// <param name="Parent">The destination parent, or <see langword="null"/>.</param>
/// <param name="After">The sibling the nodes go after, or <see langword="null"/>.</param>
/// <param name="Locked">The locked participant.</param>
public sealed record LockedParticipantCase(IReadOnlyList<NodeId> Moved, NodeId? Parent, NodeId? After, NodeId Locked);
