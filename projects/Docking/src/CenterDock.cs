// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking;

using DroidNet.Docking.Detail;

/// <summary>
/// A type of <see cref="Dock" /> intended for use to dock the central view of the application, usually hosting the application
/// document(s).
/// </summary>
/// <remarks>
/// This special dock cannot be closed or minimized.
/// </remarks>
public partial class CenterDock : Dock
{
    public override bool CanMinimize => false;

    public override bool CanClose => false;

    public static CenterDock New() => (CenterDock)Factory.CreateDock(typeof(CenterDock));
}
