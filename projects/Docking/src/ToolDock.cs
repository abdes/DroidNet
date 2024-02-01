// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking;

using DroidNet.Docking.Detail;

public class ToolDock : Dock
{
    protected ToolDock()
    {
    }

    public static ToolDock? New() => Factory.CreateDock(typeof(ToolDock)) as ToolDock;
}
