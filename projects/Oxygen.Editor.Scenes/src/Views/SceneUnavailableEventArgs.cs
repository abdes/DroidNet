// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Scenes.Views;

/// <summary>Tells that the scene the window was asked to show did not open.</summary>
/// <param name="canReloadPrevious">Whether the scene the window showed before can be shown again.</param>
public sealed class SceneUnavailableEventArgs(bool canReloadPrevious) : EventArgs
{
    /// <summary>Gets a value indicating whether the scene the window showed before can be shown again.</summary>
    public bool CanReloadPrevious { get; } = canReloadPrevious;
}
