// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.OutputConsole;

/// <summary>
///     Partial declarations for <see cref="OutputConsoleView" /> related to public events,
///     that consumers can subscribe to, to observe user actions.
/// </summary>
public partial class OutputConsoleView
{
    /// <summary>
    ///     Occurs when the user requests the view to be cleared (for example, by pressing the Clear button).
    ///     Handlers may be <see langword="null" /> when no subscribers are attached.
    /// </summary>
    public event EventHandler? ClearRequested;

    /// <summary>
    ///     Occurs when the FollowTail option changes state.
    ///     The event argument indicates the new state via <see cref="ToggleEventArgs.IsOn" />.
    ///     Handlers may be <see langword="null" /> when no subscribers are attached.
    /// </summary>
    public event EventHandler<ToggleEventArgs>? FollowTailChanged;

    /// <summary>
    ///     Occurs when the Pause option changes state.
    ///     The event argument indicates the new state via <see cref="ToggleEventArgs.IsOn" />.
    ///     Handlers may be <see langword="null" /> when no subscribers are attached.
    /// </summary>
    public event EventHandler<ToggleEventArgs>? PauseChanged;
}
