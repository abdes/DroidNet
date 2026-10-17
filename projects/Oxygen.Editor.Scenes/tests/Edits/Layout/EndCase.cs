// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Edits.Layout;

namespace Oxygen.Editor.Scenes.Tests.Edits.Layout;

/// <summary>Entries about to move out of a container, and where its end is for that move.</summary>
/// <param name="Moving">The entries about to move, if any.</param>
/// <param name="Index">The index of the container's end.</param>
public sealed record EndCase(IReadOnlyList<LayoutEntryId>? Moving, int Index);
