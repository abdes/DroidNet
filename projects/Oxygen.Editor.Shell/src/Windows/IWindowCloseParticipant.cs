// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Shell.Windows;

/// <summary>Takes part in closing windows: asks what must be asked, then does its part of each close.</summary>
/// <remarks>
///     A close's prepare, commit and finalize run on whichever thread Aura's close reached, often not a window's UI thread:
///     a participant dispatches whatever touches a window, its XAML or its <c>AppWindow</c> to the UI thread, through the
///     hosting context's dispatcher.
/// </remarks>
public interface IWindowCloseParticipant
{
    /// <summary>Gets what the participant answers for, which orders it among the other participants.</summary>
    public WindowCloseLevel Level { get; }

    /// <summary>Prepares the close of a window: asks the user what must be asked, without closing anything yet.</summary>
    /// <param name="close">The close.</param>
    /// <returns>
    ///     The participant's part of the close (<see cref="WindowCloseStep.None" /> when it has nothing to do), or
    ///     <see langword="null" /> to keep the window open.
    /// </returns>
    public Task<WindowCloseStep?> PrepareAsync(WindowClose close);
}
