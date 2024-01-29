// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.UI;

using Microsoft.UI.Xaml;

/// <summary>
/// An extended <see cref="RouterContext" />, which keeps track of the
/// <see cref="Window" /> associated with it.
/// </summary>
/// <param name="target">The context's target name.</param>
/// <param name="window">
/// The <see cref="Window" /> associated with this context.
/// </param>
internal sealed class WindowRouterContext(string target, Window window) : RouterContext(target)
{
    // Used for debugging. We want to keep track of the last known window title
    // because when the window is closed, we cannot get its Title property
    // anymore.
    private string windowTitle = window.Title;

    /// <summary>
    /// Gets the <see cref="Window" /> associated with this context.
    /// </summary>
    /// <value>
    /// The <see cref="Window" /> associated with this context.
    /// </value>
    internal Window Window { get; } = window;

    /// <inheritdoc />
    public override string ToString()
    {
        try
        {
            this.windowTitle = this.Window.Title;
        }
        catch
        {
            // ignored, we'll use the last known value of the window's title
        }

        return $"{this.Target}:{this.Window.GetType().Name}('{this.windowTitle}')";
    }
}
