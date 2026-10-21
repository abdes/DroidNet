// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Scenes.Views;

/// <summary>
/// Whether the window shows the scene it was asked for: a scene that does not open replaces the one shown, and the
/// window can go back to it.
/// </summary>
/// <remarks>The window's scene document lifetime implements it; the Scene Explorer shows the notice and its action.</remarks>
public interface ISceneAvailability
{
    /// <summary>Occurs when the scene the window was asked to show did not open.</summary>
    public event EventHandler<SceneUnavailableEventArgs>? SceneUnavailable;

    /// <summary>Opens the scene the window showed before a replacement that failed.</summary>
    /// <returns><see langword="true"/> when that scene is shown again.</returns>
    public Task<bool> ReloadPreviousAsync();
}
