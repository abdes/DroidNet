// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;

namespace DroidNet.Controls.Menus;

/// <summary>
///     Provides extension methods for converting between <see cref="MenuNavigationMode"/> and
///     <see cref="MenuInteractionInputSource"/>.
/// </summary>
public static class EnumExtensions
{
    /// <summary>
    ///     Converts a <see cref="MenuNavigationMode"/> to a corresponding <see cref="MenuInteractionInputSource"/>.
    /// </summary>
    /// <param name="navigationMode">The navigation mode to convert.</param>
    /// <returns>
    ///     The corresponding <see cref="MenuInteractionInputSource"/>. If an unrecognized value is supplied,
    ///     <see cref="MenuInteractionInputSource.Programmatic"/> is returned as a safe fallback.
    /// </returns>
    public static MenuInteractionInputSource ToInputSource(this MenuNavigationMode navigationMode)
        => navigationMode switch
        {
            MenuNavigationMode.PointerInput => MenuInteractionInputSource.PointerInput,
            MenuNavigationMode.KeyboardInput => MenuInteractionInputSource.KeyboardInput,
            MenuNavigationMode.Programmatic => MenuInteractionInputSource.Programmatic,
            _ => MenuInteractionInputSource.Programmatic,
        };

    /// <summary>
    ///     Converts a <see cref="MenuInteractionInputSource"/> to a corresponding <see cref="MenuNavigationMode"/>.
    /// </summary>
    /// <param name="inputSource">The input source to convert.</param>
    /// <returns>
    ///     The corresponding <see cref="MenuNavigationMode"/>. If an unrecognized value is supplied,
    ///     <see cref="MenuNavigationMode.Programmatic"/> is returned as a safe fallback.
    /// </returns>
    public static MenuNavigationMode ToNavigationMode(this MenuInteractionInputSource inputSource)
        => inputSource switch
        {
            MenuInteractionInputSource.PointerInput => MenuNavigationMode.PointerInput,
            MenuInteractionInputSource.KeyboardInput => MenuNavigationMode.KeyboardInput,
            MenuInteractionInputSource.Programmatic => MenuNavigationMode.Programmatic,
            _ => MenuNavigationMode.Programmatic,
        };

    /// <summary>
    ///     Converts a <see cref="MenuDismissKind"/> to a corresponding <see cref="MenuInteractionInputSource"/>.
    /// </summary>
    /// <param name="dismissKind">The dismiss kind to convert.</param>
    /// <returns>
    ///     The corresponding <see cref="MenuInteractionInputSource"/>. Special-case: <see cref="MenuDismissKind.MnemonicExit"/>
    ///     is treated as <see cref="MenuInteractionInputSource.KeyboardInput"/>. For unrecognized values,
    ///     <see cref="MenuInteractionInputSource.Programmatic"/> is returned.
    /// </returns>
    public static MenuInteractionInputSource ToInputSource(this MenuDismissKind dismissKind)
        => dismissKind switch
        {
            MenuDismissKind.PointerInput => MenuInteractionInputSource.PointerInput,
            MenuDismissKind.KeyboardInput => MenuInteractionInputSource.KeyboardInput,
            MenuDismissKind.Programmatic => MenuInteractionInputSource.Programmatic,
            MenuDismissKind.MnemonicExit => MenuInteractionInputSource.KeyboardInput,
            _ => MenuInteractionInputSource.Programmatic,
        };

    /// <summary>
    ///     Converts a <see cref="MenuNavigationMode"/> to a corresponding <see cref="FocusState"/>.
    /// </summary>
    /// <param name="navigationMode">The navigation mode to convert.</param>
    /// <returns>
    ///     The corresponding <see cref="FocusState"/>. If an unrecognized value is supplied,
    ///     <see cref="FocusState.Programmatic"/> is returned as a safe fallback.
    /// </returns>
    public static FocusState ToFocusState(this MenuNavigationMode navigationMode)
        => navigationMode switch
        {
            MenuNavigationMode.PointerInput => FocusState.Pointer,
            MenuNavigationMode.KeyboardInput => FocusState.Keyboard,
            MenuNavigationMode.Programmatic => FocusState.Programmatic,
            _ => FocusState.Programmatic,
        };
}
