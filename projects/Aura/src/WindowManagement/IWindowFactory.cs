// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;

namespace DroidNet.Aura.WindowManagement;

/// <summary>
/// Defines a factory for creating window instances with dependency injection support.
/// </summary>
/// <remarks>
/// Implementations of this interface are responsible for creating window instances,
/// typically by resolving them from a dependency injection container. This enables
/// proper service injection into window constructors.
/// </remarks>
public interface IWindowFactory
{
    /// <summary>
    /// Creates a new window instance of the specified type.
    /// </summary>
    /// <typeparam name="TWindow">The type of window to create.</typeparam>
    /// <returns>A new instance of the specified window type.</returns>
    /// <exception cref="InvalidOperationException">
    /// Thrown when the window type cannot be resolved or created.
    /// </exception>
    public TWindow CreateWindow<TWindow>()
        where TWindow : Window;

    /// <summary>
    /// Creates a new window instance by type name.
    /// </summary>
    /// <param name="category">The fully qualified type name of the window to create.</param>
    /// <returns>A new window instance.</returns>
    /// <exception cref="ArgumentException">
    /// Thrown when the type name is invalid or doesn't inherit from <see cref="Window"/>.
    /// </exception>
    /// <exception cref="InvalidOperationException">
    /// Thrown when the window type cannot be resolved or created.
    /// </exception>
    public Window CreateWindow(string category);

    /// <summary>
    /// Attempts to create a window instance of the specified type.
    /// </summary>
    /// <typeparam name="TWindow">The type of window to create.</typeparam>
    /// <param name="window">The created window instance, or null if creation failed.</param>
    /// <returns>True if the window was successfully created; otherwise, false.</returns>
    public bool TryCreateWindow<TWindow>(out TWindow? window)
        where TWindow : Window;
}
