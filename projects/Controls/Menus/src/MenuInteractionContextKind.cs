// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

/// <summary>
///     Enumerates the available menu interaction contexts.
/// </summary>
public enum MenuInteractionContextKind
{
    /// <summary>
    ///     Interaction targeting root menu items hosted by a menu bar.
    /// </summary>
    Root,

    /// <summary>
    ///     Interaction targeting a specific cascading menu column.
    /// </summary>
    Column,
}
