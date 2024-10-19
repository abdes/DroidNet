// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.OutputLog.Rendering;

using Serilog.Parsing;

internal static class AlignmentExtensions
{
    public static Alignment Widen(this Alignment alignment, int amount)
        => new(alignment.Direction, alignment.Width + amount);
}
