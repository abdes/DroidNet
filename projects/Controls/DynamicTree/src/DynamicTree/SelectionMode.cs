// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;

namespace DroidNet.Controls;

/// <summary>
///     Defines constants that specify the selection mode of items in a <see cref="DynamicTree" />.
/// </summary>
[SuppressMessage(
    "Naming",
    "CA1720:Identifier contains type name",
    Justification = "we want to keep the SelectionMode naming consistent with other WinUI controls")]
public enum SelectionMode
{
    /// <summary>
    ///     A user can't select items.
    /// </summary>
    None = 0,

    /// <summary>
    ///     A user can only select a single item.
    /// </summary>
    Single = 1,

    /// <summary>
    ///     The user can select multiple items.
    /// </summary>
    Multiple = 2,
}
