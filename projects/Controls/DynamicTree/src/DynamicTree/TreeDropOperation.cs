// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

/// <summary>Identifies the operation requested by a tree drop gesture.</summary>
public enum TreeDropOperation
{
    /// <summary>Move the source items into the requested destination.</summary>
    Move,

    /// <summary>Create copies of the source items in the requested destination.</summary>
    Copy,
}
