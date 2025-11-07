// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Aura.Controls;

/// <summary>
///     Interface for the TabStrip layout manager.
/// </summary>
public interface ITabStripLayoutManager
{
    /// <summary>
    ///     Gets or sets the maximum width for each item.
    /// </summary>
    public double MaxItemWidth { get; set; }

    /// <summary>
    ///     Gets or sets the preferred width for each item.
    /// </summary>
    public double PreferredItemWidth { get; set; }

    /// <summary>
    ///     Gets or sets the tab width policy.
    /// </summary>
    public TabWidthPolicy Policy { get; set; }

    /// <summary>
    ///     Computes the layout for the given request.
    /// </summary>
    /// <param name="request">The layout request.</param>
    /// <returns>The layout result.</returns>
    public LayoutResult ComputeLayout(LayoutRequest request);
}
