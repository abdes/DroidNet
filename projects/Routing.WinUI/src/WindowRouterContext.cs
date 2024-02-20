// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.WinUI;

using Microsoft.UI.Xaml;

/// <summary>
/// An extended <see cref="RouterContext" />, which keeps track of the
/// <see cref="Window" /> associated with it.
/// </summary>
internal sealed class WindowRouterContext : RouterContext
{
    // Used for debugging. We want to keep track of the last known window title
    // because when the window is closed, we cannot get its Title property
    // anymore.
    private string windowTitle;
    private bool windowClosed;

    /// <summary>
    /// Initializes a new instance of the <see cref="WindowRouterContext" />
    /// class.
    /// </summary>
    /// <param name="target">The context's target name.</param>
    /// <param name="window">
    /// The <see cref="Window" /> associated with this context.
    /// </param>
    public WindowRouterContext(string target, Window window)
        : base(target)
    {
        this.windowTitle = window.Title;
        this.Window = window;
        this.Window.Closed += (_, _) => this.windowClosed = true;
    }

    /// <summary>
    /// Gets the <see cref="Window" /> associated with this context.
    /// </summary>
    /// <value>
    /// The <see cref="Window" /> associated with this context.
    /// </value>
    internal Window Window { get; }

    /// <inheritdoc />
    public override string ToString()
    {
        if (!this.windowClosed)
        {
            this.windowTitle = this.Window.Title;
        }

        return $"{this.Target}:{this.Window.GetType().Name}('{this.windowTitle}')";
    }
}
