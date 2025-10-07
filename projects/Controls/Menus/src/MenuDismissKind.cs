// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.Menus;

/// <summary>
///     Describes the reason for dismissing the active menu chain.
/// </summary>
public enum MenuDismissKind
{
    /// <summary>
    ///     CollapseItem triggered by keyboard escape or equivalent command.
    /// </summary>
    KeyboardInput,

    /// <summary>
    ///     CollapseItem triggered by pointer dismissal (click outside the menu).
    /// </summary>
    PointerInput,

    /// <summary>
    ///     CollapseItem triggered by programmatic request or completed command execution.
    /// </summary>
    Programmatic,

    /// <summary>
    ///     CollapseItem triggered when leaving mnemonic mode.
    /// </summary>
    MnemonicExit,
}
