// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.Selection;

/// <summary>
/// Represents an item that can be selected within a collection.
/// </summary>
/// <remarks>
/// This interface is typically implemented by items that are part of a selection model, such as those used in list or grid controls.
/// Implementing this interface allows the selection state of an item to be managed automatically by the selection model.
/// </remarks>
public interface ISelectable
{
    /// <summary>
    /// Gets or sets a value indicating whether the item is selected.
    /// </summary>
    /// <value>
    /// <see langword="true"/> if the item is selected; otherwise, <see langword="false"/>.
    /// </value>
    /// <remarks>
    /// When implementing this property, ensure that any necessary change notifications are raised
    /// to update the UI or other dependent components.
    /// </remarks>
    public bool IsSelected { get; set; }
}
