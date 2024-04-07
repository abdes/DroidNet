// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Layouts.GridFlow;

using DroidNet.Docking;

internal static class DockGroupExtensions
{
    public static bool ShouldStretch(this IDockGroup group)
    {
        if (group.IsCenter)
        {
            return true;
        }

        return group.First?.ShouldStretch() == true || group.Second?.ShouldStretch() == true;
    }
}
