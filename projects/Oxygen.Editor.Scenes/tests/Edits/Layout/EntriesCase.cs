// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Edits.Layout;

namespace Oxygen.Editor.Scenes.Tests.Edits.Layout;

/// <summary>A container and its entries.</summary>
/// <param name="Container">Any place in the container.</param>
/// <param name="Expected">The container's entries, in order.</param>
public sealed record EntriesCase(LayoutPosition Container, IReadOnlyList<LayoutEntryId> Expected);
