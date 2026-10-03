// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Controls;

/// <summary>Specifies the placement of a property's label and editor.</summary>
public enum PropertyLayout
{
    /// <summary>Choose the layout using the available property width.</summary>
    Auto,

    /// <summary>Place the label and editor on the same row.</summary>
    Inline,

    /// <summary>Place the label above a full-width editor.</summary>
    Stacked,
}
