// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Utils;

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

    public static FlowDirection ToFlowDirection(this DockGroupOrientation orientation)
        => orientation switch
        {
            DockGroupOrientation.Undetermined => throw new ArgumentException(
                $"when orientation is {orientation}, there is no automatic mapping to a {nameof(FlowDirection)}"),
            DockGroupOrientation.Horizontal => FlowDirection.LeftToRight,
            DockGroupOrientation.Vertical => FlowDirection.TopToBottom,
            _ => throw new InvalidEnumArgumentException(
                nameof(orientation),
                (int)orientation,
                typeof(DockGroupOrientation)),
        };
}
