// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.OutputConsole;

/// <summary>
///     Event args carrying a boolean state change for simple toggle events.
/// </summary>
public sealed class ToggleEventArgs(bool isOn) : EventArgs
{
    /// <summary>
    ///     Gets a value indicating whether the new toggle state is ON or OFF.
    /// </summary>
    public bool IsOn { get; } = isOn;
}
