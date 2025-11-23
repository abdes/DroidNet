// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;

namespace DroidNet.Controls.Menus;

/// <summary>
///     A button control that shows a menu when clicked and acts as a root surface for keyboard navigation.
/// </summary>
public sealed partial class MenuButton
{
    /// <summary>
    ///     Identifies the <see cref="MenuSource"/> dependency property.
    /// </summary>
    public static readonly DependencyProperty MenuSourceProperty = DependencyProperty.Register(
        nameof(MenuSource),
        typeof(IMenuSource),
        typeof(MenuButton),
        new PropertyMetadata(defaultValue: null, OnMenuSourceChanged));

    /// <summary>
    ///     Identifies the <see cref="MaxMenuHeight"/> dependency property.
    /// </summary>
    public static readonly DependencyProperty MaxMenuHeightProperty = DependencyProperty.Register(
        nameof(MaxMenuHeight),
        typeof(double),
        typeof(MenuButton),
        new PropertyMetadata(480d));

    /// <summary>
    ///     Identifies the <see cref="Chrome"/> dependency property.
    /// </summary>
    public static readonly DependencyProperty ChromeProperty = DependencyProperty.Register(
        nameof(Chrome),
        typeof(MenuButtonChrome),
        typeof(MenuButton),
        new PropertyMetadata(MenuButtonChrome.Default, OnChromeChanged));

    /// <summary>
    ///     Gets or sets the menu source that defines the menu items to display.
    /// </summary>
    public IMenuSource? MenuSource
    {
        get => (IMenuSource?)this.GetValue(MenuSourceProperty);
        set => this.SetValue(MenuSourceProperty, value);
    }

    /// <summary>
    ///     Gets or sets the maximum height for menu levels.
    /// </summary>
    public double MaxMenuHeight
    {
        get => (double)this.GetValue(MaxMenuHeightProperty);
        set => this.SetValue(MaxMenuHeightProperty, value);
    }

    /// <summary>
    ///     Gets or sets the visual chrome variant used by the button.
    /// </summary>
    public MenuButtonChrome Chrome
    {
        get => (MenuButtonChrome)this.GetValue(ChromeProperty);
        set => this.SetValue(ChromeProperty, value);
    }

    private static void OnMenuSourceChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is MenuButton button)
        {
            // Invalidate any cached snapshot of the button item so SubItems are rebuilt
            // from the new MenuSource when next accessed.
            button.buttonItemData = null;

            _ = button.menuHost?.MenuSource = e.NewValue as IMenuSource;
        }
    }

    private static void OnChromeChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is MenuButton button)
        {
            button.ApplyChromeStyle();
        }
    }
}
