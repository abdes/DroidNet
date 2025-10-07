// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

#pragma warning disable SA1402 // File may only contain a single type

using Microsoft.UI.Xaml;

namespace DroidNet.Controls.Menus;

/// <summary>
///     Provides a hosting surface for cascading menu levels decoupled from their visual implementation.
/// </summary>
internal interface ICascadedMenuHost : IDisposable, ICascadedMenuSurface
{
    /// <summary>
    ///     Raised synchronously before UI is materialized.
    /// </summary>
    public event EventHandler? Opening;

    /// <summary>
    ///     Raised synchronously once the host is visible.
    /// </summary>
    public event EventHandler? Opened;

    /// <summary>
    ///     Raised synchronously when dismissal is requested; handlers may cancel.
    /// </summary>
    public event EventHandler<MenuHostClosingEventArgs>? Closing;

    /// <summary>
    ///     Raised synchronously after the host has been dismissed.
    /// </summary>
    public event EventHandler? Closed;

    /// <summary>
    ///     Gets the realized cascading surface used for controller-driven interactions.
    /// </summary>
    public ICascadedMenuSurface Surface { get; }

    /// <summary>
    ///     Gets the element the host is currently anchored to, if any.
    /// </summary>
    public FrameworkElement? Anchor { get; }

    /// <summary>
    ///     Gets or sets the root menu surface correlated with this host.
    /// </summary>
    public IRootMenuSurface? RootSurface { get; set; }

    /// <summary>
    ///     Gets or sets the menu data projected into the host.
    /// </summary>
    public IMenuSource? MenuSource { get; set; }

    /// <summary>
    ///     Gets or sets the maximum height applied to each visible menu level.
    /// </summary>
    public double MaxLevelHeight { get; set; }

    /// <summary>
    ///     Gets a value indicating whether the host currently displays menu levels.
    /// </summary>
    public bool IsOpen { get; }

    /// <summary>
    ///     Displays the cascading menu relative to the supplied anchor element.
    /// </summary>
    /// <param name="anchor">The menu item used as the positional anchor.</param>
    /// <param name="navigationMode">The navigation mode that triggered the request.</param>
    public void ShowAt(MenuItem anchor, MenuNavigationMode navigationMode);
}

/// <summary>
///     Arguments for the <see cref="ICascadedMenuHost.Closing"/> event.
/// </summary>
internal sealed class MenuHostClosingEventArgs(MenuDismissKind kind) : EventArgs
{
    /// <summary>
    ///     Gets the dismissal kind that triggered the closing sequence.
    /// </summary>
    public MenuDismissKind Kind { get; } = kind;

    /// <summary>
    ///     Gets or sets a value indicating whether the host should abort closing.
    /// </summary>
    public bool Cancel { get; set; }
}
