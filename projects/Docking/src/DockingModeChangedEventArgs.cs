// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking;

/// <summary>
/// Provides data for the <see cref="IDocker.DockingModeChanged"/> event.
/// </summary>
/// <param name="dockBeingDocked">The dock being docked, or <see langword="null"/> when the maneuver ends.</param>
public class DockingModeChangedEventArgs(IDock? dockBeingDocked) : EventArgs
{
    /// <summary>
    /// Gets the dock being docked, or <see langword="null"/> when the docking maneuver ends.
    /// </summary>
    public IDock? DockBeingDocked => dockBeingDocked;
}
