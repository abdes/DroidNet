// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Aura.Controls;

/// <summary>
///     Specifies the sizing policy used to determine the width of tabs in a tab strip.
/// </summary>
/// <remarks>
///     Use these policies to control how the tab control allocates horizontal space for individual
///     tabs (for example, whether tabs size to their content, share equal widths, or are compacted
///     to show more tabs).
/// </remarks>
public enum TabWidthPolicy
{
    /// <summary>
    ///     Automatically sizes each tab to fit its content, subject to the available space and the
    ///     tab strip's layout rules. Tabs may grow or shrink depending on their content and the
    ///     control's allowed minimum/maximum widths.
    /// </summary>
    Auto,

    /// <summary>
    ///     Gives every tab the same width so that all tabs appear uniform. The width is typically
    ///     calculated by dividing the available tab area among the number of tabs, and may be
    ///     constrained by configured minimum or maximum widths.
    /// </summary>
    Equal,

    /// <summary>
    ///     Uses a compact sizing strategy that reduces padding and truncates content as needed to
    ///     display as many tabs as possible. This is useful when the tab strip needs to present
    ///     many tabs in limited horizontal space.
    /// </summary>
    Compact,
}
