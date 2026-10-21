// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Shell.Windows;

/// <summary>
///     Closes the editor's windows and exits the editor through one negotiation: every participant prepares a window's
///     close, then each commits its part, then each finalizes it.
/// </summary>
public interface ICloseAndExit
{
    /// <summary>Makes a participant take part in closing windows until the returned registration is disposed.</summary>
    /// <param name="participant">The participant, such as a window's documents.</param>
    /// <returns>The registration.</returns>
    public IDisposable Join(IWindowCloseParticipant participant);
}
