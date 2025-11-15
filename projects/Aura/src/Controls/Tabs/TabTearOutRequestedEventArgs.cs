// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Aura.Controls;

/// <summary>
///     Event arguments raised when the user drops a tab outside any <see cref="TabStrip"/>. The
///     application is expected to create a new window and host the torn-out <see cref="TabItem"/>.
/// </summary>
/// <remarks>
///     Raised on the UI thread. The <see cref="ScreenDropPoint"/> contains the drop location in
///     screen coordinates and can be used as a position hint for the new window. Handlers should be
///     fast and handle cases where the application chooses not to create a window.
/// </remarks>
public sealed class TabTearOutRequestedEventArgs : EventArgs
{
    /// <summary>
    ///     Gets the logical <see cref="TabItem"/> that was torn out.
    /// </summary>
    /// <value>
    ///     The <see cref="TabItem"/> instance that was dropped outside a <see cref="TabStrip"/>.
    ///     This value is non-null when the event is raised.
    /// </value>
    public TabItem Item { get; init; } = null!;

    /// <summary>
    ///     Gets the screen coordinate point where the drop occurred.
    /// </summary>
    /// <value>
    ///     The drop location in device-independent screen coordinates (XAML logical pixels). The
    ///     value is intended as a hint for new windows; callers should consider monitor DPI and
    ///     window chrome when positioning a newly created window.
    /// </value>
    public Windows.Foundation.Point ScreenDropPoint { get; init; }
}
