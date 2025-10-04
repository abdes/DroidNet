// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

/// <summary>
/// Describes the reason for dismissing the active menu chain.
/// </summary>
public enum MenuDismissKind
{
    /// <summary>
    /// Collapse triggered by keyboard escape or equivalent command.
    /// </summary>
    KeyboardInput,

    /// <summary>
    /// Collapse triggered by pointer dismissal (click outside the menu).
    /// </summary>
    PointerInput,

    /// <summary>
    /// Collapse triggered by programmatic request or completed command execution.
    /// </summary>
    Programmatic,

    /// <summary>
    /// Collapse triggered when leaving mnemonic mode.
    /// </summary>
    MnemonicExit,
}
