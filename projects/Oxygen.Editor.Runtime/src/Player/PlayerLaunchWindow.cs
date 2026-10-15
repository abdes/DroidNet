// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Player;

/// <summary>The player window a launch opens.</summary>
/// <param name="Title">The window title.</param>
/// <param name="Mode">Whether the window is framed or borderless over its display.</param>
/// <param name="Width">The framebuffer width in physical pixels while windowed, 64 to 16384.</param>
/// <param name="Height">The framebuffer height in physical pixels while windowed, 64 to 16384.</param>
/// <param name="VSync">Whether the player presents with vertical sync.</param>
/// <param name="FrameCap">The frame rate limit in frames per second, 1 to 240; 0 is off.</param>
public sealed record PlayerLaunchWindow(string Title, PlayerWindowMode Mode, int Width, int Height, bool VSync, int FrameCap);
