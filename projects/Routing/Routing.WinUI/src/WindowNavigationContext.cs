// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;

namespace DroidNet.Routing.WinUI;

/// <summary>
/// Represents a navigation context bound to a WinUI window, providing the environment
/// where route activation and content loading occurs.
/// </summary>
/// <remarks>
/// <para>
/// A window navigation context associates router state and route activation with a specific WinUI
/// window instance. Created by the <see cref="WindowContextProvider"/>, it enables the router to
/// manage multiple independent navigation hierarchies, each tied to its own window.
/// </para>
/// <para>
/// The context maintains a reference to its window, which must implement <see cref="IOutletContainer"/>
/// to host the root-level content. During navigation, the router activates routes within this context,
/// loading content either directly into the window (for root routes) or into outlets within view
/// models (for child routes).
/// </para>
/// </remarks>
internal sealed class WindowNavigationContext : NavigationContext
{
    /// <summary>
    /// Used for debugging. We want to keep track of the last known window title
    /// because when the window is closed, we cannot get its Title property
    /// anymore.
    /// </summary>
    private string windowTitle;

    private bool windowClosed;

    /// <summary>
    /// Initializes a new instance of the <see cref="WindowNavigationContext"/> class.
    /// </summary>
    /// <param name="targetKey">The target key for the navigation happening within this context.</param>
    /// <param name="window">The window instance this context is bound to.</param>
    /// <param name="fromTarget">The source target window from which this navigation originated, if any.</param>
    /// <param name="replaceTarget">Whether the source target should be closed when this context is activated.</param>
    /// <exception cref="ArgumentNullException">
    /// Thrown if <paramref name="window"/> is <see langword="null"/>.
    /// </exception>
    public WindowNavigationContext(Target targetKey, Window window, object? fromTarget = null, bool replaceTarget = false)
        : base(targetKey, window, fromTarget, replaceTarget)
    {
        this.windowTitle = window.Title;
        window.Closed += (_, _) => this.windowClosed = true;
    }

    /// <summary>
    /// Gets the WinUI window instance associated with this navigation context.
    /// </summary>
    /// <value>
    /// The window that hosts the root content for this navigation context. This window
    /// must implement <see cref="IOutletContainer"/> to receive activated content.
    /// </value>
    public Window Window => (Window)this.NavigationTarget;

    /// <inheritdoc />
    public override string ToString()
    {
        if (!this.windowClosed)
        {
            this.windowTitle = this.Window.Title;
        }

        return $"{this.NavigationTargetKey}:{this.NavigationTarget.GetType().Name}('{this.windowTitle}')";
    }
}
