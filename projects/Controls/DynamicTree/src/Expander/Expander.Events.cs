// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

/// <summary>
/// Events for the <see cref="Expander" /> control.
/// </summary>
public partial class Expander
{
    /// <summary>
    /// Fires when the <see cref="Expander" /> switches to the expanded state.
    /// </summary>
    /// <remarks>
    /// This event should be used to expand the content related to this <see cref="Expander" />.
    /// </remarks>
    public event EventHandler? Expand;

    /// <summary>
    /// Fires when the <see cref="Expander" /> switches to the collapsed state.
    /// </summary>
    /// <remarks>
    /// This event should be used to collapse the content related to this <see cref="Expander" />.
    /// </remarks>
    public event EventHandler? Collapse;
}
