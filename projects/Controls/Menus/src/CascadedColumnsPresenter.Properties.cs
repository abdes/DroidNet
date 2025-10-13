// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;

namespace DroidNet.Controls.Menus;

/// <summary>
///     Presenter that renders one or more cascading menu levels for any <see cref="ICascadedMenuHost"/>.
/// </summary>
public sealed partial class CascadedColumnsPresenter
{
    /// <summary>
    ///     Identifies the <see cref="MenuSource"/> dependency property.
    /// </summary>
    public static readonly DependencyProperty MenuSourceProperty = DependencyProperty.Register(
        nameof(MenuSource),
        typeof(IMenuSource),
        typeof(CascadedColumnsPresenter),
        new PropertyMetadata(defaultValue: null, new PropertyChangedCallback(OnMenuSourcePropertyChanged)));

    /// <summary>
    ///     Identifies the <see cref="MaxColumnHeight"/> dependency property.
    /// </summary>
    public static readonly DependencyProperty MaxColumnHeightProperty = DependencyProperty.Register(
        nameof(MaxColumnHeight),
        typeof(double),
        typeof(CascadedColumnsPresenter),
        new PropertyMetadata(480d));

    /// <summary>
    ///     Identifies the <see cref="RootSurface"/> dependency property.
    /// </summary>
    public static readonly DependencyProperty RootSurfaceProperty = DependencyProperty.Register(
        nameof(RootSurface),
        typeof(IRootMenuSurface),
        typeof(CascadedColumnsPresenter),
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

    /// <summary>
    /// Static callback invoked when <see cref="MenuSourceProperty"/> changes.
    /// </summary>
    private static void OnMenuSourcePropertyChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is CascadedColumnsPresenter presenter)
        {
            presenter.OnMenuSourceChanged(e.OldValue as IMenuSource, e.NewValue as IMenuSource);
        }
    }

    /// <summary>
    /// Instance handler for menu source changes.
    /// </summary>
    /// <param name="oldSource">Previous menu source value (may be null).</param>
    /// <param name="newSource">New menu source value (may be null).</param>
    private void OnMenuSourceChanged(IMenuSource? oldSource, IMenuSource? newSource)
    {
        _ = oldSource; // unused
        _ = newSource; // unused

        this.LogMenuSourceChanged();

        this.Reset();
    }
}
