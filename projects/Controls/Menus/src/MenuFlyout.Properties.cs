// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;

namespace DroidNet.Controls.Menus;

/// <summary>
///     Custom flyout surface that renders menu data using <see cref="ColumnPresenter"/> columns.
///     This implementation keeps interaction logic reusable across menu containers via
///     <see cref="MenuInteractionController"/>.
/// </summary>
public sealed partial class MenuFlyout
{
    /// <summary>
    ///     Identifies the <see cref="MenuSource"/> dependency property.
    /// </summary>
    public static readonly DependencyProperty MenuSourceProperty = DependencyProperty.Register(
        nameof(MenuSource),
        typeof(IMenuSource),
        typeof(MenuFlyout),
        new PropertyMetadata(defaultValue: null));

    /// <summary>
    ///     Identifies the <see cref="MaxColumnHeight"/> dependency property.
    /// </summary>
    public static readonly DependencyProperty MaxColumnHeightProperty = DependencyProperty.Register(
        nameof(MaxColumnHeight),
        typeof(double),
        typeof(MenuFlyout),
        new PropertyMetadata(480d));

    /// <summary>
    ///     Identifies the <see cref="RootSurface"/> dependency property.
    /// </summary>
    public static readonly DependencyProperty RootSurfaceProperty = DependencyProperty.Register(
        nameof(RootSurface),
        typeof(IRootMenuSurface),
        typeof(MenuFlyout),
        new PropertyMetadata(defaultValue: null));

    /// <summary>
    ///     Gets or sets the menu source consumed by the flyout.
    /// </summary>
    /// <remarks>
    ///     Changes to this property value are only observed when the flyout is dismissed and opened again.
    /// </remarks>
    public IMenuSource? MenuSource
    {
        get => (IMenuSource?)this.GetValue(MenuSourceProperty);
        set => this.SetValue(MenuSourceProperty, value);
    }

    /// <summary>
    ///     Gets or sets the maximum height applied to each visible column.
    /// </summary>
    /// <remarks>
    ///     Changes to this property value are only observed when the flyout is dismissed and opened again.
    /// </remarks>
    public double MaxColumnHeight
    {
        get => (double)this.GetValue(MaxColumnHeightProperty);
        set => this.SetValue(MaxColumnHeightProperty, value);
    }

    /// <summary>
    ///     Gets or sets the root interaction surface coordinating the menu bar associated with this flyout.
    /// </summary>
    /// <remarks>
    ///     Changes to this property value are only observed when the flyout is dismissed and opened again.
    /// </remarks>
    internal IRootMenuSurface? RootSurface
    {
        get => (IRootMenuSurface?)this.GetValue(RootSurfaceProperty);
        set => this.SetValue(RootSurfaceProperty, value);
    }
}
