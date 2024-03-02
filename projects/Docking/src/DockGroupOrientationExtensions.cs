// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking;

using System.ComponentModel;

public static class DockGroupOrientationExtensions
{
    public static string ToSymbol(this DockGroupOrientation variable)
        => variable switch
        {
            DockGroupOrientation.Undetermined => "?",
            DockGroupOrientation.Horizontal => "\u2194",
            DockGroupOrientation.Vertical => "\u2195",
            _ => throw new InvalidEnumArgumentException(),
        };
}
