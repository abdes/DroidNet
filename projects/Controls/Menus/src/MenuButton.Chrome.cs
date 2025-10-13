// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;

namespace DroidNet.Controls.Menus;

/// <summary>
///     Defines the visual chrome variants supported by <see cref="MenuButton"/>.
/// </summary>
public enum MenuButtonChrome
{
    /// <summary>
    ///     Uses the standard WinUI button visual chrome.
    /// </summary>
    Default,

    /// <summary>
    ///     Uses a transparent chrome with clipped hover and pressed states.
    /// </summary>
    Transparent,
}

/// <summary>
///     A button control that shows a menu when clicked and acts as a root surface for keyboard navigation.
/// </summary>
public sealed partial class MenuButton
{
    private static readonly Uri MenuButtonStylesUri = new("ms-appx:///DroidNet.Controls.Menus/MenuButton.xaml");
    private static ResourceDictionary? chromeStyles;

    private static Style? GetChromeStyle(string key)
    {
        chromeStyles ??= new ResourceDictionary { Source = MenuButtonStylesUri };

        return chromeStyles.TryGetValue(key, out var resource) && resource is Style style ? style : null;
    }

    private void ApplyChromeStyle()
    {
        var targetKey = this.Chrome switch
        {
            MenuButtonChrome.Transparent => "MenuButtonTransparentStyle",
            _ => "DefaultMenuButtonStyle",
        };

        if (GetChromeStyle(targetKey) is not { } style)
        {
            return;
        }

        if (!ReferenceEquals(this.Style, style))
        {
            this.Style = style;
        }
    }
}
