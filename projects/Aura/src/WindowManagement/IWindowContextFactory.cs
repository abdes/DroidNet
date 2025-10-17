// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;

namespace DroidNet.Aura.WindowManagement;

/// <summary>
/// Factory for creating <see cref="WindowContext"/> instances with proper dependency injection.
/// </summary>
/// <remarks>
/// This factory pattern avoids the service locator anti-pattern by injecting dependencies
/// (logger factory, menu providers) via constructor, enabling proper unit testing and
/// leveraging DryIoc's resolution optimizations.
/// </remarks>
public interface IWindowContextFactory
{
    /// <summary>
    /// Creates a new <see cref="WindowContext"/> for a given window.
    /// </summary>
    /// <param name="window">The window to wrap.</param>
    /// <param name="category">The category of the window. Use constants from <see cref="WindowCategory"/>.</param>
    /// <param name="title">The window title.</param>
    /// <param name="decoration">Optional decoration options for the window.</param>
    /// <param name="metadata">Optional metadata.</param>
    /// <returns>A new <see cref="WindowContext"/> instance.</returns>
    public WindowContext Create(
        Window window,
        WindowCategory category,
        string? title = null,
        Decoration.WindowDecorationOptions? decoration = null,
        IReadOnlyDictionary<string, object>? metadata = null);
}
