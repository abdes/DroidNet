// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking;

using DroidNet.Docking.Detail;

public class CenterDock : Dock
{
    public override bool CanMinimize => false;

    public override bool CanClose => false;

    public static CenterDock New() => (CenterDock)Factory.CreateDock(typeof(CenterDock));
}
