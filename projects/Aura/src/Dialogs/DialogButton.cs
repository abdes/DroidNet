// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Aura.Dialogs;

/// <summary>
///     Represents the button the user selected to dismiss a dialog.
/// </summary>
public enum DialogButton
{
    /// <summary>
    ///     The dialog did not report a specific button (e.g., dismissed via close, escape, or programmatic hide).
    /// </summary>
    None = 0,

    /// <summary>
    ///     The primary button.
    /// </summary>
    Primary = 1,

    /// <summary>
    ///     The secondary button.
    /// </summary>
    Secondary = 2,

    /// <summary>
    ///     The close button.
    /// </summary>
    Close = 3,
}
