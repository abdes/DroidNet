// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger.UI.WorkSpace;

using DroidNet.Docking;
using DroidNet.Docking.Detail;

/// <summary>
/// Represents a dock that holds only one dockable. Any attempt to add a
/// <see cref="IDockable" /> to it will result in replacing the current one.
/// </summary>
public class ApplicationDock : Dock
{
    protected ApplicationDock()
    {
    }

    public override bool CanMinimize => false;

    public override bool CanClose => false;

    public static ApplicationDock? New() => Factory.CreateDock(typeof(ApplicationDock)) as ApplicationDock;
}
