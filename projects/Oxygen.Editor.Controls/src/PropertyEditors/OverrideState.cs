// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Controls.PropertyEditors;

/// <summary>Specifies how many of the edited objects override a property's source value.</summary>
public enum OverrideState
{
    /// <summary>Every object takes the source's value; the row is unmarked.</summary>
    None,

    /// <summary>Some objects override the value; the row's bar is drawn at half strength.</summary>
    Partial,

    /// <summary>Every object overrides the value; the row's bar is drawn at full strength.</summary>
    Full,
}
