// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Scenes.Edits.Layout;

/// <summary>Where one layout entry goes.</summary>
/// <param name="Entry">The entry.</param>
/// <param name="At">Its place.</param>
public sealed record LayoutEntryPosition(LayoutEntryId Entry, LayoutPosition At);
