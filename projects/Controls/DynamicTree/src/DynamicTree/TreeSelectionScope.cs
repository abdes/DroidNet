// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

/// <summary>Defines which rendered tree items participate in pointer and range selection.</summary>
public enum TreeSelectionScope
{
    /// <summary>Selection and range operations use every expanded item, including filtered-out items.</summary>
    ShownItems,

    /// <summary>Selection and range operations use only the items currently supplied to the item repeater.</summary>
    DisplayedItems,
}
