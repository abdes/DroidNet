// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

/// <summary>
/// Represents traversal requests across menu columns.
/// </summary>
public enum MenuInteractionTraversalDirection
{
    /// <summary>
    /// Move focus back to the parent column.
    /// </summary>
    Parent,

    /// <summary>
    /// Move focus into a child column.
    /// </summary>
    Child,
}
