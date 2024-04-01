// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger.UI.WorkSpace;

using DroidNet.Docking;

internal static class DockGroupExtensions
{
    public static bool ShouldShowGroup(this IDockGroup? group)
    {
        if (group is null)
        {
            return false;
        }

        if (!group.HasNoDocks)
        {
            return group is IDockTray || group.Docks.Any(d => d.State != DockingState.Minimized);
        }

        return ShouldShowGroup(group.First) || ShouldShowGroup(group.Second);
    }

    public static bool ShouldStretch(this IDockGroup group)
    {
        if (group.IsCenter)
        {
            return true;
        }

        return (group.First != null && ShouldStretch(group.First)) ||
               (group.Second != null && ShouldStretch(group.Second));
    }
}
