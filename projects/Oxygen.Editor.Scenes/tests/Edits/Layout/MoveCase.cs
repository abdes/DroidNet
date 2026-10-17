// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Edits.Layout;

namespace Oxygen.Editor.Scenes.Tests.Edits.Layout;

/// <summary>A move within a scope, and the container whose order it changes.</summary>
/// <param name="Entries">The entries to move.</param>
/// <param name="To">The destination.</param>
/// <param name="Container">Any place in the destination container.</param>
/// <param name="Expected">The destination container's entries after the move, in order.</param>
public sealed record MoveCase(IReadOnlyList<LayoutEntryId> Entries, LayoutPosition To, LayoutPosition Container, IReadOnlyList<LayoutEntryId> Expected);
