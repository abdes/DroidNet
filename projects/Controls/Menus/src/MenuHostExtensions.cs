// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Input;

namespace DroidNet.Controls.Menus;

/// <summary>
///     Extension methods for <see cref="ICascadedMenuHost"/>.
/// </summary>
internal static class MenuHostExtensions
{
    /// <summary>
    ///     Sets up initial keyboard navigation for a menu host by focusing the root element
    ///     and attaching a one-time KeyDown handler that focuses the first item on Up/Down arrow.
    /// </summary>
    /// <param name="host">The menu host to set up.</param>
    public static void SetupInitialKeyboardNavigation(this ICascadedMenuHost host)
    {
        // Give focus to the presenter so it can receive keyboard input
        var rootElement = host.RootElement;
        var focusResult = rootElement.Focus(FocusState.Programmatic);

        // Attach a one-time KeyDown handler for initial arrow key navigation
        rootElement.KeyDown += OnInitialKeyDown;

        void OnInitialKeyDown(object s, KeyRoutedEventArgs args)
        {
            var key = args.Key;

            // On first Up or Down arrow, focus the first item
            if (key is Windows.System.VirtualKey.Up or Windows.System.VirtualKey.Down)
            {
                var handled = host.Surface.FocusFirstItem(MenuLevel.First, MenuNavigationMode.KeyboardInput);
                if (handled)
                {
                    args.Handled = true;

                    // Detach this handler after first use - controller takes over from here
                    rootElement.KeyDown -= OnInitialKeyDown;
                }
            }
        }
    }
}
