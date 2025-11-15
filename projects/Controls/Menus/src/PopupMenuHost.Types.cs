// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls.Primitives;

namespace DroidNet.Controls.Menus;

/// <summary>
///     <see cref="ICascadedMenuHost"/> implementation backed by <see cref="Popup"/>.
///     Acts as its own <see cref="ICascadedMenuSurface"/>.
/// </summary>
internal sealed partial class PopupMenuHost
{
    private enum PopupLifecycleState
    {
        Idle,
        PendingOpen,
        Opening,
        Open,
        Closing,
    }

    private readonly struct PopupRequest(int token, FrameworkElement anchor, MenuNavigationMode navigationMode, Windows.Foundation.Point? customPosition = null)
    {
        public int Token { get; } = token;

        public FrameworkElement Anchor { get; } = anchor;

        public MenuNavigationMode NavigationMode { get; } = navigationMode;

        public Windows.Foundation.Point? CustomPosition { get; } = customPosition;
    }
}
