// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI;

namespace Oxygen.Editor.World.Documents;

/// <summary>Reports that a retired scene has no installed replacement in the owning workspace.</summary>
/// <param name="windowId">The workspace window.</param>
/// <param name="canReloadPrevious">Whether a saved previous scene can be explicitly reloaded.</param>
internal sealed class SceneUnavailableMessage(WindowId windowId, bool canReloadPrevious)
{
    /// <summary>Gets the owning workspace.</summary>
    public WindowId WindowId { get; } = windowId;

    /// <summary>Gets whether the previous saved scene can be recovered.</summary>
    public bool CanReloadPrevious { get; } = canReloadPrevious;
}
